#define NOMINMAX
#include "Object3d.h"
#include "Object3dTransformUtils.h"
#include "EditorToolSettings.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "EffectObject3d.h"
#include "SRVManager.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include "CollisionManager.h"
#include <cassert>
#include <algorithm> // min, max
#include <ParticleManager.h>
#include <GPUParticleManager.h>
#include <GPUParticleEmitter.h>
#include <DebugConsole.h>
#include <ProfilerManager.h>
#include <fstream>
#include <filesystem>

namespace engine {

namespace {

std::filesystem::path ResolveTerrainCollisionFilePath(const std::string& path) {
    // 保存済み相対パスはResources配下も試し、旧シーンの地形判定を解決する。
    std::filesystem::path filePath(path);
    if (std::filesystem::exists(filePath)) {
        return filePath;
    }

    std::filesystem::path resourcesPath = std::filesystem::path("Resources") / filePath;
    if (std::filesystem::exists(resourcesPath)) {
        return resourcesPath;
    }

    return filePath;
}

// 親候補の祖先に対象が含まれるか調べ、階層の循環を防ぎます。
bool HasParentInChain(Object3d* parent, const Object3d* child) {
    // 親の祖先に自分がいれば循環になるため、設定前に階層を調べる。
    for (Object3d* current = parent; current != nullptr; current = current->GetParent()) {
        if (current == child) {
            return true;
        }
    }
    return false;
}

}

// ========================================================================
// Object3d 基本処理
// ------------------------------------------------------------------------
// 生成、毎フレーム更新、親子Transform、MeshRendererへの基本委譲を担当する。
// コリジョン、JSON、付属エフェクトは別ファイルに分離して見通しを保つ。
// ========================================================================
Object3d::Object3d() = default;

Object3d::~Object3d() {
    // 他のメンバを片付ける前に、記録器の接続を解放する。
    recorder_.reset();

    // 親がいる場合、親の子供リストから自分自身を取り除く
    if (parent_) {
        std::vector<Object3d*>& kids = parent_->children_;
        kids.erase(std::remove(kids.begin(), kids.end(), this), kids.end());
        parent_ = nullptr;
    }
    
    // 自分が親で子供たちがいる場合、子供たちの親ポインタをクリアする
    for (auto* child : children_) {
        if (child) {
            child->parent_ = nullptr;
            child->transform_.parent = nullptr;
        }
    }
    children_.clear();
}

// ========================================================================
// 初期化
// ========================================================================
void Object3d::Initialize(Object3dCommon* common) {
    assert(common);
    common_ = common;
    EnsurePersistentGuid();
    EnsureReplayId();
    replayRetained_ = false;
    replayRemoved_ = false;
    particleEmitterComponent_.reset();
    meshEffectComponent_.reset();
    pathMoverComponent_.reset();
    gameplayLinkComponent_.reset();
    materialInstancePath_.clear();
    decalSettings_ = DecalSettings{};
    decalElapsedTime_ = 0.0f;
    decalAuthoredAlpha_ = 1.0f;

    // Transform初期化
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };
    transform_.translate = { 0.0f, 0.0f, 0.0f };
    transform_.parent = nullptr;

    // 1. コライダー (Transformと同期)
    collider_ = std::make_unique<Collider>(&transform_);

    // 2. メッシュレンダラー (Transformと同期)
    meshRenderer_ = std::make_unique<MeshRenderer>(&transform_);
    meshRenderer_->Initialize(common_);

    // 行列計算
    UpdateLocalMatrix();
    UpdateWorldMatrix();

    // レコーダー
    InitializeRecorder(nullptr);

    isCollecting_ = false;
    collectTimer_ = 0.0f;
    gpuEmitter_ = nullptr;
    isDead = false;
}

// ========================================================================
// 更新・描画
// ========================================================================

