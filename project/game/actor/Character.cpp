#define NOMINMAX
#include "Character.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <algorithm>
#include <cmath>
#include "GhostRecorder.h"

// =================================================================
// 更新・描画処理
// =================================================================

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

    // 死亡判定
    if (param_->hp <= 0) {
        isDead = true;
    }

    characterMotor_.Move(*this, transform_.translate, velocity_, isGrounded_, deltaTime);
}

void Character::SyncCharacterMotorSettings() {
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
    settings.collisionMask = kAllSolid;
    characterMotor_.SetSettings(settings);
}

void Character::ApplyExternalImpulse(const Vector3& velocity, float duration) {
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
    if (!(attribute & kAllSolid)) {
        return;
    }

    SyncCharacterMotorSettings();
    characterMotor_.ResolveCollision(
        transform_.translate,
        velocity_,
        isGrounded_,
        info,
        attribute);
}

// =================================================================
// オブジェクト複製 (Clone)
// =================================================================

std::unique_ptr<Object3d> Character::Clone() const {
    auto newObj = std::make_unique<Character>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    // 1. 基本設定とTransformのコピー
    if (!GetModelName().empty()) {
        newObj->SetModel(GetModelName());
    }
    newObj->transform_ = transform_;
    newObj->SetName(name_);
    newObj->SetClassName(GetClassName());
    newObj->SetEnemyType(GetEnemyType());

    // 2. 衝突判定・属性のコピー
    newObj->SetColliderConfig(GetColliderConfig());
    newObj->SetCollisionAttribute(GetCollisionAttribute());
    newObj->SetCollisionMask(GetCollisionMask());

    // 3. ステータスと状態フラグのコピー
    newObj->SetEventType(eventType_);
    if (param_.has_value()) {
        newObj->param_ = param_.value();
    }
    newObj->velocity_ = velocity_;
    newObj->isGrounded_ = isGrounded_;

    newObj->characterMotor_ = characterMotor_;
    // 4. アニメーション設定のコピー
    newObj->animName_ = animName_;
    newObj->isAnimLoop_ = isAnimLoop_;

    // 5. ゴーストレコーダー(パス移動)のコピーと初期化
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
        newObj->recorder_->Play(
            newObj->GetRecordPathName(),
            newObj->IsRecordLoop(),
            newObj->IsRecordRelative(),
            isCinematic
        );
    }

    return newObj;
}
