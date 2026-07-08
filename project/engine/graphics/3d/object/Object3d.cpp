#define NOMINMAX
#include "Object3d.h"
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

namespace {

std::filesystem::path ResolveTerrainCollisionFilePath(const std::string& path) {
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

bool HasParentInChain(Object3d* parent, const Object3d* child) {
    for (Object3d* current = parent; current != nullptr; current = current->GetParent()) {
        if (current == child) {
            return true;
        }
    }
    return false;
}

void ApplyMatrixToTransform(Transform& transform, const Matrix4x4& matrix) {
    const float epsilon = 0.0001f;

    Vector3 rowX = { matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] };
    Vector3 rowY = { matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] };
    Vector3 rowZ = { matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] };

    transform.scale = {
        std::max(Math::Length(rowX), epsilon),
        std::max(Math::Length(rowY), epsilon),
        std::max(Math::Length(rowZ), epsilon),
    };
    transform.translate = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

    Matrix4x4 rotateMatrix = Math::MakeIdentity4x4();
    rotateMatrix.m[0][0] = matrix.m[0][0] / transform.scale.x;
    rotateMatrix.m[0][1] = matrix.m[0][1] / transform.scale.x;
    rotateMatrix.m[0][2] = matrix.m[0][2] / transform.scale.x;
    rotateMatrix.m[1][0] = matrix.m[1][0] / transform.scale.y;
    rotateMatrix.m[1][1] = matrix.m[1][1] / transform.scale.y;
    rotateMatrix.m[1][2] = matrix.m[1][2] / transform.scale.y;
    rotateMatrix.m[2][0] = matrix.m[2][0] / transform.scale.z;
    rotateMatrix.m[2][1] = matrix.m[2][1] / transform.scale.z;
    rotateMatrix.m[2][2] = matrix.m[2][2] / transform.scale.z;

    transform.quaternion = Math::MatrixToQuaternion(rotateMatrix);
    transform.rotate = Math::MatrixToEuler(rotateMatrix);
    transform.isQuaternionMaster = true;
}

}