void Object3d::Update(float deltaTime) {
    if (meshRenderer_ && (GetMaterialType() == 30 || GetMaterialType() == 31)
        && std::isfinite(deltaTime) && deltaTime > 0.0f) {
        if (auto* material = meshRenderer_->GetMaterialData()) {
            material->time += deltaTime;
        }
    }
    if (meshRenderer_ && GetMaterialType() == 8) {
        meshRenderer_->AdvanceWaterAnimation(deltaTime);
    }
    auto startAll = std::chrono::high_resolution_clock::now();
    
    // 毎フレームリセット
    cpuUpdateTimeMs_ = 0.0f;
    cpuAnimTimeMs_ = 0.0f;
    cpuMatrixTimeMs_ = 0.0f;

    // 一時Decalだけ寿命を進めます。lifetimeが0の配置Decalは永続表示です。
    if (decalSettings_.enabled && decalSettings_.lifetime > 0.0f && isVisible_) {
        decalElapsedTime_ += std::max(deltaTime, 0.0f);
        float alphaFactor = 1.0f;
        if (decalSettings_.fadeIn > 0.0f) {
            alphaFactor = std::min(alphaFactor, decalElapsedTime_ / decalSettings_.fadeIn);
        }
        const float remaining = decalSettings_.lifetime - decalElapsedTime_;
        if (decalSettings_.fadeOut > 0.0f) {
            alphaFactor = std::min(alphaFactor, remaining / decalSettings_.fadeOut);
        }
        engine::Vector4 color = GetColor();
        color.w = decalAuthoredAlpha_ * std::clamp(alphaFactor, 0.0f, 1.0f);
        SetColor(color);
        if (decalElapsedTime_ >= decalSettings_.lifetime) {
            SetIsVisible(false);
            if (decalSettings_.transient) {
                isDead = true;
            }
        }
    }

    // 収集アニメーション
    if (isCollecting_) {
        collectTimer_ += deltaTime;
        // 収集中だけ回転アニメーションへ切り替えます。
        transform_.isQuaternionMaster = false;
        transform_.translate.y += 10.0f * deltaTime; // 上昇
        transform_.rotate.y += 15.0f * deltaTime;    // 回転
        if (collectTimer_ >= 0.5f) {
            isCollecting_ = false;
            isVisible_ = false;
            isDead = true; // 完全に消去
        }
    }
    else if (eventType_ == EventType::StarCoin && isVisible_) {
        // --- スターコインの常駐演出 ---
        // 常駐中にTransformを回し続けると、保存した向きが毎フレーム壊れて
        // エディター表示とゲーム中の見え方が一致しなくなります。
        // 保存された向きは変更せず、きらめきパーティクルだけで動きを出します。

        if (!gpuEmitter_) {
            gpuEmitter_ = std::make_unique<GPUParticleEmitter>();
            gpuEmitter_->Initialize("star_sparkle", this);
            gpuEmitter_->SetInterval(0.1f);
            gpuEmitter_->Play();
        }
    }

    if (gpuEmitter_) {
        gpuEmitter_->Update(deltaTime);
    }

    // --- アニメーション計測 ---
    auto startAnim = std::chrono::high_resolution_clock::now();
    if (meshRenderer_ && meshRenderer_->GetModel()) {
        bool appliedAnimation = false;
        Model* model = meshRenderer_->GetModel();
        const bool controllerApplied = UpdateAnimatorController(deltaTime, model);
        if (controllerApplied) {
            appliedAnimation = true;
        } else if (!animName_.empty()) {
            const Model::Animation* anim = model->GetAnimation(animName_);
            if (anim) {
                animationTime_ += deltaTime;
                float time = animationTime_;
                if (isAnimLoop_ && anim->duration > 0.0f) {
                    time = std::fmod(time, anim->duration);
                } else {
                    time = std::min(time, anim->duration);
                }
                model->ApplyAnimation(*anim, time);
                appliedAnimation = true;
            }
        }
        if (!controllerApplied) {
            model->Update(appliedAnimation);
        }
    }
    auto endAnim = std::chrono::high_resolution_clock::now();
    cpuAnimTimeMs_ = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(endAnim - startAnim).count()) / 1000.0f;

    // --- 行列・その他計測 ---
    auto startMat = std::chrono::high_resolution_clock::now();
    // 派生クラスがこの後Transformを変更する可能性があるため、ここでは
    // エフェクト追従に必要な行列だけを更新します。描画定数はObjectManagerが
    // 全オブジェクトのロジック更新後に一度だけ確定します。
    UpdateWorldMatrix(false);

    if (recorder_) {
        recorder_->Update();
    }
    UpdateParticle();
    UpdateAttachedEffects(deltaTime);
    auto endMat = std::chrono::high_resolution_clock::now();
    cpuMatrixTimeMs_ = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(endMat - startMat).count()) / 1000.0f;

    auto endAll = std::chrono::high_resolution_clock::now();
    cpuUpdateTimeMs_ = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(endAll - startAll).count()) / 1000.0f;
}

