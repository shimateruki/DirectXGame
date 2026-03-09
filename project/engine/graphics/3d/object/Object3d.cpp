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
    for (Object3d* child : children_) {
        child->UpdateWorldMatrix();
    }
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
#ifdef NDEBUG // "Release" ビルドの時だけ有効になるマクロ
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
    if (meshRenderer_) meshRenderer_->SetModel(model);
}

void Object3d::SetModel(const std::string& modelName) {
    if (meshRenderer_ && !modelName.empty()) {
        ModelManager::GetInstance()->LoadModel(modelName);

        meshRenderer_->SetModel(modelName);
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

void Object3d::CopyFrom(const Object3d* other) {
    if (!other) return;
    this->saveCategory_ = other->saveCategory_;
    if (!other->GetModelName().empty()) {
        this->SetModel(other->GetModelName());
    }
    this->name_ = other->name_;
    this->transform_ = other->transform_;

    // Colliderコピー
    if (collider_ && other->collider_) {
        this->SetColliderConfig(other->GetColliderConfig());
        this->SetCollisionAttribute(other->GetCollisionAttribute());
        this->SetCollisionMask(other->GetCollisionMask());
    }

    // MeshRenderer設定コピー
    if (meshRenderer_ && other->meshRenderer_) {
        this->SetBlendMode(other->GetBlendMode());
        this->SetMaterialType(other->GetMaterialType());
        this->SetColor(other->GetColor());
        this->SetNormalMap(other->GetNormalMapPath()); 
        this->SetOrmMap(other->GetOrmMapPath());
        this->SetTexture(other->GetTexturePath());
    }

    this->className_ = other->className_;
    this->isVisible_ = other->isVisible_;
    this->eventType_ = other->eventType_;
    this->enemyType_ = other->enemyType_;
    this->param_ = other->param_;

    // ★ボーンアニメーションのコピー
    this->animName_ = other->animName_;
    this->isAnimLoop_ = other->isAnimLoop_;

    // ★GhostRecorder用のコピー
    this->recordPathName_ = other->recordPathName_;
    this->isRecordLoop_ = other->isRecordLoop_;
    this->isRecordRelative_ = other->isRecordRelative_;

    this->InitializeRecorder(nullptr);
    if (!this->recordPathName_.empty() && this->recorder_) {
        bool isCinematic = (this->className_ == "CinematicCamera");
        this->recorder_->Play(this->recordPathName_, this->isRecordLoop_, this->isRecordRelative_, isCinematic);
    }
}

std::unique_ptr<Object3d> Object3d::Clone() const {
    auto newObj = std::make_unique<Object3d>();
    assert(common_ != nullptr);
    newObj->Initialize(common_);
    newObj->CopyFrom(this);
    return newObj;
}

json Object3d::ExportToJson() {
    json j;
    j["name"] = name_;
    j["modelName"] = GetModelName();

    j["scale"] = { transform_.scale.x, transform_.scale.y, transform_.scale.z };
    j["rotate"] = { transform_.rotate.x, transform_.rotate.y, transform_.rotate.z };

    if (collider_) {
        const auto& config = collider_->GetConfig();
        j["collider"] = {
            {"type", static_cast<int>(config.type)},
            {"size", { config.size.x, config.size.y, config.size.z }},
            {"center", { config.center.x, config.center.y, config.center.z }},
            { "rotation", { config.rotation.x, config.rotation.y, config.rotation.z } }
        };
    }

    // ★ボーンアニメとレコーダーのパスを分けて保存
    j["animation"] = {
        {"animName", animName_},
        {"isAnimLoop", isAnimLoop_}
    };
    j["recorder"] = {
        {"recordPathName", recordPathName_},
        {"isRecordLoop", isRecordLoop_},
        {"isRecordRelative", isRecordRelative_}
    };

    j["eventType"] = static_cast<int>(eventType_);
    j["enemyType"] = enemyType_;

    j["blendMode"] = static_cast<int>(GetBlendMode());
    j["materialType"] = GetMaterialType();
    j["ormMapPath"] = GetOrmMapPath();
    j["texturePath"] = GetTexturePath();
    j["saveCategory"] = saveCategory_;
    return j;
}
void Object3d::ImportFromJson(const json& j) {
    if (j.contains("modelName")) {
        SetModel(j["modelName"].get<std::string>());
    }

    if (j.contains("scale")) transform_.scale = { j["scale"][0], j["scale"][1], j["scale"][2] };
    if (j.contains("rotate")) transform_.rotate = { j["rotate"][0], j["rotate"][1], j["rotate"][2] };

    transform_.UpdateMatrix();

    if (j.contains("collider") && collider_) {
        const auto& col = j["collider"];
        ColliderConfig config = collider_->GetConfig();

        if (col.contains("type")) config.type = static_cast<ColliderType>(col["type"]);
        if (col.contains("size")) config.size = { col["size"][0], col["size"][1], col["size"][2] };
        if (col.contains("center")) config.center = { col["center"][0], col["center"][1], col["center"][2] };
        if (col.contains("rotation")) config.rotation = { col["rotation"][0], col["rotation"][1], col["rotation"][2] };

        collider_->SetConfig(config);
    }

    // ★ボーンアニメの読み込み
    if (j.contains("animation")) {
        const auto& anim = j["animation"];
        if (anim.contains("animName")) animName_ = anim["animName"];
        if (anim.contains("isAnimLoop")) isAnimLoop_ = anim["isAnimLoop"];
        
        // （互換性用）過去のデータにパスが含まれていた場合の救済
        if (anim.contains("recordPathName")) recordPathName_ = anim["recordPathName"];
        if (anim.contains("isAnimRelative")) isRecordRelative_ = anim["isAnimRelative"];
    }

    // ★GhostRecorderのパスデータ読み込み
    if (j.contains("recorder")) {
        const auto& rec = j["recorder"];
        if (rec.contains("recordPathName")) recordPathName_ = rec["recordPathName"];
        if (rec.contains("isRecordLoop")) isRecordLoop_ = rec["isRecordLoop"];
        if (rec.contains("isRecordRelative")) isRecordRelative_ = rec["isRecordRelative"];
    }

    // パスデータがあれば再生準備
    if (recorder_ && !recordPathName_.empty()) {
        bool isCinematic = (this->GetClassName() == "CinematicCamera");
        recorder_->Play(
            recordPathName_,
            isRecordLoop_,
            isRecordRelative_,
            isCinematic 
        );
    }

    if (j.contains("eventType")) eventType_ = static_cast<EventType>(j["eventType"]);
    if (j.contains("enemyType")) enemyType_ = j["enemyType"];

    if (j.contains("blendMode")) SetBlendMode(static_cast<BlendMode>(j["blendMode"]));
    if (j.contains("materialType")) SetMaterialType(j["materialType"]);
    if (j.contains("enableNormalMap")) SetEnableNormalMap(j["enableNormalMap"].get<bool>());
    if (j.contains("normalMapPath")) SetNormalMap(j["normalMapPath"].get<std::string>());
    if (j.contains("ormMapPath")) SetOrmMap(j["ormMapPath"].get<std::string>()); 
    if (j.contains("texturePath")) SetTexture(j["texturePath"].get<std::string>());
    if (j.contains("saveCategory")) saveCategory_ = j["saveCategory"].get<std::string>();

}

void Object3d::DrawShadow() {
    if (meshRenderer_) {
        meshRenderer_->DrawShadow();
    }
}