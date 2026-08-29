#define NOMINMAX
#include "Character.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <algorithm>
#include <cmath>
#include "GhostRecorder.h"

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

    // HPは汎用EntityParameterに残しているため、派生クラスを問わず死亡状態だけを同期します。
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

bool Character::OnCollision(Object3d* other) {
    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }

    ApplyPhysicsCollision(info, other->GetCollisionAttribute());

    return info.isColliding;
}

void Character::ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute) {
    // トリガーや攻撃判定はイベント用途なので、位置を押し戻しません。
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

std::unique_ptr<Object3d> Character::Clone() const {
    auto newObj = std::make_unique<Character>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    // Object3dの基本設定に加え、Character固有の運動状態も複製します。
    if (!GetModelName().empty()) {
        newObj->SetModel(GetModelName());
    }
    newObj->transform_ = transform_;
    newObj->SetName(name_);
    newObj->SetClassName(GetClassName());
    newObj->SetEnemyType(GetEnemyType());

    newObj->SetColliderConfig(GetColliderConfig());
    newObj->SetCollisionAttribute(GetCollisionAttribute());
    newObj->SetCollisionMask(GetCollisionMask());

    newObj->SetEventType(eventType_);
    if (param_.has_value()) {
        newObj->param_ = param_.value();
    }
    newObj->velocity_ = velocity_;
    newObj->isGrounded_ = isGrounded_;

    newObj->characterMotor_ = characterMotor_;
    newObj->animName_ = animName_;
    newObj->isAnimLoop_ = isAnimLoop_;

    // PathMoverは実行中インスタンスを共有せず、設定から再構築します。
    if (HasPathMoverComponent()) {
        newObj->EnsurePathMoverComponent();
        newObj->SetRecordPathName(GetRecordPathName());
        newObj->SetRecordLoop(IsRecordLoop());
        newObj->SetRecordRelative(IsRecordRelative());
    }

    newObj->InitializeRecorder(nullptr);

    // 複製直後も元と同じパス設定で動けるよう、設定済みの場合だけ再生を開始します。
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
