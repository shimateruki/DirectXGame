#define NOMINMAX
#include "EnemyBeamDrone.h"
#include "CollisionConfig.h"
#include "EventManager.h"
#include "Player.h"
#include "CollisionManager.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kHoverHeight = 4.0f;
constexpr float kPreferredDistance = 14.0f;
constexpr float kBeamChargeTime = 0.85f;
constexpr float kBeamActiveTime = 0.35f;
constexpr float kBeamCooldown = 2.6f;
constexpr float kBeamOuterThickness = 0.34f;
constexpr float kBeamCoreThickness = 0.11f;
constexpr float kBeamOvershootLength = 6.0f;
constexpr float kBeamHitRadius = 1.0f;
constexpr float kBeamDamage = 1.0f;
constexpr float kOrbitSideOffset = 4.2f;
constexpr float kSteeringResponse = 2.8f;
constexpr float kEyeModelYawOffset = 3.1415926535f;
constexpr float kEyeMuzzleForwardOffset = 0.95f;
constexpr float kEyeMuzzleHeightOffset = 0.42f;
constexpr float kBeamAimHeight = 0.8f;
constexpr float kPlayerBeamChargeTime = 0.65f;
constexpr float kPlayerBeamActiveTime = 0.32f;
constexpr float kPlayerBeamCooldown = 0.9f;
constexpr float kPlayerBeamLength = 42.0f;
constexpr float kPlayerBeamHitRadius = 1.15f;
constexpr float kPlayerBeamDamage = 35.0f;
constexpr const char* kPlayerBeamChargeEffect = "Resources/json/effect/effect_carry_eye_charge_ring.json";
constexpr const char* kPlayerBeamMuzzleEffect = "Resources/json/effect/effect_carry_eye_beam_muzzle.json";
constexpr const char* kPlayerBeamChargeSparkPreset = "carry_eye_charge_sparks";
constexpr const char* kPlayerBeamSparkPreset = "carry_eye_beam_sparks";

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

Vector3 SafeNormalize(const Vector3& value, const Vector3& fallback) {
    const float length = Math::Length(value);
    if (length <= 0.001f) {
        return fallback;
    }
    return value / length;
}

Quaternion MakeYAxisToDirectionQuaternion(const Vector3& direction) {
    Vector3 to = direction;
    if (Math::Length(to) < 0.001f) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    to = Math::Normalize(to);

    const Vector3 from = { 0.0f, 1.0f, 0.0f };
    const float dot = std::clamp(Math::Dot(from, to), -1.0f, 1.0f);
    if (dot > 0.9999f) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    if (dot < -0.9999f) {
        return { 1.0f, 0.0f, 0.0f, 0.0f };
    }

    Vector3 axis = Math::Cross(from, to);
    const float s = std::sqrt((1.0f + dot) * 2.0f);
    const float invS = 1.0f / s;
    return { axis.x * invS, axis.y * invS, axis.z * invS, s * 0.5f };
}
}

void EnemyBeamDrone::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_BeamDrone");
    SetEnemyType("BeamDrone");
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    SetEmissive(1.4f);
    defaultColor_ = GetColor();
    SetScale({ 0.85f, 0.85f, 0.85f });

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(1.1f);

    beamVisual_ = std::make_unique<Object3d>();
    beamVisual_->Initialize(common);
    beamVisual_->SetName("Enemy_BeamDrone_Beam");
    beamVisual_->SetClassName("Effect");
    beamVisual_->SetModel("Primitives/cylinder");
    beamVisual_->SetTexture("Resources/sprite/common/white.png");
    beamVisual_->SetBlendMode(BlendMode::kAdd);
    beamVisual_->SetMaterialType(12);
    beamVisual_->SetColor({ 0.15f, 0.9f, 1.0f, 0.9f });
    beamVisual_->SetEmissive(8.0f);
    beamVisual_->SetScale({ 0.0f, 0.0f, 0.0f });
    beamVisual_->SetCollisionAttribute(0);
    beamVisual_->SetCollisionMask(0);
    beamVisual_->SetIsVisible(false);

    beamCoreVisual_ = std::make_unique<Object3d>();
    beamCoreVisual_->Initialize(common);
    beamCoreVisual_->SetName("Enemy_BeamDrone_BeamCore");
    beamCoreVisual_->SetClassName("Effect");
    beamCoreVisual_->SetModel("Primitives/cylinder");
    beamCoreVisual_->SetTexture("Resources/sprite/common/white.png");
    beamCoreVisual_->SetBlendMode(BlendMode::kAdd);
    beamCoreVisual_->SetMaterialType(12);
    beamCoreVisual_->SetColor({ 0.85f, 1.0f, 1.0f, 1.0f });
    beamCoreVisual_->SetEmissive(12.0f);
    beamCoreVisual_->SetScale({ 0.0f, 0.0f, 0.0f });
    beamCoreVisual_->SetCollisionAttribute(0);
    beamCoreVisual_->SetCollisionMask(0);
    beamCoreVisual_->SetIsVisible(false);
}