void Object3d::UpdateParticle() {
    ParticleEmitterComponent* particle = GetParticleEmitterComponent();
    if (!particle) {
        gpuEmitter_.reset();
        return;
    }

    // 1. CPUパーティクル (旧仕様)
    if (!particle->GetCpuParticle().empty()) {
        engine::Vector3 pos = GetWorldPosition();
        ParticleManager::GetInstance()->Emit(
            particle->GetCpuParticle(),
            pos,
            particle->GetEmissionTimer());
    }

    // 2. GPUパーティクル (新仕様)
    if (!particle->GetGpuParticle().empty()) {
        // 未作成なら作成
        if (!gpuEmitter_) {
            gpuEmitter_ = std::make_unique<GPUParticleEmitter>();
            gpuEmitter_->Initialize(particle->GetGpuParticle(), this);
            gpuEmitter_->Play();
        }
        // 名前が不一致なら作り直し
        else if (gpuEmitter_->GetName() != particle->GetGpuParticle()) {
            gpuEmitter_->Initialize(particle->GetGpuParticle(), this);
            gpuEmitter_->Play();
        }
    }
    else {
        // 名前が消えたらエミッターも消す
        if (gpuEmitter_) {
            gpuEmitter_ = nullptr;
        }
    }
}

void Object3d::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!GetIsRenderVisible()) return;
#ifdef USE_IMGUI
    // シーン配置カメラも補助表示です。動作や保存データは変更せず描画だけを抑制します。
    if (IsCameraObject() && !EditorToolSettings::Get().IsVisible("cameraBody")) return;
    if ((className_ == "GPUParticle" || className_ == "InvisibleBox") && !EditorToolSettings::Get().IsVisible("helperModels")) return;
#endif
    if (IsCameraObject()) {
        SceneManager* sceneManager = SceneManager::GetInstance();
        if ((sceneManager && sceneManager->IsPlaying()) || DirectXCommon::GetInstance()->IsCameraPreviewRendering()) {
            return;
        }
    }
#ifdef DD // "Release" ビルドの時だけ有効になるマクロ
    if (IsCameraObject() || className_ == "GPUParticle" || className_ == "InvisibleBox") {
        return; // 何も描画せずに帰る（門前払い）
    }
#endif
    if (meshRenderer_) {
        bool sampling = ProfilerManager::GetInstance()->IsGpuSampling();
        if (sampling) {
            common_->GetDxCommon()->StartGpuProfile(name_);
        }

        meshRenderer_->Draw(pointLightResource, spotLightResource);

        if (sampling) {
            common_->GetDxCommon()->EndGpuProfile(name_);
        }
    }
    // ★ DrawAttachedEffects はここでは呼ばない！
    //    エフェクトの歪み(Distortion)がGrabTextureを参照するため、
    //    GrabTexture更新後の専用パスで描画する必要がある。
}

// ========================================================================
// トランスフォーム操作 (Transformへの委譲)
// ========================================================================

