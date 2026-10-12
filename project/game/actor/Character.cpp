#define NOMINMAX
#include "Character.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <algorithm>
#include <cmath>
#include <typeinfo>
#include "GhostRecorder.h"

// 派生キャラクターが決めた速度へ重力と外力を加え、最後に Motor で衝突込みの移動を行います。
void Character::Update(float deltaTime) {
    Object3d::Update(deltaTime);

    if (!param_.has_value()) {
        return;
    }

    SyncCharacterMotorSettings();
    characterMotor_.BeginFrame(isGrounded_);
    characterMotor_.ApplyGravity(velocity_, param_->gravity, param_->maxFallSpeed, deltaTime);

    if (externalImpulseTimer_ > 0.0f) {
        const float duration = (std::max)(externalImpulseDuration_, 0.001f);
        const float remaining = std::clamp(externalImpulseTimer_ / duration, 0.0f, 1.0f);
        const float weight = remaining * remaining * (3.0f - 2.0f * remaining);
        velocity_.x = Math::Lerp(velocity_.x, externalImpulseVelocity_.x, weight);
        velocity_.z = Math::Lerp(velocity_.z, externalImpulseVelocity_.z, weight);
        if (externalImpulseVerticalPending_) {
            velocity_.y = (std::max)(velocity_.y, externalImpulseVelocity_.y);
            externalImpulseVerticalPending_ = false;
        }
        externalImpulseTimer_ = (std::max)(0.0f, externalImpulseTimer_ - deltaTime);
    }

    // HP の死亡状態を移動前に同期します。
    if (param_->hp <= 0) {
        isDead = true;
    }

    Vector3 position = GetTranslate();
    characterMotor_.Move(*this, position, velocity_, isGrounded_, deltaTime);
    SetTranslate(position);
}

void Character::SyncCharacterMotorSettings() {
    // ステータスの移動設定を毎フレーム Motor へ同期し、編集値を即座に反映する。
    if (!param_.has_value()) {
        return;
    }

    CharacterMotorSettings settings;
    settings.continuousCollision = param_->motorContinuousCollision;
    settings.snapToGround = param_->motorSnapToGround;
    settings.maxSlopeDegrees = param_->motorMaxSlopeDegrees;
    settings.stepHeight = param_->motorStepHeight;
    settings.groundProbeDistance = param_->motorGroundProbeDistance;
    settings.skinWidth = param_->motorSkinWidth;
    settings.collisionMask = CollisionCategories::Get("ground");
    characterMotor_.SetSettings(settings);
}

void Character::ApplyExternalImpulse(const Vector3& velocity, float duration) {
    // 連続被弾では弱い水平外力で強いノックバックを上書きせず、上方向だけ維持できる。
    const float safeDuration = (std::max)(0.01f, duration);
    if (externalImpulseTimer_ <= 0.0f) {
        externalImpulseVelocity_ = velocity;
        externalImpulseDuration_ = safeDuration;
        externalImpulseTimer_ = safeDuration;
        externalImpulseVerticalPending_ = true;
        return;
    }

    const float currentPlanarSq = externalImpulseVelocity_.x * externalImpulseVelocity_.x +
                                  externalImpulseVelocity_.z * externalImpulseVelocity_.z;
    const float nextPlanarSq = velocity.x * velocity.x + velocity.z * velocity.z;
    if (nextPlanarSq >= currentPlanarSq * 0.72f) {
        externalImpulseVelocity_ = velocity;
    } else {
        externalImpulseVelocity_.y = (std::max)(externalImpulseVelocity_.y, velocity.y);
    }
    externalImpulseDuration_ = (std::max)(externalImpulseDuration_, safeDuration);
    externalImpulseTimer_ = (std::max)(externalImpulseTimer_, safeDuration);
    externalImpulseVerticalPending_ = true;
}

void Character::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // キャラクター固有の描画はここで加えず、共通のライト付き 3D 描画経路を使う。
    Object3d::Draw(pointLightResource, spotLightResource);
}

// =================================================================
// 衝突判定・物理挙動
// =================================================================

bool Character::OnCollision(Object3d* other) {
    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }

    // 物理的な押し戻し・速度補正を適用
    ApplyPhysicsCollision(info, other->GetCollisionAttribute());

    return info.isColliding;
}

void Character::ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute) {
    // 地面や壁(ソリッド属性)以外は物理的な押し出しを行わない
    if (!(attribute & CollisionCategories::Get("ground"))) {
        return;
    }

    SyncCharacterMotorSettings();
    Vector3 position = GetTranslate();
    characterMotor_.ResolveCollision(position, velocity_, isGrounded_, info, attribute);
    SetTranslate(position);
}

// 編集用の複製ではステータスと移動設定を引き継ぎ、経路レコーダーは新しい実体として初期化します。
std::unique_ptr<Object3d> Character::Clone() const {
    if (typeid(*this) != typeid(Character)) {
        return nullptr;
    }
    auto newObj = std::make_unique<Character>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    // 表示と Transform を元の配置から複製します。
    if (!GetModelName().empty()) {
        newObj->SetModel(GetModelName());
    }
    newObj->CopyTransformStateFrom(*this);
    newObj->SetName(GetName());
    newObj->SetClassName(GetClassName());
    newObj->SetEnemyType(GetEnemyType());

    // 衝突属性は複製後も同じ接触相手を持つように引き継ぎます。
    newObj->SetColliderConfig(GetColliderConfig());
    newObj->SetCollisionAttribute(GetCollisionAttribute());
    newObj->SetCollisionMask(GetCollisionMask());

    // ステータスと現在の移動状態を引き継ぎます。
    newObj->SetEventType(eventType_);
    if (param_.has_value()) {
        newObj->param_ = param_.value();
    }
    newObj->velocity_ = velocity_;
    newObj->isGrounded_ = isGrounded_;

    newObj->characterMotor_ = characterMotor_;
    // アニメーションの選択とループ設定を引き継ぎます。
    newObj->animName_ = animName_;
    newObj->isAnimLoop_ = isAnimLoop_;

    // 経路設定を複製し、再生器は複製先専用に作り直します。
    if (HasPathMoverComponent()) {
        newObj->EnsurePathMoverComponent();
        newObj->SetRecordPathName(GetRecordPathName());
        newObj->SetRecordLoop(IsRecordLoop());
        newObj->SetRecordRelative(IsRecordRelative());
    }

    newObj->InitializeRecorder(nullptr);

    // パス名が設定されていれば自動再生を開始
    if (!newObj->GetRecordPathName().empty() && newObj->recorder_) {
        bool isCinematic = newObj->IsCameraObject();
        newObj->recorder_->Play(newObj->GetRecordPathName(), newObj->IsRecordLoop(), newObj->IsRecordRelative(),
                                isCinematic);
    }

    return newObj;
}