void EnemyBeamDrone::Update(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        HideBeamVisuals();
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (isCarried_) {
        return;
    }
    if (IsThrowRecovering()) {
        HideBeamVisuals();
        BaseEnemy::Update(deltaTime);
        return;
    }

    CaptureHomePosition();
    hoverTimer_ += deltaTime;
    cooldownTimer_ = (std::max)(0.0f, cooldownTimer_ - deltaTime);

    if (state_ == BeamState::Beam) {
        smoothedVelocity_ = { 0.0f, 0.0f, 0.0f };
        SetVelocity({ 0.0f, 0.0f, 0.0f });
    }
    else {
        UpdateHover(deltaTime);
    }

    Vector3 toTarget = { 0.0f, 0.0f, 1.0f };
    Vector3 direction = { 0.0f, 0.0f, 1.0f };
    float distance = 9999.0f;
    if (target_) {
        toTarget = target_->GetTranslate() - GetTranslate();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    }

    switch (state_) {
    case BeamState::Idle:
        if (target_ && distance <= detectionRange_ && cooldownTimer_ <= 0.0f) {
            StartCharge();
        }
        break;
    case BeamState::Charge:
        chargeTimer_ -= deltaTime;
        SetColor({ 0.75f, 0.98f, 1.0f, 1.0f });
        if (target_) {
            UpdateFacing(direction);
        }
        if (chargeTimer_ <= 0.0f) {
            FireBeam();
        }
        break;
    case BeamState::Beam:
        beamTimer_ -= deltaTime;
        UpdateBeamVisual();
        UpdateBeamDamage();
        UpdateFacing(NormalizePlanar(beamDirection_));
        if (beamTimer_ <= 0.0f) {
            state_ = BeamState::Idle;
            cooldownTimer_ = kBeamCooldown;
            SetColor(defaultColor_);
            HideBeamVisuals();
        }
        break;
    }

    BaseEnemy::Update(deltaTime);
}

void EnemyBeamDrone::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    BaseEnemy::Draw(pointLightResource, spotLightResource);
    if (beamVisual_ && beamVisual_->GetIsVisible()) {
        beamVisual_->Draw(pointLightResource, spotLightResource);
    }
    if (beamCoreVisual_ && beamCoreVisual_->GetIsVisible()) {
        beamCoreVisual_->Draw(pointLightResource, spotLightResource);
    }
}

