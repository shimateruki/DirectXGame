#include "EnemyBeamDrone.h"
#include "CollisionConfig.h"
#include "EventManager.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kHoverHeight = 4.0f;
constexpr float kPreferredDistance = 14.0f;
constexpr float kBeamChargeTime = 0.85f;
constexpr float kBeamActiveTime = 0.35f;
constexpr float kBeamCooldown = 2.6f;
constexpr float kBeamThickness = 0.24f;
constexpr float kBeamHitRadius = 1.0f;
constexpr float kBeamDamage = 2.0f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

void EnemyBeamDrone::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_BeamDrone");
    SetEnemyType("BeamDrone");
    SetColor({ 0.12f, 0.82f, 1.0f, 1.0f });
    SetEmissive(2.0f);
    defaultColor_ = GetColor();
    SetScale({ 0.9f, 0.9f, 0.9f });

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(0.9f);

    beamVisual_ = std::make_unique<Object3d>();
    beamVisual_->Initialize(common);
    beamVisual_->SetName("Enemy_BeamDrone_Beam");
    beamVisual_->SetClassName("Effect");
    beamVisual_->SetModel("Primitives/cylinder");
    beamVisual_->SetTexture("Resources/sprite/white.png");
    beamVisual_->SetBlendMode(BlendMode::kAdd);
    beamVisual_->SetMaterialType(12);
    beamVisual_->SetColor({ 0.15f, 0.9f, 1.0f, 0.9f });
    beamVisual_->SetEmissive(8.0f);
    beamVisual_->SetScale({ 0.0f, 0.0f, 0.0f });
    beamVisual_->SetCollisionAttribute(0);
    beamVisual_->SetCollisionMask(0);
    beamVisual_->SetIsVisible(false);
}

void EnemyBeamDrone::Update(float deltaTime) {
    if (isCarried_) {
        if (beamVisual_) beamVisual_->SetIsVisible(false);
        return;
    }

    CaptureHomePosition();
    hoverTimer_ += deltaTime;
    cooldownTimer_ = (std::max)(0.0f, cooldownTimer_ - deltaTime);

    UpdateHover(deltaTime);

    Vector3 toTarget = { 0.0f, 0.0f, 1.0f };
    Vector3 direction = { 0.0f, 0.0f, 1.0f };
    float distance = 9999.0f;
    if (target_) {
        toTarget = target_->GetTranslate() - GetTranslate();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
        UpdateFacing(direction);
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
        if (chargeTimer_ <= 0.0f) {
            FireBeam();
        }
        break;
    case BeamState::Beam:
        beamTimer_ -= deltaTime;
        UpdateBeamVisual();
        UpdateBeamDamage();
        if (beamTimer_ <= 0.0f) {
            state_ = BeamState::Idle;
            cooldownTimer_ = kBeamCooldown;
            SetColor(defaultColor_);
            if (beamVisual_) beamVisual_->SetIsVisible(false);
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
    Vector3 desired = homePosition_;
    desired.y += std::sin(hoverTimer_ * 2.1f) * 0.45f;

    if (target_) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
        if (distance <= detectionRange_) {
            Vector3 targetPos = target_->GetTranslate();
            targetPos.y += kHoverHeight + std::sin(hoverTimer_ * 2.4f) * 0.35f;
            desired = targetPos - direction * kPreferredDistance;
        }
    }

    Vector3 toDesired = desired - GetTranslate();
    const float speed = param_.has_value() ? (std::max)(1.0f, param_->speed) : 5.0f;
    Vector3 velocity = toDesired * std::min(1.0f, deltaTime * speed);
    if (deltaTime > 0.001f) {
        velocity = velocity / deltaTime;
    }
    SetVelocity(velocity);
}

void EnemyBeamDrone::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, std::atan2(direction.x, direction.z), 0.16f));
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

    beamStart_ = GetTranslate();
    beamStart_.y += 0.2f;
    beamEnd_ = target_->GetTranslate();
    beamEnd_.y += 0.8f;
    state_ = BeamState::Beam;
    beamTimer_ = kBeamActiveTime;
    beamDamageDone_ = false;
    UpdateBeamVisual();
}

void EnemyBeamDrone::UpdateBeamVisual() {
    if (!beamVisual_) return;

    Vector3 diff = beamEnd_ - beamStart_;
    const float length = Math::Length(diff);
    if (length <= 0.001f) {
        beamVisual_->SetIsVisible(false);
        return;
    }

    const float pulse = 0.92f + std::sin(beamTimer_ * 80.0f) * 0.08f;
    const Vector3 midpoint = beamStart_ + diff * 0.5f;
    beamVisual_->SetTranslate(midpoint);
    beamVisual_->SetScale({ kBeamThickness * pulse, length * 0.5f, kBeamThickness * pulse });
    beamVisual_->GetTransform()->quaternion = MakeYAxisToDirectionQuaternion(diff);
    beamVisual_->GetTransform()->isQuaternionMaster = true;
    beamVisual_->SetColor({ 0.18f, 0.92f, 1.0f, 0.86f + pulse * 0.1f });
    beamVisual_->SetIsVisible(true);
    beamVisual_->UpdateWorldMatrix();
}

void EnemyBeamDrone::UpdateBeamDamage() {
    if (beamDamageDone_ || !target_) return;

    if (CalcDistancePointToSegment(target_->GetTranslate(), beamStart_, beamEnd_) > kBeamHitRadius) {
        return;
    }

    Vector3 knockback = NormalizePlanar(target_->GetTranslate() - beamStart_);
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = kBeamDamage;
    damageEvent.knockbackVelocity = { knockback.x * 14.0f, 7.0f, knockback.z * 14.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);
    beamDamageDone_ = true;
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

Quaternion EnemyBeamDrone::MakeYAxisToDirectionQuaternion(const Vector3& direction) const {
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
