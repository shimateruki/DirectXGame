#define NOMINMAX
#include "Object3d.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "SRVManager.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include <cassert>
#include <algorithm> // min, max
#include <ParticleManager.h>
#include <DebugConsole.h>

Object3d::~Object3d() {
    if (recorder_) {
        delete recorder_;
        recorder_ = nullptr;
    }
    // unique_ptr (collider_, meshRenderer_) は自動解放
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
}

// ========================================================================
// 更新・描画
// ========================================================================

void Object3d::Update(float deltaTime) {

    if (meshRenderer_ && meshRenderer_->GetModel()) {
        // アニメーション更新
        if (!animName_.empty()) {
            Model* model = meshRenderer_->GetModel();
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
            }
        
        }
        meshRenderer_->GetModel()->Update();
    }

    // レンダラー更新 (WVP行列転送など)
    if (meshRenderer_) {
        meshRenderer_->Update();
    }
    UpdateWorldMatrix();

    if (recorder_) {
        recorder_->Update();
    }
    
    UpdateParticle();

}

void Object3d::UpdateParticle() {
    // 名前が設定されていれば、マネージャー経由で発生させる
    if (!particleName_.empty()) {

        Vector3 pos = GetWorldPosition();

        ParticleManager::GetInstance()->Emit(particleName_, pos, particleTimer_);
    }
}

void Object3d::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!isVisible_) return;
#ifdef DD // "Release" ビルドの時だけ有効になるマクロ
    if (className_ == "CinematicCamera") {
        return; // 何も描画せずに帰る（門前払い）
    }
#endif
    if (meshRenderer_) {
        meshRenderer_->Draw(pointLightResource, spotLightResource);
    }
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
    if (collider_) collider_->SetConfig(config);
}
const Object3d::ColliderConfig& Object3d::GetColliderConfig() const {
    return collider_->GetConfig();
}

void Object3d::SetColliderType(ColliderType type) {
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.type = type;
    collider_->SetConfig(config);
}
ColliderType Object3d::GetColliderType() const {
    return collider_ ? collider_->GetType() : ColliderType::kNone;
}

void Object3d::SetCollisionSize(const Vector3& size) {
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.size = size;
    collider_->SetConfig(config);
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
    if (collider_) collider_->SetAttribute(attribute);
}
uint32_t Object3d::GetCollisionAttribute() const {
    return collider_ ? collider_->GetAttribute() : 0;
}

void Object3d::SetCollisionMask(uint32_t mask) {
    if (collider_) collider_->SetMask(mask);
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

CollisionInfo Object3d::CheckCollision(Object3d* other) {
    if (!collider_ || !other || !other->GetCollider()) {
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
        d["param"]["gravity"] = p.gravity;
        d["param"]["jumpPower"] = p.jumpPower;
        d["param"]["maxFallSpeed"] = p.maxFallSpeed;
        d["param"]["enemyType"] = p.enemyType;
        d["param"]["interval"] = p.interval;
        d["param"]["maxCount"] = p.maxCount;
    }

    // 6. グラフィックス・マテリアル
    Vector4 col = GetColor();
    d["color"] = { col.x, col.y, col.z, col.w };
    d["blendMode"] = static_cast<int>(GetBlendMode());
    d["materialType"] = GetMaterialType();

    // ★追加: 金属度と粗さ
    d["metallic"] = GetMetallic();
    d["roughness"] = GetRoughness();

    d["enableNormalMap"] = GetEnableNormalMap();
    d["normalMapPath"] = GetNormalMapPath();
    d["ormMapPath"] = GetOrmMapPath();
    d["texturePath"] = GetTexturePath();
    d["enableEnvMap"] = GetEnableEnvMap();
    d["envIntensity"] = GetEnvIntensity();
    d["emissive"] = GetEmissive();
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
        if (jp.contains("interval")) p.interval = jp["interval"];
        if (jp.contains("maxCount")) p.maxCount = jp["maxCount"];
        param_ = p;
    }

    // 6. グラフィックス・マテリアル
    if (j.contains("color")) SetColor({ j["color"][0], j["color"][1], j["color"][2], j["color"][3] });
    if (j.contains("blendMode")) SetBlendMode(static_cast<BlendMode>(j["blendMode"]));
    if (j.contains("materialType")) SetMaterialType(j["materialType"]);

    // ★追加: 金属度と粗さ
    if (j.contains("metallic")) SetMetallic(j["metallic"].get<float>());
    if (j.contains("roughness")) SetRoughness(j["roughness"].get<float>());

    if (j.contains("enableNormalMap")) SetEnableNormalMap(j["enableNormalMap"]);
    if (j.contains("normalMapPath")) SetNormalMap(j["normalMapPath"]);
    if (j.contains("ormMapPath")) SetOrmMap(j["ormMapPath"]);
    if (j.contains("texturePath")) SetTexture(j["texturePath"]);
    if (j.contains("enableEnvMap")) SetEnableEnvMap(j["enableEnvMap"]);
    if (j.contains("envIntensity")) SetEnvIntensity(j["envIntensity"]);
    if (j.contains("emissive")) SetEmissive(j["emissive"].get<float>());

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