std::unique_ptr<Object3d> EnemyBeamDrone::Clone() const {
    auto clone = std::make_unique<EnemyBeamDrone>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyBeamDrone::SetCarried(bool isCarried) {
    BaseEnemy::SetCarried(isCarried);
    isPlayerBeamMode_ = false;
    playerBeamDamageDone_ = false;
    beamDamageDone_ = false;
    chargeTimer_ = 0.0f;
    beamTimer_ = 0.0f;
    playerBeamEffectTimer_ = 0.0f;
    state_ = BeamState::Idle;
    cooldownTimer_ = isCarried ? 0.0f : kBeamCooldown * 0.35f;
    SetColor(defaultColor_);
    SetEmissive(1.4f);
    HideBeamVisuals();
}

void EnemyBeamDrone::ExecuteAbility(Player* player) {
    if (!player || !isCarried_) {
        return;
    }
    if (state_ != BeamState::Idle || cooldownTimer_ > 0.0f) {
        return;
    }

    StartPlayerBeamCharge(player);
}

void EnemyBeamDrone::UpdateCarriedAbility(Player* player, float deltaTime) {
    if (!player || !isCarried_) {
        return;
    }

    cooldownTimer_ = (std::max)(0.0f, cooldownTimer_ - deltaTime);

    switch (state_) {
    case BeamState::Idle:
        isPlayerBeamMode_ = false;
        playerBeamDamageDone_ = false;
        SetColor(defaultColor_);
        SetEmissive(1.4f);
        HideBeamVisuals();
        break;
    case BeamState::Charge:
        chargeTimer_ -= deltaTime;
        playerBeamEffectTimer_ -= deltaTime;
        SetColor({ 0.65f, 0.95f, 1.0f, 1.0f });
        SetEmissive(3.5f + std::sin(chargeTimer_ * 36.0f) * 0.8f);
        if (playerBeamEffectTimer_ <= 0.0f) {
            const Vector3 muzzle = GetPlayerBeamMuzzlePosition(player);
            if (MeshEffectManager::GetInstance()) {
                MeshEffectManager::GetInstance()->SpawnEffectAt(
                    kPlayerBeamChargeEffect,
                    muzzle,
                    { 1.5707963f, player->GetMoveYaw(), 0.0f },
                    { 1.0f, 1.0f, 1.0f }
                );
            }
            if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
                gpuParticleManager->Emit(kPlayerBeamChargeSparkPreset, muzzle);
            }
            playerBeamEffectTimer_ = 0.13f;
        }
        if (chargeTimer_ <= 0.0f) {
            FirePlayerBeam(player);
        }
        break;
    case BeamState::Beam:
        beamTimer_ -= deltaTime;
        UpdatePlayerBeam(player, deltaTime);
        playerBeamEffectTimer_ -= deltaTime;
        if (playerBeamEffectTimer_ <= 0.0f) {
            if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
                gpuParticleManager->Emit(kPlayerBeamSparkPreset, beamStart_ + beamDirection_ * 3.0f);
                gpuParticleManager->Emit(kPlayerBeamSparkPreset, beamStart_ + beamDirection_ * 12.0f);
            }
            playerBeamEffectTimer_ = 0.08f;
        }
        if (beamTimer_ <= 0.0f) {
            state_ = BeamState::Idle;
            cooldownTimer_ = kPlayerBeamCooldown;
            isPlayerBeamMode_ = false;
            playerBeamDamageDone_ = false;
            playerBeamEffectTimer_ = 0.0f;
            SetColor(defaultColor_);
            SetEmissive(1.4f);
            HideBeamVisuals();
        }
        break;
    }
}

void EnemyBeamDrone::CaptureHomePosition() {
    if (hasHomePosition_) return;
    homePosition_ = GetTranslate();
    if (homePosition_.y < kHoverHeight) {
        homePosition_.y += kHoverHeight;
        SetTranslate(homePosition_);
    }
    hasHomePosition_ = true;
}

