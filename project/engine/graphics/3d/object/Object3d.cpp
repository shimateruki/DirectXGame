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

void Object3d::SetParent(Object3d* parent) {
    if (parent_) {
        std::vector<Object3d*>& kids = parent_->children_;
        kids.erase(std::remove(kids.begin(), kids.end(), this), kids.end());
    }

    parent_ = parent;

    if (parent_) {
        parent_->children_.push_back(this);
        transform_.parent = parent_->GetTransform();
    } else {
        transform_.parent = nullptr;
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
Model* Object3d::GetModel() const {
    return meshRenderer_ ? meshRenderer_->GetModel() : nullptr;
}

std::string Object3d::GetModelName() const {
    return meshRenderer_ ? meshRenderer_->GetModelName() : "";
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
// ========================================================================

void Object3d::SetColliderConfig(const ColliderConfig& config) {
    if (collider_) {
        collider_->SetConfig(config);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}
const Object3d::ColliderConfig& Object3d::GetColliderConfig() const {
    return collider_->GetConfig();
}

void Object3d::SetColliderType(ColliderType type) {
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.type = type;
    collider_->SetConfig(config);
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}
ColliderType Object3d::GetColliderType() const {
    return collider_ ? collider_->GetType() : ColliderType::kNone;
}

void Object3d::SetCollisionSize(const Vector3& size) {
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.size = size;
    collider_->SetConfig(config);
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}
Vector3 Object3d::GetCollisionSize() const {
    return collider_ ? collider_->GetSize() : Vector3{ 0,0,0 };
}

void Object3d::SetCollisionRadius(float radius) {
    SetCollisionSize({ radius, radius, radius });
}
float Object3d::GetCollisionRadius() const {
    return collider_ ? collider_->GetRadius() : 0.0f;
}

void Object3d::SetCollisionAttribute(uint32_t attribute) {
    if (collider_) {
        collider_->SetAttribute(attribute);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}
uint32_t Object3d::GetCollisionAttribute() const {
    return collider_ ? collider_->GetAttribute() : 0;
}

void Object3d::SetCollisionMask(uint32_t mask) {
    if (collider_) {
        collider_->SetMask(mask);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}
uint32_t Object3d::GetCollisionMask() const {
    return collider_ ? collider_->GetMask() : 0;
}

AABB Object3d::GetAABB() const {
    return collider_ ? collider_->GetAABB() : AABB{};
}
OBB Object3d::GetOBB() const {
    return collider_ ? collider_->GetOBB() : OBB{};
}

AABB Object3d::GetModelWorldAABB() const {
    Model* model = GetModel();
    if (!model) {
        // モデルがない場合は位置を基準にデフォルトサイズ
        return { {transform_.translate.x - 0.5f, transform_.translate.y - 0.5f, transform_.translate.z - 0.5f},
                 {transform_.translate.x + 0.5f, transform_.translate.y + 0.5f, transform_.translate.z + 0.5f} };
    }

    Vector3 min = model->GetLocalAabbMin();
    Vector3 max = model->GetLocalAabbMax();

    // 8つの頂点をトランスフォーム
    Vector3 corners[8] = {
        {min.x, min.y, min.z},
        {min.x, min.y, max.z},
        {min.x, max.y, min.z},
        {min.x, max.y, max.z},
        {max.x, min.y, min.z},
        {max.x, min.y, max.z},
        {max.x, max.y, min.z},
        {max.x, max.y, max.z}
    };

    AABB worldAABB;
    worldAABB.min = { FLT_MAX, FLT_MAX, FLT_MAX };
    worldAABB.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    for (int i = 0; i < 8; ++i) {
        Vector3 worldPos = Math::Transform(corners[i], transform_.matWorld);
        worldAABB.min.x = (std::min)(worldAABB.min.x, worldPos.x);
        worldAABB.min.y = (std::min)(worldAABB.min.y, worldPos.y);
        worldAABB.min.z = (std::min)(worldAABB.min.z, worldPos.z);
        worldAABB.max.x = (std::max)(worldAABB.max.x, worldPos.x);
        worldAABB.max.y = (std::max)(worldAABB.max.y, worldPos.y);
        worldAABB.max.z = (std::max)(worldAABB.max.z, worldPos.z);
    }

    return worldAABB;
}

CollisionInfo Object3d::CheckCollision(Object3d* other) {
    if (!collider_ || !other || !other->GetCollider()) {
        CollisionInfo info;
        info.isColliding = false;
        return info;
    }
    if (GetColliderType() == ColliderType::kNone ||
        other->GetColliderType() == ColliderType::kNone ||
        GetCollisionAttribute() == 0 || other->GetCollisionAttribute() == 0 ||
        GetCollisionMask() == 0 || other->GetCollisionMask() == 0) {
        CollisionInfo info;
        info.isColliding = false;
        return info;
    }
    return collider_->CheckCollision(other->GetCollider());
}

// ========================================================================
// その他 (コピー、保存、レコーダー等)
// ========================================================================

void Object3d::InitializeRecorder(SceneManager* sceneManager) {
    if (recorder_) {
        delete recorder_;
    }
    recorder_ = new GhostRecorder();
    recorder_->Initialize(sceneManager);
    recorder_->SetTarget(this);
}


std::unique_ptr<Object3d> Object3d::Clone() const {
    auto newObj = std::make_unique<Object3d>();
    assert(common_ != nullptr);
    newObj->Initialize(common_);
    newObj->CopyFrom(this);
    return newObj;
}


void Object3d::DrawShadow() {
    if (meshRenderer_) {
        meshRenderer_->DrawShadow();
    }
}
void Object3d::SetShadowCommonState() {
    if (meshRenderer_) {
        meshRenderer_->SetShadowCommonState();
    }
}
void Object3d::DrawShadowOnly() {
    if (meshRenderer_) {
        meshRenderer_->DrawShadowOnly();
    }
}

void Object3d::DrawLocalFog(uint32_t depthSrvHandle) {
    if (meshRenderer_) {
        // メッシュレンダラーに描画を丸投げ！
        meshRenderer_->DrawLocalFog(depthSrvHandle);
    }
}

MeshRenderer::LocalFogData* Object3d::GetLocalFogData() {
    return meshRenderer_ ? meshRenderer_->GetLocalFogData() : nullptr;
}

void Object3d::CopyFrom(const Object3d* other) {
    if (!other) return;

    // 1. 基本設定・識別子
    this->name_ = other->name_;
    this->className_ = other->className_;
    this->saveCategory_ = other->saveCategory_;
    this->enemyType_ = other->enemyType_;
    this->gimmickType_ = other->gimmickType_;
    this->itemType_ = other->itemType_;
    this->isVisible_ = other->isVisible_;
    this->isLocked_ = other->isLocked_;
    if (!other->GetModelName().empty()) {
        this->SetModel(other->GetModelName());
    }

    // 2. Transform構造体 (位置・回転・クォータニオン・スケールを完全コピー)
    this->transform_ = other->transform_;

    // 3. Collider ＆ 衝突属性
    if (collider_ && other->collider_) {
        this->SetColliderConfig(other->GetColliderConfig());
        this->SetCollisionAttribute(other->GetCollisionAttribute());
        this->SetCollisionMask(other->GetCollisionMask());
    }

    // 4. イベント関連
    this->eventType_ = other->eventType_;
    this->SetTargetID(other->GetTargetID());
    this->SetEventID(other->GetEventID());

    // 5. Stats (Param)
    this->param_ = other->param_;

    // 6. MeshRenderer (グラフィックス・マテリアル・PBR設定)
    if (meshRenderer_ && other->meshRenderer_) {
        this->SetColor(other->GetColor());
        this->SetBlendMode(other->GetBlendMode());
        this->SetMaterialType(other->GetMaterialType());

        // ★追加: 金属度と粗さ
        this->SetMetallic(other->GetMetallic());
        this->SetRoughness(other->GetRoughness());

        // テクスチャ・マップ群
        this->SetEnableNormalMap(other->GetEnableNormalMap());
        this->SetNormalMap(other->GetNormalMapPath());
        this->SetOrmMap(other->GetOrmMapPath());
        this->SetTexture(other->GetTexturePath());

        // 環境マップ
        this->SetEnableEnvMap(other->GetEnableEnvMap());
        this->SetEnvIntensity(other->GetEnvIntensity());
        this->SetEmissive(other->GetEmissive());
    }

    // 7. アニメーション
    this->animName_ = other->animName_;
    this->isAnimLoop_ = other->isAnimLoop_;

    // パーティクル
    this->particleName_ = other->particleName_;
    this->gpuParticleName_ = other->gpuParticleName_;

    // メッシュエフェクト
    this->meshEffectName1_ = other->meshEffectName1_;
    this->meshEffectName2_ = other->meshEffectName2_;

    // 8. レコーダー (Ghost)
    this->recordPathName_ = other->recordPathName_;
    this->isRecordLoop_ = other->isRecordLoop_;
    this->isRecordRelative_ = other->isRecordRelative_;

    this->InitializeRecorder(nullptr);
    if (!this->recordPathName_.empty() && this->recorder_) {
        bool isCinematic = (this->className_ == "CinematicCamera");
        this->recorder_->Play(this->recordPathName_, this->isRecordLoop_, this->isRecordRelative_, isCinematic);
    }

    // 9. ローカルフォグ (もし両方にフォグデータがあれば構造体ごとコピー)
    auto myFog = this->GetLocalFogData();
    auto otherFog = const_cast<Object3d*>(other)->GetLocalFogData();
    if (myFog && otherFog) {
        *myFog = *otherFog;
    }
}
json Object3d::ExportToJson() {
    json d;

    // 1. 基本設定
    d["name"] = name_;
    d["modelName"] = GetModelName();
    d["type"] = className_;
    d["saveCategory"] = saveCategory_;
    d["enemyType"] = enemyType_;
    d["gimmickType"] = gimmickType_;
    d["itemType"] = itemType_;
    d["isVisible"] = isVisible_;
    d["isLocked"] = isLocked_;

    // 2. Transform
    d["translate"] = { transform_.translate.x, transform_.translate.y, transform_.translate.z };
    d["scale"] = { transform_.scale.x, transform_.scale.y, transform_.scale.z };
    d["rotate"] = { transform_.rotate.x, transform_.rotate.y, transform_.rotate.z };
    d["quaternion"] = { transform_.quaternion.x, transform_.quaternion.y, transform_.quaternion.z, transform_.quaternion.w };

    // 3. Collider ＆ 衝突属性
    if (collider_) {
        const auto& c = collider_->GetConfig();
        d["collider"]["type"] = static_cast<int>(c.type);
        d["collider"]["size"] = { c.size.x, c.size.y, c.size.z };
        d["collider"]["center"] = { c.center.x, c.center.y, c.center.z };
        d["collider"]["rotation"] = { c.rotation.x, c.rotation.y, c.rotation.z };
    }
    d["collisionAttribute"] = GetCollisionAttribute();
    d["collisionMask"] = GetCollisionMask();

    // 4. イベント関連
    d["eventType"] = static_cast<int>(eventType_);
     d["targetID"] = GetTargetID();
     d["myEventID"] = GetEventID();

    // 5. Stats (Param)
    if (param_.has_value()) {
        auto& p = param_.value();
        d["param"]["hp"] = p.hp;
        d["param"]["maxHp"] = p.maxHp;
        d["param"]["speed"] = p.speed;
        json jp;
        jp["hp"] = p.hp;
        jp["maxHp"] = p.maxHp;
        jp["speed"] = p.speed;
        jp["gravity"] = p.gravity;
        jp["jumpPower"] = p.jumpPower;
        jp["maxFallSpeed"] = p.maxFallSpeed;
        jp["enemyType"] = p.enemyType;
        jp["gimmickType"] = p.gimmickType;
        jp["itemType"] = p.itemType;
        jp["healAmount"] = p.healAmount;
        jp["interval"] = p.interval;
        jp["maxCount"] = p.maxCount;
        jp["detectionRange"] = p.detectionRange;
        jp["colorType"] = p.colorType;
        jp["shakeDuration"] = p.shakeDuration;
        jp["fallDuration"] = p.fallDuration;
        jp["switchMode"] = p.switchMode;
        jp["actionMode"] = p.actionMode;
        jp["moveAmount"] = p.moveAmount;
        jp["moveSpeed"] = p.moveSpeed;
        jp["startActive"] = p.startActive;
        jp["returnOnOff"] = p.returnOnOff;
        d["param"] = jp;
    }

    // 6. グラフィックス・マテリアル
    Vector4 col = GetColor();
    d["color"] = { col.x, col.y, col.z, col.w };
    d["blendMode"] = static_cast<int>(GetBlendMode());
    d["materialType"] = GetMaterialType();

    // ★追加: 金属度と粗さ
    d["metallic"] = GetMetallic();
    d["roughness"] = GetRoughness();

    d["meshEffect1"] = meshEffectName1_;
    d["meshEffect2"] = meshEffectName2_;

    d["enableNormalMap"] = GetEnableNormalMap();
    d["normalMapPath"] = GetNormalMapPath();
    d["ormMapPath"] = GetOrmMapPath();
    d["texturePath"] = GetTexturePath();
    d["enableEnvMap"] = GetEnableEnvMap();
    d["envIntensity"] = GetEnvIntensity();
    d["emissive"] = GetEmissive();
    if (GetMaterialType() >= 8 && GetMaterialType() <= 11 && GetMeshRenderer() && GetMeshRenderer()->GetWaterParamData()) {
        auto* water = GetMeshRenderer()->GetWaterParamData();
        json jw;
        jw["waveSpeed"] = water->waveSpeed;
        jw["waveHeight"] = water->waveHeight;
        jw["waveFrequency"] = water->waveFrequency;
        jw["flowSpeedX"] = water->flowSpeedX;
        jw["flowSpeedY"] = water->flowSpeedY;
        jw["effectType"] = water->effectType;
        jw["effectScale"] = water->effectScale;
        jw["effectSoftness"] = water->effectSoftness;
        jw["effectIntensity"] = water->effectIntensity;
        jw["billboardScale"] = water->billboardScale;
        d["waterParam"] = jw;
    }
    // 7. アニメーション
    d["animation"]["animName"] = animName_;
    d["animation"]["isAnimLoop"] = isAnimLoop_;

    // 8. レコーダー (Ghost)
    d["recorder"]["recordPathName"] = recordPathName_;
    d["recorder"]["isRecordLoop"] = isRecordLoop_;
    d["recorder"]["isRecordRelative"] = isRecordRelative_;

    // 9. ローカルフォグ
    if (auto* fogData = GetLocalFogData()) {
        d["localFog"]["color"] = { fogData->fogColor.x, fogData->fogColor.y, fogData->fogColor.z, fogData->fogColor.w };
        d["localFog"]["density"] = fogData->fogDensity;
        d["localFog"]["edgeFade"] = fogData->edgeFade;
        d["localFog"]["noiseSpeed"] = fogData->noiseSpeed;
        d["localFog"]["noiseScale"] = fogData->noiseScale;
        d["localFog"]["scatteringG"] = fogData->scatteringG;
        d["localFog"]["scatteringIntensity"] = fogData->scatteringIntensity;
    }

    return d;
}

void Object3d::ImportFromJson(const json& j) {
    // 1. 基本設定
    if (j.contains("modelName")) SetModel(j["modelName"].get<std::string>());
    if (j.contains("type")) className_ = j["type"];
    if (j.contains("saveCategory")) saveCategory_ = j["saveCategory"];
    if (j.contains("enemyType")) enemyType_ = j["enemyType"];
    if (j.contains("gimmickType")) gimmickType_ = j["gimmickType"];
    if (j.contains("itemType")) itemType_ = j["itemType"];
    if (j.contains("isVisible")) isVisible_ = j["isVisible"];
    if (j.contains("isLocked")) isLocked_ = j["isLocked"];

    // 2. Transform
    if (j.contains("translate")) transform_.translate = { j["translate"][0], j["translate"][1], j["translate"][2] };
    if (j.contains("scale")) transform_.scale = { j["scale"][0], j["scale"][1], j["scale"][2] };

    if (j.contains("quaternion")) {
        transform_.quaternion = { j["quaternion"][0], j["quaternion"][1], j["quaternion"][2], j["quaternion"][3] };
        transform_.isQuaternionMaster = true;
        if (j.contains("rotate")) transform_.rotate = { j["rotate"][0], j["rotate"][1], j["rotate"][2] };
    }
    else if (j.contains("rotate")) {
        transform_.rotate = { j["rotate"][0], j["rotate"][1], j["rotate"][2] };
        transform_.isQuaternionMaster = false;
    }
    transform_.UpdateMatrix();

    // 3. Collider ＆ 衝突属性
    if (j.contains("collider") && collider_) {
        const auto& col = j["collider"];
        ColliderConfig config = collider_->GetConfig();
        if (col.contains("type")) config.type = static_cast<ColliderType>(col["type"]);
        if (col.contains("size")) config.size = { col["size"][0], col["size"][1], col["size"][2] };
        if (col.contains("center")) config.center = { col["center"][0], col["center"][1], col["center"][2] };
        if (col.contains("rotation")) config.rotation = { col["rotation"][0], col["rotation"][1], col["rotation"][2] };
        collider_->SetConfig(config);
    }
    if (j.contains("collisionAttribute")) SetCollisionAttribute(j["collisionAttribute"]);
    if (j.contains("collisionMask")) SetCollisionMask(j["collisionMask"]);

    // 4. イベント関連
    if (j.contains("eventType")) eventType_ = static_cast<EventType>(j["eventType"]);
     if (j.contains("targetID")) SetTargetID(j["targetID"]);
     if (j.contains("myEventID")) SetEventID(j["myEventID"]);

    // 5. Stats (Param)
    if (j.contains("param")) {
        EntityParameter p;
        const auto& jp = j["param"];
        if (jp.contains("hp")) p.hp = jp["hp"];
        if (jp.contains("maxHp")) p.maxHp = jp["maxHp"];
        if (jp.contains("speed")) p.speed = jp["speed"];
        if (jp.contains("gravity")) p.gravity = jp["gravity"];
        if (jp.contains("jumpPower")) p.jumpPower = jp["jumpPower"];
        if (jp.contains("maxFallSpeed")) p.maxFallSpeed = jp["maxFallSpeed"];
        if (jp.contains("enemyType")) p.enemyType = jp["enemyType"];
        if (jp.contains("gimmickType")) p.gimmickType = jp["gimmickType"];
        if (jp.contains("itemType")) p.itemType = jp["itemType"];
        if (jp.contains("healAmount")) p.healAmount = jp["healAmount"];
        if (jp.contains("interval")) p.interval = jp["interval"];
        if (jp.contains("maxCount")) p.maxCount = jp["maxCount"];
        if (jp.contains("detectionRange")) p.detectionRange = jp["detectionRange"];
        if (jp.contains("colorType")) p.colorType = jp["colorType"];
        if (jp.contains("shakeDuration")) p.shakeDuration = jp["shakeDuration"];
        if (jp.contains("fallDuration")) p.fallDuration = jp["fallDuration"];
        if (jp.contains("switchMode")) p.switchMode = jp["switchMode"];
        if (jp.contains("actionMode")) p.actionMode = jp["actionMode"];
        if (jp.contains("moveAmount")) p.moveAmount = jp["moveAmount"];
        if (jp.contains("moveSpeed")) p.moveSpeed = jp["moveSpeed"];
        if (jp.contains("startActive")) p.startActive = jp["startActive"];
        if (jp.contains("returnOnOff")) p.returnOnOff = jp["returnOnOff"];
        param_ = p;
    }

    // 6. グラフィックス・マテリアル
    if (j.contains("color")) SetColor({ j["color"][0], j["color"][1], j["color"][2], j["color"][3] });
    if (j.contains("blendMode")) SetBlendMode(static_cast<BlendMode>(j["blendMode"]));
    if (j.contains("materialType")) SetMaterialType(j["materialType"]);

    // ★追加: 金属度と粗さ
    if (j.contains("metallic")) SetMetallic(j["metallic"].get<float>());
    if (j.contains("roughness")) SetRoughness(j["roughness"].get<float>());

    if (j.contains("meshEffect1")) meshEffectName1_ = j["meshEffect1"].get<std::string>();
    if (j.contains("meshEffect2")) meshEffectName2_ = j["meshEffect2"].get<std::string>();

    if (j.contains("enableNormalMap")) SetEnableNormalMap(j["enableNormalMap"]);
    if (j.contains("normalMapPath")) SetNormalMap(j["normalMapPath"]);
    if (j.contains("ormMapPath")) SetOrmMap(j["ormMapPath"]);
    if (j.contains("texturePath")) SetTexture(j["texturePath"]);
    if (j.contains("enableEnvMap")) SetEnableEnvMap(j["enableEnvMap"]);
    if (j.contains("envIntensity")) SetEnvIntensity(j["envIntensity"]);
    if (j.contains("emissive")) SetEmissive(j["emissive"].get<float>());
    if (j.contains("waterParam") && GetMaterialType() >= 8 && GetMaterialType() <= 11) {
        if (GetMeshRenderer() && GetMeshRenderer()->GetWaterParamData()) {
            auto* water = GetMeshRenderer()->GetWaterParamData();
            const auto& jw = j["waterParam"];
            if (jw.contains("waveSpeed")) water->waveSpeed = jw["waveSpeed"];
            if (jw.contains("waveHeight")) water->waveHeight = jw["waveHeight"];
            if (jw.contains("waveFrequency")) water->waveFrequency = jw["waveFrequency"];
            if (jw.contains("flowSpeedX")) water->flowSpeedX = jw["flowSpeedX"];
            if (jw.contains("flowSpeedY")) water->flowSpeedY = jw["flowSpeedY"];
            if (jw.contains("effectType")) water->effectType = jw["effectType"];
            if (jw.contains("effectScale")) water->effectScale = jw["effectScale"];
            if (jw.contains("effectSoftness")) water->effectSoftness = jw["effectSoftness"];
            if (jw.contains("effectIntensity")) water->effectIntensity = jw["effectIntensity"];
            if (jw.contains("billboardScale")) water->billboardScale = jw["billboardScale"];
        }
    }

    // 7. アニメーション
    if (j.contains("animation")) {
        const auto& anim = j["animation"];
        if (anim.contains("animName")) animName_ = anim["animName"];
        if (anim.contains("isAnimLoop")) isAnimLoop_ = anim["isAnimLoop"];
        if (anim.contains("recordPathName")) recordPathName_ = anim["recordPathName"]; // 互換性
        if (anim.contains("isAnimRelative")) isRecordRelative_ = anim["isAnimRelative"]; // 互換性
    }

    // 8. レコーダー (Ghost)
    if (j.contains("recorder")) {
        const auto& rec = j["recorder"];
        if (rec.contains("recordPathName")) recordPathName_ = rec["recordPathName"];
        if (rec.contains("isRecordLoop")) isRecordLoop_ = rec["isRecordLoop"];
        if (rec.contains("isRecordRelative")) isRecordRelative_ = rec["isRecordRelative"];
    }
    if (recorder_ && !recordPathName_.empty()) {
        bool isCinematic = (className_ == "CinematicCamera");
        recorder_->Play(recordPathName_, isRecordLoop_, isRecordRelative_, isCinematic);
    }

    // 9. ローカルフォグ
    if (j.contains("localFog")) {
        if (auto* fogData = GetLocalFogData()) {
            const auto& jf = j["localFog"];
            if (jf.contains("color")) fogData->fogColor = { jf["color"][0], jf["color"][1], jf["color"][2], jf["color"][3] };
            if (jf.contains("density")) fogData->fogDensity = jf["density"];
            if (jf.contains("edgeFade")) fogData->edgeFade = jf["edgeFade"];
            if (jf.contains("noiseSpeed")) fogData->noiseSpeed = jf["noiseSpeed"];
            if (jf.contains("noiseScale")) fogData->noiseScale = jf["noiseScale"];
            if (jf.contains("scatteringG")) fogData->scatteringG = jf["scatteringG"];
            if (jf.contains("scatteringIntensity")) fogData->scatteringIntensity = jf["scatteringIntensity"];
        }
    }
}
void Object3d::UpdateAttachedEffects(float deltaTime) {
    // =================================================================
    // ★ 追加: エフェクト用のアンカーを更新（エディタと同じ挙動を再現）
    // 親のスケールやX,Z回転を無視し、座標と大元のY軸回転だけを反映する
    // =================================================================
    effectAnchor_.translate = GetWorldPosition();
    effectAnchor_.scale = { 1.0f, 1.0f, 1.0f };

    // ルート（大元）のY軸回転を取得
    float rootRotY = transform_.rotate.y;
    Object3d* rootObj = this;
    while (rootObj && rootObj->GetParent()) {
        rootObj = rootObj->GetParent();
    }
    if (rootObj) {
        rootRotY = rootObj->GetRotation().y;
    }
    effectAnchor_.rotate = { 0.0f, rootRotY, 0.0f };
    effectAnchor_.UpdateMatrix();

    // ========================================================
    // --- スロット1 ---
    // ========================================================
    if (!meshEffectName1_.empty()) {
        if (attachedEffects1_.empty() || currentMeshEffect1_ != meshEffectName1_) {
            attachedEffects1_.clear();
            currentMeshEffect1_ = "";

            std::ifstream file(meshEffectName1_);
            if (file.is_open()) {
                json j; file >> j; file.close();

                int volumeMode = j.contains("VolumeMode") ? (int)j["VolumeMode"] : 0;
                int numSpawns = (volumeMode == 2) ? 3 : (volumeMode == 1 ? 2 : 1);

                for (int i = 0; i < numSpawns; ++i) {
                    auto effect = std::make_unique<EffectObject3d>();
                    effect->Initialize(common_);
                    effect->SetName(meshEffectName1_ + "_" + std::to_string(i));

                    if (effect->LoadFromJson(meshEffectName1_)) {
                        Vector3 localRot = effect->GetRotation();
                        Vector3 localPos = effect->GetTranslate();

                        if (volumeMode == 1 && i == 1) {
                            localRot.x += 1.570796f; // 90度クロス
                        }
                        else if (volumeMode == 2) {
                            float gap = 0.02f;
                            Vector3 localZ;
                            localZ.x = sinf(localRot.y) * cosf(localRot.x);
                            localZ.y = -sinf(localRot.x);
                            localZ.z = cosf(localRot.y) * cosf(localRot.x);

                            if (i == 1) { localPos.x += localZ.x * gap; localPos.y += localZ.y * gap; localPos.z += localZ.z * gap; }
                            if (i == 2) { localPos.x -= localZ.x * gap; localPos.y -= localZ.y * gap; localPos.z -= localZ.z * gap; }
                        }

                        effect->SetTranslate(localPos);
                        effect->SetRotation(localRot);

                        // ★修正: 親の階層には繋がず、専用のアンカーを親にする！
                        effect->GetTransform()->parent = &effectAnchor_;

                        attachedEffects1_.push_back(std::move(effect));
                    }
                }
                currentMeshEffect1_ = meshEffectName1_;
            }
        }
    }
    else {
        attachedEffects1_.clear();
        currentMeshEffect1_ = "";
    }

    // ========================================================
    // --- スロット2 ---
    // ========================================================
    if (!meshEffectName2_.empty()) {
        if (attachedEffects2_.empty() || currentMeshEffect2_ != meshEffectName2_) {
            attachedEffects2_.clear();
            currentMeshEffect2_ = "";

            std::ifstream file(meshEffectName2_);
            if (file.is_open()) {
                json j; file >> j; file.close();

                int volumeMode = j.contains("VolumeMode") ? (int)j["VolumeMode"] : 0;
                int numSpawns = (volumeMode == 2) ? 3 : (volumeMode == 1 ? 2 : 1);

                for (int i = 0; i < numSpawns; ++i) {
                    auto effect = std::make_unique<EffectObject3d>();
                    effect->Initialize(common_);
                    effect->SetName(meshEffectName2_ + "_" + std::to_string(i));

                    if (effect->LoadFromJson(meshEffectName2_)) {
                        Vector3 localRot = effect->GetRotation();
                        Vector3 localPos = effect->GetTranslate();

                        if (volumeMode == 1 && i == 1) {
                            localRot.x += 1.570796f;
                        }
                        else if (volumeMode == 2) {
                            float gap = 0.02f;
                            Vector3 localZ;
                            localZ.x = sinf(localRot.y) * cosf(localRot.x);
                            localZ.y = -sinf(localRot.x);
                            localZ.z = cosf(localRot.y) * cosf(localRot.x);

                            if (i == 1) { localPos.x += localZ.x * gap; localPos.y += localZ.y * gap; localPos.z += localZ.z * gap; }
                            if (i == 2) { localPos.x -= localZ.x * gap; localPos.y -= localZ.y * gap; localPos.z -= localZ.z * gap; }
                        }

                        effect->SetTranslate(localPos);
                        effect->SetRotation(localRot);

                        // ★修正: 親の階層には繋がず、専用のアンカーを親にする！
                        effect->GetTransform()->parent = &effectAnchor_;

                        attachedEffects2_.push_back(std::move(effect));
                    }
                }
                currentMeshEffect2_ = meshEffectName2_;
            }
        }
    }
    else {
        attachedEffects2_.clear();
        currentMeshEffect2_ = "";
    }

    // 配列内のすべてのエフェクトを更新
    for (auto& effect : attachedEffects1_) {
        effect->Update(deltaTime);
    }
    for (auto& effect : attachedEffects2_) {
        effect->Update(deltaTime);
    }
}
void Object3d::DrawAttachedEffects(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!isVisible_) return;

    // 配列内のすべてのエフェクトを描画
    for (auto& effect : attachedEffects1_) {
        effect->Draw(pointLightResource, spotLightResource);
    }
    for (auto& effect : attachedEffects2_) {
        effect->Draw(pointLightResource, spotLightResource);
    }
}
