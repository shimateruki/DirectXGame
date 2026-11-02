#define NOMINMAX
#include "Character.h"
#include "Easing.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <algorithm>
#include <cmath>
#include <typeinfo>
#include "GhostRecorder.h"

// 派生キャラクターが決めた速度へ重力と外力を加え、最後に Motor で衝突込みの移動を行います。
void Character::Update(float deltaTime) {
    engine::Object3d::Update(deltaTime);

    if (!GetParameters().has_value()) {
        return;
    }

    if (GetParameters()->hp <= 0) {
        SetDead(true);
    }

    if (motorSuspended_) {
        // 直接移動の終点へ重力を重ねると、着地や突進の補間軌道がずれる。
        return;
    }

    SyncCharacterMotorSettings();
    characterMotor_.BeginFrame(isGrounded_);
    characterMotor_.ApplyGravity(velocity_, GetParameters()->gravity, GetParameters()->maxFallSpeed, deltaTime);

    if (externalImpulseTimer_ > 0.0f) {
        const float duration = (std::max)(externalImpulseDuration_, 0.001f);
        const float remaining = std::clamp(externalImpulseTimer_ / duration, 0.0f, 1.0f);
        const float weight = Easing::SmoothStep(remaining);
        velocity_.x = engine::Math::Lerp(velocity_.x, externalImpulseVelocity_.x, weight);
        velocity_.z = engine::Math::Lerp(velocity_.z, externalImpulseVelocity_.z, weight);

        if (externalImpulseVerticalPending_) {
            velocity_.y = (std::max)(velocity_.y, externalImpulseVelocity_.y);
            externalImpulseVerticalPending_ = false;
        }

        externalImpulseTimer_ = (std::max)(0.0f, externalImpulseTimer_ - deltaTime);
    }

    engine::Vector3 position = GetTranslate();
    characterMotor_.Move(*this, position, velocity_, isGrounded_, deltaTime);
    SetTranslate(position);
}

void Character::SyncCharacterMotorSettings() {
    // ステータスの移動設定を毎フレーム Motor へ同期し、編集値を即座に反映する。
    if (!GetParameters().has_value()) {
        return;
    }

    CharacterMotorSettings settings;
    settings.continuousCollision = GetParameters()->motorContinuousCollision;
    settings.snapToGround = GetParameters()->motorSnapToGround;
    settings.maxSlopeDegrees = GetParameters()->motorMaxSlopeDegrees;
    settings.stepHeight = GetParameters()->motorStepHeight;
    settings.groundProbeDistance = GetParameters()->motorGroundProbeDistance;
    settings.skinWidth = GetParameters()->motorSkinWidth;
    settings.collisionMask = CollisionCategories::Get("ground");
    characterMotor_.SetSettings(settings);
}

void Character::ApplyExternalImpulse(const engine::Vector3& velocity, float duration) {
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
    engine::Object3d::Draw(pointLightResource, spotLightResource);
}

// =================================================================
// 衝突判定・物理挙動
// =================================================================

bool Character::OnCollision(engine::Object3d* other) {
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
    engine::Vector3 position = GetTranslate();
    characterMotor_.ResolveCollision(position, velocity_, isGrounded_, info, attribute);
    SetTranslate(position);
}

// 編集用の複製ではステータスと移動設定を引き継ぎ、経路レコーダーは新しい実体として初期化します。
std::unique_ptr<engine::Object3d> Character::Clone() const {
    if (typeid(*this) != typeid(Character)) {
        return nullptr;
    }

    auto newObj = std::make_unique<Character>();

    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon());

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
    newObj->SetEventType(GetEventType());

    if (GetParameters().has_value()) {
        newObj->SetParameters(GetParameters().value());
    }

    newObj->velocity_ = velocity_;
    newObj->isGrounded_ = isGrounded_;

    newObj->characterMotor_ = characterMotor_;

    // アニメーションの選択とループ設定を引き継ぎます。
    newObj->SetAnimationName(GetAnimationName());
    newObj->SetAnimationLoop(IsAnimationLoop());

    // 経路設定を複製し、再生器は複製先専用に作り直します。
    if (HasPathMoverComponent()) {
        newObj->EnsurePathMoverComponent();
        newObj->SetRecordPathName(GetRecordPathName());
        newObj->SetRecordLoop(IsRecordLoop());
        newObj->SetRecordRelative(IsRecordRelative());
        newObj->GetPathMoverComponent()->SetPlaybackMode(GetPathMoverComponent()->GetPlaybackMode());
        newObj->GetPathMoverComponent()->SetPhaseOffset(GetPathMoverComponent()->GetPhaseOffset());
    }

    newObj->InitializeRecorder(nullptr);

    // パス名が設定されていれば自動再生を開始
    if (!newObj->GetRecordPathName().empty() && newObj->GetRecorder()) {
        bool isCinematic = newObj->IsCameraObject();
        newObj->GetRecorder()->Play(newObj->GetRecordPathName(), newObj->IsRecordLoop(), newObj->IsRecordRelative(),
                                isCinematic);
    }

    return newObj;
}

// ゲーム設定の有無を維持し、指定した物理項目だけを更新する。
void Character::SetGravity(float gravity) {
    if (!this->GetParameters().has_value()) {
        this->ResetParameters();
    }
    this->SetParameter(&engine::EntityParameter::gravity, gravity);
}

void Character::SetMaxFallSpeed(float maxFallSpeed) {
    if (!this->GetParameters().has_value()) {
        this->ResetParameters();
    }
    this->SetParameter(&engine::EntityParameter::maxFallSpeed, maxFallSpeed);
}

void Character::SimulateMotorStep(engine::Vector3& position, float gravity, float maxFallSpeed, float deltaTime) {
    // 衝突コールバックからも同じ速度・接地状態を参照できるよう、保持値を直接渡す。
    characterMotor_.BeginFrame(isGrounded_);
    characterMotor_.ApplyGravity(velocity_, gravity, maxFallSpeed, deltaTime);
    characterMotor_.Move(*this, position, velocity_, isGrounded_, deltaTime);
}