void Object3d::DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex) {
    // プレビュー用カメラでは補助モデルの表示条件を揃え、通常描画と同じ可視性を守る。
    if (!GetIsRenderVisible()) return;
#ifdef USE_IMGUI
    if (IsCameraObject() && !EditorToolSettings::Get().IsVisible("cameraBody")) return;
    if ((className_ == "GPUParticle" || className_ == "InvisibleBox") && !EditorToolSettings::Get().IsVisible("helperModels")) return;
#endif
    if (IsCameraObject() && DirectXCommon::GetInstance()->IsCameraPreviewRendering()) {
        return;
    }
#ifdef DD
    if (IsCameraObject() || className_ == "GPUParticle" || className_ == "InvisibleBox") {
        return;
    }
#endif
    if (meshRenderer_) {
        meshRenderer_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
}

void Object3d::UpdateLocalMatrix() {
    // ローカルのTransform値を行列へ確定する。
    transform_.UpdateMatrix();
}

void Object3d::UpdateWorldMatrix(bool refreshRenderer) {
    // 親子階層を順に更新し、必要なときだけ描画定数も更新する。
    transform_.UpdateMatrix();

    if (refreshRenderer && meshRenderer_) {
        meshRenderer_->Update();
    }
    for (Object3d* child : children_) {
        if (child) {
            child->UpdateWorldMatrix(refreshRenderer);
        }
    }
}

void Object3d::RefreshRenderCameraData() {
    // プレビューカメラの切替後に、子も含めてカメラ依存の描画値を更新する。
    transform_.UpdateMatrix();

    if (meshRenderer_) {
        meshRenderer_->RefreshCameraDependentData();
    }
    for (Object3d* child : children_) {
        if (child) {
            child->RefreshRenderCameraData();
        }
    }
}

void Object3d::SetParent(Object3d* parent, bool keepWorldTransform) {
    // 親子の循環を拒否し、指定された場合は付け替え前のワールド姿勢を維持する。
    if (parent == this || HasParentInChain(parent, this)) {
        return;
    }

    if (parent == parent_ && !keepWorldTransform) {
        return;
    }

    UpdateWorldMatrix();
    if (parent) {
        parent->UpdateWorldMatrix();
    }

    engine::Math math;
    engine::Matrix4x4 worldBefore = transform_.matWorld;
    engine::Matrix4x4 parentWorld = parent ? parent->GetWorldMatrix() : engine::Math::MakeIdentity4x4();

    if (parent_) {
        std::vector<Object3d*>& kids = parent_->children_;
        kids.erase(std::remove(kids.begin(), kids.end(), this), kids.end());
    }

    parent_ = parent;

    if (parent_) {
        if (std::find(parent_->children_.begin(), parent_->children_.end(), this) == parent_->children_.end()) {
            parent_->children_.push_back(this);
        }
        transform_.parent = parent_->GetTransform();
    } else {
        transform_.parent = nullptr;
    }

    if (keepWorldTransform) {
        engine::Matrix4x4 localMatrix = worldBefore;
        if (parent_) {
            localMatrix = math.Multiply(worldBefore, math.Inverse(parentWorld));
        }
        Object3dTransformUtils::ApplyMatrixToTransform(transform_, localMatrix);
    }

    UpdateWorldMatrix();
}

// ========================================================================
// グラフィックス設定 (MeshRendererへの委譲)
// ========================================================================

void Object3d::SetModel(Model* model) {
    if (meshRenderer_) {
        meshRenderer_->SetModel(model);

        // =======================================================
        //  モデルのサイズに合わせてコライダーを自動設定 (Auto-Fit)
        // =======================================================
        if (model && collider_) {
            ColliderConfig config = collider_->GetConfig();
            engine::Vector3 fullSize = model->GetSize();
            config.size = { fullSize.x / 2.0f, fullSize.y / 2.0f, fullSize.z / 2.0f };

            config.center = model->GetCenter();
            collider_->SetConfig(config);
        }
        RefreshModelSurfaceCollision();
    }
}

void Object3d::SetModel(const std::string& modelName) {
    if (modelName.empty()) {
        SetModel(nullptr);
        return;
    }
    if (meshRenderer_ && !modelName.empty()) {
        Model* model = ModelManager::GetInstance()->LoadModel(modelName);
        meshRenderer_->SetModel(modelName);

        // =======================================================
        //  モデルのサイズに合わせてコライダーを自動設定 (Auto-Fit)
        // =======================================================
        if (model && collider_) {
            ColliderConfig config = collider_->GetConfig();
            engine::Vector3 fullSize = model->GetSize();
            config.size = { fullSize.x / 2.0f, fullSize.y / 2.0f, fullSize.z / 2.0f };

            config.center = model->GetCenter();
            collider_->SetConfig(config);
        }
        RefreshModelSurfaceCollision();
    }
}

void Object3d::DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // シーンや弾の管理者は材質番号を知らず、描画コンポーネントに選択を委ねる。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawSpecialMaterial(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawWater(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 可視メッシュだけを深度・背景参照付きの水描画パスへ送る。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawWater(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // マグマ用パスでは深度と色バッファをレンダラーへ渡す。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawMagma(depthSrvHandle, colorSrvHandle);
    }
}