void EnemyBeamDrone::UpdateHover(float deltaTime) {
    Vector3 desired = GetWanderTargetPosition(deltaTime, 0.55f);
    desired.y += std::sin(hoverTimer_ * 2.1f) * 0.45f;
    bool isCombatPosition = false;

    if (target_) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
        if (distance <= detectionRange_) {
            Vector3 targetPos = target_->GetTranslate();
            const Vector3 sideDirection = { direction.z, 0.0f, -direction.x };
            const float sideOffset = std::sin(hoverTimer_ * 0.85f) * kOrbitSideOffset;
            targetPos.y += kHoverHeight + std::sin(hoverTimer_ * 2.4f) * 0.35f;
            desired = targetPos - direction * kPreferredDistance + sideDirection * sideOffset;
            isCombatPosition = true;
        }
    }

    if (!isCombatPosition) {
        const Vector3 toWander = desired - GetTranslate();
        const Vector3 wanderDirection = NormalizePlanar(toWander);
        const Vector3 sideDirection = { wanderDirection.z, 0.0f, -wanderDirection.x };
        desired = desired + sideDirection * (std::sin(hoverTimer_ * 1.15f) * 2.0f);
    }

    Vector3 toDesired = desired - GetTranslate();
    const float distanceToDesired = Math::Length(toDesired);
    const float speed = param_.has_value() ? (std::max)(1.0f, param_->speed) : 5.0f;
    Vector3 desiredVelocity = { 0.0f, 0.0f, 0.0f };
    if (distanceToDesired > 0.001f) {
        const float arrival = std::clamp(distanceToDesired / 5.0f, 0.18f, 1.0f);
        desiredVelocity = (toDesired / distanceToDesired) * speed * arrival;
    }

    const float blend = std::clamp(deltaTime * kSteeringResponse, 0.0f, 1.0f);
    smoothedVelocity_ = smoothedVelocity_ + (desiredVelocity - smoothedVelocity_) * blend;
    SetVelocity(smoothedVelocity_);

    Vector3 facingDirection = smoothedVelocity_;
    facingDirection.y = 0.0f;
    if (Math::Length(facingDirection) > 0.08f && state_ == BeamState::Idle) {
        UpdateFacing(facingDirection);
    }
}

void EnemyBeamDrone::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;

    const float targetYaw = std::atan2(direction.x, direction.z) + kEyeModelYawOffset;
    SetRotation({ 0.0f, Math::LerpShortAngle(GetRotation().y, targetYaw, 0.16f), 0.0f });
}

void EnemyBeamDrone::StartCharge() {
    state_ = BeamState::Charge;
    chargeTimer_ = kBeamChargeTime;
    beamDamageDone_ = false;
}

void EnemyBeamDrone::FireBeam() {
    if (!target_) {
        state_ = BeamState::Idle;
        cooldownTimer_ = kBeamCooldown;
        return;
    }

    Vector3 aimPoint = target_->GetTranslate();
    aimPoint.y += kBeamAimHeight;

    Vector3 roughOrigin = GetTranslate();
    roughOrigin.y += GetScale().y * kEyeMuzzleHeightOffset;
    const Vector3 roughDirection = SafeNormalize(aimPoint - roughOrigin, NormalizePlanar(aimPoint - GetTranslate()));
    beamStart_ = GetBeamMuzzlePosition(roughDirection);

    const Vector3 aimDiff = aimPoint - beamStart_;
    const float aimLength = (std::max)(0.01f, Math::Length(aimDiff));
    beamDirection_ = SafeNormalize(aimDiff, roughDirection);
    beamLength_ = aimLength + kBeamOvershootLength;
    beamEnd_ = beamStart_ + beamDirection_ * beamLength_;
    state_ = BeamState::Beam;
    beamTimer_ = kBeamActiveTime;
    beamDamageDone_ = false;
    UpdateFacing(NormalizePlanar(beamDirection_));
    UpdateBeamVisual();
}

void EnemyBeamDrone::UpdateBeamVisual() {
    if (!beamVisual_ || !beamCoreVisual_) return;

    beamEnd_ = beamStart_ + beamDirection_ * beamLength_;

    Vector3 diff = beamEnd_ - beamStart_;
    const float length = Math::Length(diff);
    if (length <= 0.001f) {
        HideBeamVisuals();
        return;
    }

    const float pulse = 0.92f + std::sin(beamTimer_ * 80.0f) * 0.08f;
    ApplyBeamVisualTransform(beamVisual_.get(), beamStart_, beamEnd_, kBeamOuterThickness * pulse, { 0.08f, 0.85f, 1.0f, 0.58f }, 8.0f);
    ApplyBeamVisualTransform(beamCoreVisual_.get(), beamStart_, beamEnd_, kBeamCoreThickness * pulse, { 0.85f, 1.0f, 1.0f, 0.98f }, 14.0f);
}