// ========================================================================
// Object3d 基本処理
// ------------------------------------------------------------------------
// 生成、毎フレーム更新、親子Transform、MeshRendererへの基本委譲を担当する。
// コリジョン、JSON、付属エフェクトは別ファイルに分離して見通しを保つ。
// ========================================================================
Object3d::~Object3d() {
    if (recorder_) {
        delete recorder_;
        recorder_ = nullptr;
    }
    
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
    auto startAll = std::chrono::high_resolution_clock::now();
    
    // 毎フレームリセット
    cpuUpdateTimeMs_ = 0.0f;
    cpuAnimTimeMs_ = 0.0f;
    cpuMatrixTimeMs_ = 0.0f;

    // 収集アニメーション
    if (isCollecting_) {
        collectTimer_ += deltaTime;
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
        transform_.rotate.y += 3.0f * deltaTime;
        transform_.isQuaternionMaster = false;

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
        if (!animName_.empty()) {
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
        model->Update(appliedAnimation);
    }
    auto endAnim = std::chrono::high_resolution_clock::now();
    cpuAnimTimeMs_ = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(endAnim - startAnim).count()) / 1000.0f;

    // --- 行列・その他計測 ---
    auto startMat = std::chrono::high_resolution_clock::now();
    if (meshRenderer_) {
        meshRenderer_->Update();
    }
    UpdateWorldMatrix();

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
    // 1. CPUパーティクル (旧仕様)
    if (!particleName_.empty()) {
        Vector3 pos = GetWorldPosition();
        ParticleManager::GetInstance()->Emit(particleName_, pos, particleTimer_);
    }

    // 2. GPUパーティクル (新仕様)
    if (!gpuParticleName_.empty()) {
        // 未作成なら作成
        if (!gpuEmitter_) {
            gpuEmitter_ = std::make_unique<GPUParticleEmitter>();
            gpuEmitter_->Initialize(gpuParticleName_, this);
            gpuEmitter_->Play();
        }
        // 名前が不一致なら作り直し
        else if (gpuEmitter_->GetName() != gpuParticleName_) {
            gpuEmitter_->Initialize(gpuParticleName_, this);
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
    if (!isVisible_) return;
#ifdef DD // "Release" ビルドの時だけ有効になるマクロ
    if (className_ == "CinematicCamera" || className_ == "GPUParticle" || className_ == "InvisibleBox") {
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
    if (!isVisible_) return;
#ifdef DD
    if (className_ == "CinematicCamera" || className_ == "GPUParticle" || className_ == "InvisibleBox") {
        return;
    }
#endif
    if (meshRenderer_) {
        meshRenderer_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
}

void Object3d::UpdateLocalMatrix() {
    transform_.UpdateMatrix();
}

void Object3d::UpdateWorldMatrix() {
    transform_.UpdateMatrix();

    if (meshRenderer_) {
        meshRenderer_->Update();
    }
    for (Object3d* child : children_) {
        if (child) {
            child->UpdateWorldMatrix();
        }
    }
}

void Object3d::RefreshRenderCameraData() {
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

    Math math;
    Matrix4x4 worldBefore = transform_.matWorld;
    Matrix4x4 parentWorld = parent ? parent->GetWorldMatrix() : Math::MakeIdentity4x4();

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
        Matrix4x4 localMatrix = worldBefore;
        if (parent_) {
            localMatrix = math.Multiply(worldBefore, math.Inverse(parentWorld));
        }
        ApplyMatrixToTransform(transform_, localMatrix);
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
            Vector3 fullSize = model->GetSize();
            config.size = { fullSize.x / 2.0f, fullSize.y / 2.0f, fullSize.z / 2.0f };

            config.center = model->GetCenter();
            collider_->SetConfig(config);
        }
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
            Vector3 fullSize = model->GetSize();
            config.size = { fullSize.x / 2.0f, fullSize.y / 2.0f, fullSize.z / 2.0f };

            config.center = model->GetCenter();
            collider_->SetConfig(config);
        }
    }
}
void Object3d::DrawWater(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawWater(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawMagma(depthSrvHandle, colorSrvHandle);
    }
}
void Object3d::DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawIce(depthSrvHandle, colorSrvHandle);
    }
}
void Object3d::DrawFire(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawFire(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawLaser(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawLaser(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawSlimeGel(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawSlimeGel(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawShockwave(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawShockwave(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawLiquidContact(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawLiquidContact(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawDamageCrack(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawDamageCrack(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawUpdraft(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawUpdraft(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawStunBind(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawStunBind(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawCrownUnlock(depthSrvHandle, grabSrvHandle);
    }
}
void Object3d::DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawPoisonSpore(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawCloud(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawCloud(depthSrvHandle, grabSrvHandle);
    }
}

void Object3d::DrawGatePortal(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (meshRenderer_) {
        meshRenderer_->DrawGatePortal(depthSrvHandle, grabSrvHandle);
    }
}

Model* Object3d::GetModel() const {
    return meshRenderer_ ? meshRenderer_->GetModel() : nullptr;
}

std::string Object3d::GetModelName() const {
    return meshRenderer_ ? meshRenderer_->GetModelName() : "";
}

void Object3d::SetMeshDrawIndex(int meshIndex) {
    if (meshRenderer_) {
        meshRenderer_->SetMeshDrawIndex(meshIndex);
    }
}

int Object3d::GetMeshDrawIndex() const {
    return meshRenderer_ ? meshRenderer_->GetMeshDrawIndex() : -1;
}

bool Object3d::IsMeshDrawFiltered() const {
    return meshRenderer_ ? meshRenderer_->IsMeshDrawFiltered() : false;
}

void Object3d::SetLodEnabled(bool enabled) {
    if (meshRenderer_) {
        meshRenderer_->SetLodEnabled(enabled);
    }
}

bool Object3d::IsLodEnabled() const {
    return meshRenderer_ ? meshRenderer_->IsLodEnabled() : false;
}

bool Object3d::HasLodLevels() const {
    return meshRenderer_ ? meshRenderer_->HasLodLevels() : false;
}

const std::vector<Object3d::LodLevel>& Object3d::GetLodLevels() const {
    static const std::vector<LodLevel> empty;
    return meshRenderer_ ? meshRenderer_->GetLodLevels() : empty;
}

void Object3d::SetLodLevels(const std::vector<LodLevel>& levels) {
    if (meshRenderer_) {
        meshRenderer_->SetLodLevels(levels);
    }
}

void Object3d::ClearLodLevels() {
    if (meshRenderer_) {
        meshRenderer_->ClearLodLevels();
    }
}

bool Object3d::SetLodLevelDistance(int level, float distance) {
    return meshRenderer_ ? meshRenderer_->SetLodLevelDistance(level, distance) : false;
}

bool Object3d::ReloadLodManifest() {
    return meshRenderer_ ? meshRenderer_->LoadLodManifestForModel(GetModelName()) : false;
}

int Object3d::GetActiveLodLevel() const {
    return meshRenderer_ ? meshRenderer_->GetActiveLodLevel() : 0;
}

std::string Object3d::GetActiveLodModelName() const {
    return meshRenderer_ ? meshRenderer_->GetActiveModelName() : GetModelName();
}

float Object3d::GetLodCameraDistance() const {
    return meshRenderer_ ? meshRenderer_->GetCameraDistanceToObject() : 0.0f;
}

Vector4 Object3d::GetColor() const {
    return meshRenderer_ ? meshRenderer_->GetColor() : Vector4{ 1,1,1,1 };
}

void Object3d::SetColor(const Vector4& color) {
    if (meshRenderer_) meshRenderer_->SetColor(color);
}

void Object3d::SetBlendMode(BlendMode blendMode) {
    if (meshRenderer_) meshRenderer_->SetBlendMode(blendMode);
}

BlendMode Object3d::GetBlendMode() const {
    return meshRenderer_ ? meshRenderer_->GetBlendMode() : BlendMode::kNone;
}

void Object3d::SetIntensity(float intensity) {
    if (meshRenderer_) meshRenderer_->SetIntensity(intensity);
}

float Object3d::GetIntensity() const {
    if (meshRenderer_ && meshRenderer_->GetLightData()) {
        return meshRenderer_->GetLightData()->intensity;
    }
    return 1.0f;
}

// 古いアクセッサの互換性維持
Object3d::DirectionalLight* Object3d::GetDirectionalLightData() {
    return meshRenderer_ ? meshRenderer_->GetLightData() : nullptr;
}

Object3d::Material* Object3d::GetMaterialData() {
    return meshRenderer_ ? meshRenderer_->GetMaterialData() : nullptr;
}

void Object3d::SetMaterialType(int32_t type) {
    if (meshRenderer_) meshRenderer_->SetMaterialType(type);
}

void Object3d::SetShininess(float shininess) {
    if (meshRenderer_ && meshRenderer_->GetMaterialData()) {
        meshRenderer_->GetMaterialData()->shininess = shininess;
    }
}

int32_t Object3d::GetMaterialType() const {
    return meshRenderer_ ? meshRenderer_->GetMaterialType() : 0;
}

void Object3d::SetSelectedLighting(int32_t type) {
    if (meshRenderer_ && meshRenderer_->GetMaterialData()) {
        meshRenderer_->GetMaterialData()->selectedLighting = type;
    }
}

// ========================================================================
// 衝突判定 (Colliderへの委譲)