void Object3d::DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 氷材質は画面色を参照する専用パスで可視メッシュだけ描く。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawIce(depthSrvHandle, colorSrvHandle);
    }
}

void Object3d::DrawFire(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 背景を参照する炎の描画を通常の不透明パスから分離する。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawFire(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawLaser(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // レーザー材質は深度と背景参照を使う専用パスへ送る。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawLaser(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawSlimeGel(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 半透明のスライム材質を背景参照付きの専用パスで描く。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawSlimeGel(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawShockwave(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 衝撃波の背景歪みを扱う描画パスへ渡す。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawShockwave(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawLiquidContact(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 液面との接触表現を深度・背景参照付きパスに送る。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawLiquidContact(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawDamageCrack(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 被弾亀裂を通常材質とは別の画面参照パスへ送る。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawDamageCrack(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawUpdraft(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 上昇気流の透過表現を専用パスで描く。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawUpdraft(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawStunBind(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 拘束演出が背景を参照できる専用パスへ送る。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawStunBind(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 王冠解放の特殊材質を通常のメッシュ描画から分ける。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawCrownUnlock(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 毒胞子の透過描画を専用パスへ送る。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawPoisonSpore(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawCloud(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 雲の背景参照処理をレンダラーの専用パスへ委譲する。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawCloud(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawGatePortal(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // ゲートのポータル材質を深度・背景参照付きで描く。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawGatePortal(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawSpecialMaterialForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle, int previewBufferIndex) {
    // プレビューでは材質番号と専用カメラを渡し、本番と同じ特殊材質パスを使う。
    if (!GetIsRenderVisible() || !meshRenderer_) {
        return;
    }
    meshRenderer_->DrawSpecialMaterialForCamera(
        GetMaterialType(),
        camera,
        depthSrvHandle,
        grabSrvHandle,
        previewBufferIndex);
}

void Object3d::DrawWindOrb(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 風球の背景歪みを専用パスで描く。
    if (GetIsRenderVisible() && meshRenderer_) {
        meshRenderer_->DrawWindOrb(depthSrvHandle, grabSrvHandle);
    }
}

Model* Object3d::GetModel() const {
    // メッシュレンダラーが無い場合はモデルも存在しない。
    return meshRenderer_ ? meshRenderer_->GetModel() : nullptr;
}

std::string Object3d::GetModelName() const {
    // モデル未設定なら空文字列を返し、管理器への参照を増やさない。
    return meshRenderer_ ? meshRenderer_->GetModelName() : "";
}

void Object3d::SetMeshDrawIndex(int meshIndex) {
    // 複数メッシュの表示対象をレンダラー側で切り替える。
    if (meshRenderer_) {
        meshRenderer_->SetMeshDrawIndex(meshIndex);
    }
}

int Object3d::GetMeshDrawIndex() const {
    // レンダラーが無いときは-1を未選択として返す。
    return meshRenderer_ ? meshRenderer_->GetMeshDrawIndex() : -1;
}

bool Object3d::IsMeshDrawFiltered() const {
    // メッシュ単位の表示制限はレンダラーがある場合だけ有効にする。
    return meshRenderer_ ? meshRenderer_->IsMeshDrawFiltered() : false;
}

void Object3d::SetLodEnabled(bool enabled) {
    // 距離に応じたモデル切替の設定をレンダラーへ渡す。
    if (meshRenderer_) {
        meshRenderer_->SetLodEnabled(enabled);
    }
}

bool Object3d::IsLodEnabled() const {
    // レンダラー未初期化時はLODを無効として扱う。
    return meshRenderer_ ? meshRenderer_->IsLodEnabled() : false;
}

bool Object3d::HasLodLevels() const {
    // モデルに切替段階が定義されているかを問い合わせる。
    return meshRenderer_ ? meshRenderer_->HasLodLevels() : false;
}

const std::vector<Object3d::LodLevel>& Object3d::GetLodLevels() const {
    // レンダラーが無い場合も寿命の安定した空一覧を返す。
    static const std::vector<LodLevel> empty;
    return meshRenderer_ ? meshRenderer_->GetLodLevels() : empty;
}

void Object3d::SetLodLevels(const std::vector<LodLevel>& levels) {
    // 編集したLOD段階を描画側へ反映する。
    if (meshRenderer_) {
        meshRenderer_->SetLodLevels(levels);
    }
}

void Object3d::ClearLodLevels() {
    // 距離別モデルの手動設定を描画側から解除する。
    if (meshRenderer_) {
        meshRenderer_->ClearLodLevels();
    }
}

bool Object3d::SetLodLevelDistance(int level, float distance) {
    // 指定段階が編集できなければfalseを返し、呼び出し側へ失敗を伝える。
    return meshRenderer_ ? meshRenderer_->SetLodLevelDistance(level, distance) : false;
}

bool Object3d::ReloadLodManifest() {
    // 現在のモデル名に対応するLOD定義を読み直す。
    return meshRenderer_ ? meshRenderer_->LoadLodManifestForModel(GetModelName()) : false;
}

int Object3d::GetActiveLodLevel() const {
    // LOD未使用時は基準段階の0を返す。
    return meshRenderer_ ? meshRenderer_->GetActiveLodLevel() : 0;
}

std::string Object3d::GetActiveLodModelName() const {
    // LOD描画中のモデル名を返し、未設定なら元モデル名に戻す。
    return meshRenderer_ ? meshRenderer_->GetActiveModelName() : GetModelName();
}

float Object3d::GetLodCameraDistance() const {
    // レンダラー未初期化時はカメラ距離を0として返す。
    return meshRenderer_ ? meshRenderer_->GetCameraDistanceToObject() : 0.0f;
}

engine::Vector4 Object3d::GetColor() const {
    // 材質が無い場合は不透明な白を既定色として返す。
    return meshRenderer_ ? meshRenderer_->GetColor() : engine::Vector4{ 1,1,1,1 };
}

void Object3d::SetColor(const engine::Vector4& color) {
    // 表示色は実際の材質を持つレンダラーへだけ反映する。
    if (meshRenderer_) meshRenderer_->SetColor(color);
}

void Object3d::SetBlendMode(BlendMode blendMode) {
    // 透過の描画方式をレンダラーの材質設定へ渡す。
    if (meshRenderer_) meshRenderer_->SetBlendMode(blendMode);
}

BlendMode Object3d::GetBlendMode() const {
    // 材質が無い場合はブレンドなしとして返す。
    return meshRenderer_ ? meshRenderer_->GetBlendMode() : BlendMode::kNone;
}

void Object3d::SetIntensity(float intensity) {
    // オブジェクトごとの照明強度をレンダラーへ反映する。
    if (meshRenderer_) meshRenderer_->SetIntensity(intensity);
}

float Object3d::GetIntensity() const {
    // 光源データが未設定なら中立の強度1を使う。
    if (meshRenderer_ && meshRenderer_->GetLightData()) {
        return meshRenderer_->GetLightData()->intensity;
    }
    return 1.0f;
}

// 古いアクセッサの互換性維持
Object3d::DirectionalLight* Object3d::GetDirectionalLightData() {
    // 旧APIとの互換用にレンダラー所有の光源データを公開する。
    return meshRenderer_ ? meshRenderer_->GetLightData() : nullptr;
}

Object3d::Material* Object3d::GetMaterialData() {
    // 旧APIからはレンダラー所有の材質データを参照させる。
    return meshRenderer_ ? meshRenderer_->GetMaterialData() : nullptr;
}

void Object3d::SetMaterialType(int32_t type) {
    // 特殊材質の種類を描画側の設定へ渡す。
    if (meshRenderer_) meshRenderer_->SetMaterialType(type);
}

void Object3d::SetShininess(float shininess) {
    // 材質が存在するときだけ旧来の光沢値を書き換える。
    if (meshRenderer_ && meshRenderer_->GetMaterialData()) {
        meshRenderer_->GetMaterialData()->shininess = shininess;
    }
}

int32_t Object3d::GetMaterialType() const {
    // 材質未設定なら標準材質の番号0を返す。
    return meshRenderer_ ? meshRenderer_->GetMaterialType() : 0;
}

void Object3d::SetSelectedLighting(int32_t type) {
    // 材質が存在するときだけライティングの選択値を更新する。
    if (meshRenderer_ && meshRenderer_->GetMaterialData()) {
        meshRenderer_->GetMaterialData()->selectedLighting = type;
    }
}

// ========================================================================
// 衝突判定 (Colliderへの委譲)

} // namespace engine