void EnemyBeamDrone::UpdateBeamDamage() {
    if (beamDamageDone_ || !target_) return;

    Vector3 damageStart = beamStart_;
    Vector3 damageEnd = beamEnd_;
    if (!GetVisibleBeamSegment(damageStart, damageEnd)) {
        return;
    }

    Vector3 targetPoint = target_->GetTranslate();
    targetPoint.y += 0.8f;
    if (CalcDistancePointToSegment(targetPoint, damageStart, damageEnd) > kBeamHitRadius) {
        return;
    }

    Vector3 knockback = NormalizePlanar(target_->GetTranslate() - damageStart);
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = kBeamDamage;
    damageEvent.knockbackVelocity = { knockback.x * 14.0f, 7.0f, knockback.z * 14.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);
    beamDamageDone_ = true;
}

void EnemyBeamDrone::HideBeamVisuals() {
    if (beamVisual_) {
        beamVisual_->SetIsVisible(false);
    }
    if (beamCoreVisual_) {
        beamCoreVisual_->SetIsVisible(false);
    }
}

void EnemyBeamDrone::ApplyBeamVisualTransform(Object3d* visual, const Vector3& source, const Vector3& target, float thickness, const Vector4& color, float emissive) {
    if (!visual) return;

    Vector3 diff = target - source;
    const float length = Math::Length(diff);
    if (length <= 0.001f) {
        visual->SetIsVisible(false);
        return;
    }

    visual->SetTranslate(source + diff * 0.5f);
    visual->SetScale({ thickness, length * 0.5f, thickness });
    visual->GetTransform()->quaternion = MakeYAxisToDirectionQuaternion(diff);
    visual->GetTransform()->isQuaternionMaster = true;
    visual->SetColor(color);
    visual->SetEmissive(emissive);
    visual->SetIsVisible(true);
    visual->UpdateWorldMatrix();
}

bool EnemyBeamDrone::GetVisibleBeamSegment(Vector3& outStart, Vector3& outEnd) const {
    if (!beamVisual_ || !beamVisual_->GetIsVisible()) {
        return false;
    }

    if (Math::Length(beamEnd_ - beamStart_) <= 0.001f) {
        return false;
    }

    outStart = beamStart_;
    outEnd = beamEnd_;
    return true;
}

Vector3 EnemyBeamDrone::GetBeamMuzzlePosition(const Vector3& direction) const {
    Vector3 forward = NormalizePlanar(direction);
    Vector3 position = GetTranslate();
    const Vector3 scale = GetScale();
    const float horizontalScale = (std::max)(0.5f, (scale.x + scale.z) * 0.5f);
    const float verticalScale = (std::max)(0.5f, scale.y);

    position += forward * (horizontalScale * kEyeMuzzleForwardOffset);
    position.y += verticalScale * kEyeMuzzleHeightOffset;
    return position;
}

void EnemyBeamDrone::StartPlayerBeamCharge(Player* player) {
    if (!player) {
        return;
    }

    isPlayerBeamMode_ = true;
    playerBeamDamageDone_ = false;
    beamDamageDone_ = false;
    state_ = BeamState::Charge;
    chargeTimer_ = kPlayerBeamChargeTime;
    beamTimer_ = 0.0f;
    playerBeamEffectTimer_ = 0.0f;
    HideBeamVisuals();
}

void EnemyBeamDrone::FirePlayerBeam(Player* player) {
    if (!player) {
        state_ = BeamState::Idle;
        HideBeamVisuals();
        return;
    }

    isPlayerBeamMode_ = true;
    playerBeamDamageDone_ = false;
    beamStart_ = GetPlayerBeamMuzzlePosition(player);
    beamDirection_ = GetPlayerBeamDirection(player);
    beamLength_ = kPlayerBeamLength;
    beamEnd_ = beamStart_ + beamDirection_ * beamLength_;
    beamTimer_ = kPlayerBeamActiveTime;
    state_ = BeamState::Beam;
    SetColor({ 0.78f, 1.0f, 1.0f, 1.0f });
    SetEmissive(5.0f);
    playerBeamEffectTimer_ = 0.0f;
    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            kPlayerBeamMuzzleEffect,
            beamStart_,
            { 1.5707963f, player->GetMoveYaw(), 0.0f },
            { 1.0f, 1.0f, 1.0f }
        );
    }
    if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
        gpuParticleManager->Emit(kPlayerBeamSparkPreset, beamStart_);
    }
    UpdatePlayerBeamVisual();
}

void EnemyBeamDrone::UpdatePlayerBeam(Player* player, float deltaTime) {
    (void)deltaTime;
    if (!player) {
        return;
    }

    beamStart_ = GetPlayerBeamMuzzlePosition(player);
    beamEnd_ = beamStart_ + beamDirection_ * beamLength_;
    UpdatePlayerBeamVisual();
    UpdatePlayerBeamDamage(player);
}

void EnemyBeamDrone::UpdatePlayerBeamVisual() {
    if (!beamVisual_ || !beamCoreVisual_) {
        return;
    }

    const float pulse = 0.96f + std::sin(beamTimer_ * 92.0f) * 0.08f;
    ApplyBeamVisualTransform(beamVisual_.get(), beamStart_, beamEnd_, 0.46f * pulse, { 0.08f, 0.55f, 1.0f, 0.62f }, 10.0f);
    ApplyBeamVisualTransform(beamCoreVisual_.get(), beamStart_, beamEnd_, 0.15f * pulse, { 0.9f, 1.0f, 1.0f, 1.0f }, 16.0f);
}

void EnemyBeamDrone::UpdatePlayerBeamDamage(Player* player) {
    if (playerBeamDamageDone_) {
        return;
    }

    Vector3 damageStart = beamStart_;
    Vector3 damageEnd = beamEnd_;
    if (!GetVisibleBeamSegment(damageStart, damageEnd)) {
        return;
    }

    CollisionManager* collisionManager = CollisionManager::GetInstance();
    if (!collisionManager) {
        return;
    }

    for (Object3d* object : collisionManager->GetObjects()) {
        if (!object || object == this || object == player || object->isDead) {
            continue;
        }
        if (!(object->GetCollisionAttribute() & kEnemy)) {
            continue;
        }

        Vector3 targetPoint = object->GetWorldPosition();
        targetPoint.y += 0.8f;
        if (CalcDistancePointToSegment(targetPoint, damageStart, damageEnd) > kPlayerBeamHitRadius) {
            continue;
        }

        DamageEvent damageEvent;
        damageEvent.target = object;
        damageEvent.attacker = player;
        damageEvent.damageAmount = kPlayerBeamDamage;
        damageEvent.knockbackVelocity = {
            beamDirection_.x * 20.0f,
            5.0f,
            beamDirection_.z * 20.0f
        };
        EventManager::GetInstance()->Dispatch(damageEvent);
    }

    playerBeamDamageDone_ = true;
}

Vector3 EnemyBeamDrone::GetPlayerBeamMuzzlePosition(Player* player) const {
    Vector3 position = player->GetTranslate();
    const Vector3 direction = GetPlayerBeamDirection(player);
    position.x += direction.x * 1.8f;
    position.y += 2.1f;
    position.z += direction.z * 1.8f;
    return position;
}

Vector3 EnemyBeamDrone::GetPlayerBeamDirection(Player* player) const {
    Vector3 direction = player->GetForwardDirection();
    const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { direction.x / length, 0.0f, direction.z / length };
}

float EnemyBeamDrone::CalcDistancePointToSegment(const Vector3& point, const Vector3& start, const Vector3& end) const {
    const Vector3 segment = end - start;
    const float lengthSq = Math::Dot(segment, segment);
    if (lengthSq <= 0.0001f) {
        return Math::Length(point - start);
    }

    float t = Math::Dot(point - start, segment) / lengthSq;
    t = std::clamp(t, 0.0f, 1.0f);
    const Vector3 closest = start + segment * t;
    return Math::Length(point - closest);
}

