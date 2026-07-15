#include "EnemyGiantSlime.h"
#include "SlimeBounceAnimator.h"
#include "BaseScene.h"
#include "CollisionConfig.h"
#include "DebugConsole.h"
#include "EnemyFactory.h"
#include "EventManager.h"
#include "HitEffectDirector.h"
#include "MeshEffectManager.h"
#include "ParticleSystem.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

namespace {
// 大型スライムの着地衝撃波とフック分裂の調整値
constexpr float kShockwaveRadius = 7.0f;
constexpr float kShockwaveDamage = 2.0f;
constexpr float kHookSplitDuration = 1.85f;
constexpr float kHookSplitWeakDuration = 1.05f;
constexpr float kJumpChargeDuration = 0.78f;
constexpr float kLandingRecoveryDuration = 1.15f;
constexpr float kTelegraphInterval = 0.18f;
constexpr float kMinJumpDistance = 4.0f;
constexpr float kMaxJumpDistance = 18.0f;
constexpr float kHeavyHopDefaultJumpPower = 18.5f;
constexpr float kHeavyHopMinJumpPower = 14.0f;
constexpr float kHeavyHopMaxJumpPower = 21.0f;
constexpr float kHeavyHopMinSpeed = 10.0f;
constexpr float kHeavyHopMaxSpeed = 34.0f;
constexpr float kGroundCollisionWorldRadius = 1.15f;
constexpr float kThrownCollisionWorldRadius = 1.45f;
constexpr float kGroundCollisionWorldCenterY = 0.58f;
constexpr float kThrownCollisionWorldCenterY = 0.35f;
constexpr int kSplitSlimeCount = 4;
constexpr float kSplitSlimeRadius = 3.1f;
constexpr float kGiantSlimeModelYawOffset = 3.1415926535f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

// 大型スライムの初期化
void EnemyGiantSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_GiantSlime");
    SetEnemyType("GiantSlime");
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    defaultColor_ = GetColor();
    SetScale({ 2.0f, 2.0f, 2.0f });
    SetRotationY(kGiantSlimeModelYawOffset);

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
}

// ジャンプ攻撃、徘徊ジャンプ、着地衝撃波の更新
void EnemyGiantSlime::Update(float deltaTime) {
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    UpdateTimers(deltaTime);

    if (state_ == State::Airborne && isGrounded_ && GetVelocity().y <= 0.5f) {
        BeginLandingRecovery();
    }

    Vector3 direction = { 0.0f, 0.0f, 1.0f };
    float distance = 9999.0f;
    UpdateTargetFacing(direction, distance);
    if (state_ == State::Idle && UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        SetVelocity({ 0.0f, GetVelocity().y, 0.0f });
        ApplySlimeAnimation(deltaTime);
        SyncGroundCollisionRadius();
        BaseEnemy::Update(deltaTime);
        return;
    }
    UpdateStateMachine(deltaTime, direction, distance);

    SyncGroundCollisionRadius();
    BaseEnemy::Update(deltaTime);
}

bool EnemyGiantSlime::UpdateInactiveState(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (isCarried_) {
        return true;
    }

    EnsureBaseScale();
    if (UpdateHookSplitState(deltaTime)) {
        return true;
    }
    return UpdateThrowRecoveryState(deltaTime);
}

bool EnemyGiantSlime::UpdateHookSplitState(float deltaTime) {
    if (!isHookSplitPulled_) {
        return false;
    }
    idleTimer_ += deltaTime;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SyncGroundCollisionRadius();
    return true;
}

bool EnemyGiantSlime::UpdateThrowRecoveryState(float deltaTime) {
    if (!IsThrowRecovering()) {
        return false;
    }
    if (IsThrownPhysics()) {
        SyncThrownCollisionRadius();
    }
    else {
        SyncGroundCollisionRadius();
    }
    BaseEnemy::Update(deltaTime);
    return true;
}

void EnemyGiantSlime::EnsureBaseScale() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyGiantSlime::UpdateTimers(float deltaTime) {
    idleTimer_ += deltaTime;
    if (landingPulseTimer_ > 0.0f) {
        landingPulseTimer_ = (std::max)(0.0f, landingPulseTimer_ - deltaTime);
    }
}

void EnemyGiantSlime::UpdateTargetFacing(Vector3& direction, float& distance) {
    if (target_) {
        Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        if (distance <= detectionRange_) {
            const float targetYaw = std::atan2(direction.x, direction.z) + kGiantSlimeModelYawOffset;
            SetRotation({ 0.0f, Math::LerpShortAngle(GetRotation().y, targetYaw, 0.08f), 0.0f });
        }
    }
}

void EnemyGiantSlime::UpdateStateMachine(float deltaTime, const Vector3& direction, float distance) {
    switch (state_) {
    case State::ChargeJump:
        UpdateJumpCharge(deltaTime);
        break;
    case State::Recovery:
        UpdateLandingRecovery(deltaTime);
        break;
    case State::Airborne:
        ShowAttackTelegraphCircle(
            HitEffectDirector::ResolveGroundEffectPosition(landingTelegraphPosition_),
            kShockwaveRadius,
            1.0f,
            { 1.0f, 0.36f, 0.82f, 0.82f });
        ApplySlimeAnimation(deltaTime);
        break;
    case State::Idle:
    default:
        UpdateIdleState(deltaTime, direction, distance);
        break;
    }
}

void EnemyGiantSlime::UpdateIdleState(float deltaTime, const Vector3& direction, float distance) {
    if (isGrounded_) {
        Vector3 velocity = GetVelocity();
        velocity.x *= 0.65f;
        velocity.z *= 0.65f;
        SetVelocity(velocity);

        if (distance <= detectionRange_) {
            jumpTimer_ += deltaTime;
            if (jumpTimer_ >= 1.15f) {
                BeginJumpCharge(direction, distance);
            }
        }
        else {
            jumpTimer_ += deltaTime * 0.65f;
            Vector3 wanderVelocity = CalculateWanderVelocity(deltaTime, 1.25f, 0.55f);
            Vector3 wanderDirection = { wanderVelocity.x, 0.0f, wanderVelocity.z };
            const float wanderLength = Math::Length(wanderDirection);
            if (wanderLength > 0.001f) {
                wanderDirection = wanderDirection / wanderLength;
                const float targetYaw = std::atan2(wanderDirection.x, wanderDirection.z) + kGiantSlimeModelYawOffset;
                SetRotation({ 0.0f, Math::LerpShortAngle(GetRotation().y, targetYaw, 0.05f), 0.0f });
            }

            if (jumpTimer_ >= 1.75f && wanderLength > 0.05f) {
                SetVelocity({ wanderDirection.x * 4.6f, kHeavyHopDefaultJumpPower * 0.55f, wanderDirection.z * 4.6f });
                jumpTimer_ = 0.0f;
                isGrounded_ = false;
            }
        }
    }
    ApplySlimeAnimation(deltaTime);
}

void EnemyGiantSlime::BeginThrown(const Vector3& initialVelocity) {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }

    state_ = State::Idle;
    stateTimer_ = 0.0f;
    telegraphTimer_ = 0.0f;
    landingPulseTimer_ = 0.0f;
    isJumpingAttack_ = false;
    HideAttackTelegraph();
    SetColor(defaultColor_);
    SetScale(baseScale_);
    SyncThrownCollisionRadius();
    BaseEnemy::BeginThrown(initialVelocity);
}

std::unique_ptr<Object3d> EnemyGiantSlime::Clone() const {
    auto clone = std::make_unique<EnemyGiantSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyGiantSlime::BeginJumpCharge(const Vector3& direction, float distance) {
    state_ = State::ChargeJump;
    stateTimer_ = 0.0f;
    telegraphTimer_ = kTelegraphInterval;
    jumpTimer_ = 0.0f;
    isJumpingAttack_ = false;
    SetVelocity({ 0.0f, 0.0f, 0.0f });

    SetRotation({ 0.0f, GetRotation().y, 0.0f });

    const float jumpDistance = std::clamp(distance * 0.95f, kMinJumpDistance, kMaxJumpDistance);
    landingTelegraphPosition_ = GetTranslate() + direction * jumpDistance;
    landingTelegraphPosition_.y = GetTranslate().y;
    SpawnLandingTelegraph();
    SetColor({ 1.0f, 0.62f, 0.86f, 1.0f });
}

void EnemyGiantSlime::UpdateJumpCharge(float deltaTime) {
    stateTimer_ += deltaTime;
    telegraphTimer_ += deltaTime;
    SetVelocity({ 0.0f, 0.0f, 0.0f });

    if (target_) {
        Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        toTarget.y = 0.0f;
        const float targetDistance = Math::Length(toTarget);
        if (targetDistance > 0.001f) {
            Vector3 direction = toTarget / targetDistance;
            const float jumpDistance = std::clamp(targetDistance * 0.95f, kMinJumpDistance, kMaxJumpDistance);
            landingTelegraphPosition_ = GetTranslate() + direction * jumpDistance;
            landingTelegraphPosition_.y = GetTranslate().y;
            const float targetYaw = std::atan2(direction.x, direction.z) + kGiantSlimeModelYawOffset;
            SetRotation({ 0.0f, Math::LerpShortAngle(GetRotation().y, targetYaw, 0.12f), 0.0f });
        }
    }

    if (telegraphTimer_ >= kTelegraphInterval) {
        SpawnLandingTelegraph();
        telegraphTimer_ = 0.0f;
    }

    const float t = std::clamp(stateTimer_ / kJumpChargeDuration, 0.0f, 1.0f);
    ShowAttackTelegraphCircle(
        HitEffectDirector::ResolveGroundEffectPosition(landingTelegraphPosition_),
        kShockwaveRadius,
        t,
        { 1.0f, 0.36f, 0.82f, 0.82f });
    const float pulse = std::sin(t * 3.14159265f * 5.0f) * 0.05f;
    SetScale({
        baseScale_.x * (1.0f + t * 0.34f + pulse),
        baseScale_.y * (1.0f - t * 0.34f),
        baseScale_.z * (1.0f + t * 0.34f - pulse)
    });

    if (stateTimer_ >= kJumpChargeDuration) {
        Vector3 toLanding = landingTelegraphPosition_ - GetTranslate();
        toLanding.y = 0.0f;
        const float landingDistance = Math::Length(toLanding);
        Vector3 jumpDirection = landingDistance > 0.001f ? toLanding / landingDistance : Vector3{ 0.0f, 0.0f, 1.0f };
        LaunchJump(jumpDirection, landingDistance);
    }
}

void EnemyGiantSlime::BeginLandingRecovery() {
    state_ = State::Recovery;
    stateTimer_ = 0.0f;
    isJumpingAttack_ = false;
    landingPulseTimer_ = 0.35f;
    TriggerAttackTelegraphCue({ 1.0f, 0.08f, 0.05f, 1.0f });
    HideAttackTelegraph();
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetColor({ 1.0f, 0.44f, 0.78f, 1.0f });
    DispatchLandingShockwave();
    SpawnLandingEffects();
}

void EnemyGiantSlime::UpdateLandingRecovery(float deltaTime) {
    stateTimer_ += deltaTime;
    SetVelocity({ 0.0f, 0.0f, 0.0f });

    const float t = std::clamp(stateTimer_ / kLandingRecoveryDuration, 0.0f, 1.0f);
    const float wobble = std::sin(stateTimer_ * 28.0f) * (1.0f - t) * 0.06f;
    SetScale({
        baseScale_.x * (1.24f - t * 0.24f + wobble),
        baseScale_.y * (0.58f + t * 0.42f),
        baseScale_.z * (1.24f - t * 0.24f - wobble)
    });

    if (stateTimer_ >= kLandingRecoveryDuration) {
        state_ = State::Idle;
        stateTimer_ = 0.0f;
        jumpTimer_ = 0.0f;
        SetColor(defaultColor_);
        SetScale(baseScale_);
    }
}

void EnemyGiantSlime::SpawnLandingTelegraph() {
    Vector3 effectPos = HitEffectDirector::ResolveGroundEffectPosition(landingTelegraphPosition_);
    effectPos.y += 0.04f;
    if (auto* meshEffect = MeshEffectManager::GetInstance()) {
        meshEffect->SpawnRingWaveEffect(effectPos);
    }
}

void EnemyGiantSlime::SpawnLandingEffects() {
    Vector3 effectPos = HitEffectDirector::ResolveGroundEffectPosition(GetTranslate());
    effectPos.y += 0.04f;
    HitEffectDirector::SpawnThrowSlamShockwave(effectPos, 24.0f);

    ParticleSystem* particleSystem = nullptr;
    if (BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr) {
        particleSystem = scene->GetParticleSystem();
    }
    if (particleSystem) {
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        particleSystem->SpawnParticles(
            effectPos,
            42,
            4.5f,
            &up,
            42.0f,
            { 1.0f, 0.55f, 0.84f, 1.0f },
            { 1.0f, 0.86f, 0.95f, 0.0f },
            0.18f,
            0.48f,
            0.65f,
            0.08f);
    }
}

// フックで引っ張られて分裂する特殊処理
void EnemyGiantSlime::BeginHookSplitPull(const Vector3& hookOwnerPos) {
    (void)hookOwnerPos;
    if (hasSplit_) return;

    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }

    isHookSplitPulled_ = true;
    HideAttackTelegraph();
    hookSplitWeakPoint_ = state_ == State::Recovery;
    hookSplitPullTimer_ = 0.0f;
    hookSplitBasePosition_ = GetTranslate();
    hookSplitBaseScale_ = GetScale();
    state_ = State::Idle;
    stateTimer_ = 0.0f;
    isJumpingAttack_ = false;
    jumpTimer_ = 0.0f;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetColor(hookSplitWeakPoint_ ? Vector4{ 1.0f, 0.48f, 0.86f, 1.0f } : Vector4{ 0.42f, 0.88f, 1.0f, 1.0f });
}

bool EnemyGiantSlime::UpdateHookSplitPull(float deltaTime, const Vector3& hookOwnerPos, ParticleSystem* particleSystem) {
    if (hasSplit_) {
        return true;
    }
    if (!isHookSplitPulled_) {
        BeginHookSplitPull(hookOwnerPos);
    }

    hookSplitPullTimer_ += deltaTime;
    const float progress = GetHookSplitProgress();

    Vector3 toOwner = hookOwnerPos - hookSplitBasePosition_;
    toOwner.y = 0.0f;
    float distance = Math::Length(toOwner);
    Vector3 dir = distance > 0.001f ? toOwner / distance : Vector3{ 0.0f, 0.0f, 1.0f };

    const float pullEase = progress * progress * (3.0f - 2.0f * progress);
    const float tugDistance = std::min(distance * 0.42f, 7.2f);
    const float tugPulse = std::sin(hookSplitPullTimer_ * 34.0f) * (1.0f - progress) * 0.48f;
    const Vector3 side = { -dir.z, 0.0f, dir.x };
    const float shakePower = (1.0f - progress * 0.25f) * 0.42f;
    const float shakeSide = std::sin(hookSplitPullTimer_ * 46.0f) * shakePower;
    const float shakeY = std::sin(hookSplitPullTimer_ * 31.0f) * shakePower * 0.38f;
    Vector3 pulledPos = hookSplitBasePosition_ + dir * (tugDistance * (0.28f + pullEase * 0.95f) + tugPulse);
    pulledPos += side * shakeSide;
    pulledPos.y += shakeY;
    SetTranslate(pulledPos);

    const float pulse = std::abs(std::sin(hookSplitPullTimer_ * 22.0f));
    const float strain = 0.28f + progress * 0.72f + pulse * (1.0f - progress) * 0.16f;
    const float stretch = std::sin(hookSplitPullTimer_ * 28.0f) * (0.10f + progress * 0.08f);
    SetScale({
        hookSplitBaseScale_.x * (1.0f + strain * 0.75f + stretch),
        hookSplitBaseScale_.y * (std::max)(0.42f, 1.0f - strain * 0.58f),
        hookSplitBaseScale_.z * (1.0f + strain * 0.48f - stretch)
    });

    Vector3 rot = GetRotation();
    rot.x = std::sin(hookSplitPullTimer_ * 22.0f) * (0.18f + progress * 0.38f);
    rot.z = std::cos(hookSplitPullTimer_ * 19.0f) * (0.18f + progress * 0.38f);
    rot.y += std::sin(hookSplitPullTimer_ * 16.0f) * deltaTime * (1.0f + progress * 3.0f);
    SetRotation(rot);
    SetColor({
        0.42f + progress * 0.58f,
        0.88f - progress * 0.32f + pulse * 0.12f,
        1.0f - progress * 0.12f,
        1.0f
    });
    UpdateLocalMatrix();
    UpdateWorldMatrix();

    if (progress >= 1.0f) {
        SplitIntoSmallSlimes(particleSystem);
        return true;
    }
    return false;
}

void EnemyGiantSlime::CancelHookSplitPull() {
    if (hasSplit_) return;
    isHookSplitPulled_ = false;
    HideAttackTelegraph();
    hookSplitWeakPoint_ = false;
    hookSplitPullTimer_ = 0.0f;
    state_ = State::Idle;
    stateTimer_ = 0.0f;
    SetColor(defaultColor_);
    SetScale(hookSplitBaseScale_);
    Vector3 rot = GetRotation();
    rot.x = 0.0f;
    rot.z = 0.0f;
    SetRotation(rot);
}

float EnemyGiantSlime::GetHookSplitProgress() const {
    const float duration = hookSplitWeakPoint_ ? kHookSplitWeakDuration : kHookSplitDuration;
    return std::clamp(hookSplitPullTimer_ / duration, 0.0f, 1.0f);
}

// ジャンプ攻撃、着地衝撃波、伸縮の補助処理
void EnemyGiantSlime::LaunchJump(const Vector3& direction, float distance) {
    const float gravity = param_.has_value() ? (std::max)(1.0f, param_->gravity) : 70.0f;
    const float configuredJumpPower = param_.has_value() && param_->jumpPower > 0.0f ? param_->jumpPower : kHeavyHopDefaultJumpPower;
    const float jumpPower = std::clamp(configuredJumpPower, kHeavyHopMinJumpPower, kHeavyHopMaxJumpPower);
    const float flightTime = std::clamp((jumpPower * 2.0f) / gravity, 0.42f, 0.68f);
    const float horizontalSpeed = std::clamp(distance / flightTime, kHeavyHopMinSpeed, kHeavyHopMaxSpeed);
    SetVelocity({ direction.x * horizontalSpeed, jumpPower, direction.z * horizontalSpeed });
    state_ = State::Airborne;
    stateTimer_ = 0.0f;
    isJumpingAttack_ = true;
    isGrounded_ = false;
    SetColor(defaultColor_);
}

void EnemyGiantSlime::DispatchLandingShockwave() {
    if (!target_) return;

    Vector3 diff = target_->GetTranslate() - GetTranslate();
    diff.y = 0.0f;
    const float distance = Math::Length(diff);
    if (distance > kShockwaveRadius) return;

    Vector3 direction = distance > 0.001f ? diff / distance : Vector3{ 0.0f, 0.0f, 1.0f };
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = kShockwaveDamage;
    damageEvent.knockbackVelocity = { direction.x * 18.0f, 13.0f, direction.z * 18.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);
}

void EnemyGiantSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    if (landingPulseTimer_ > 0.0f) {
        const float t = landingPulseTimer_ / 0.35f;
        targetScale.x = baseScale_.x * (1.35f - t * 0.25f);
        targetScale.y = baseScale_.y * (0.62f + t * 0.25f);
        targetScale.z = baseScale_.z * (1.35f - t * 0.25f);
    } else if (!isGrounded_) {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 6.0f;
        params.idleAmplitude = 0.045f;
        params.moveAmplitude = 0.18f;
        params.hopFrequency = 6.8f;
        params.horizontalSquash = 0.20f;
        params.verticalStretch = 0.26f;
        params.airborneStretch = 0.30f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, false, params);
    } else if (jumpTimer_ > 0.95f) {
        const float chargeRate = std::clamp((jumpTimer_ - 0.95f) / 0.32f, 0.0f, 1.0f);
        targetScale = SlimeBounceAnimator::MakeChargeSquash(baseScale_, chargeRate, idleTimer_, 1.08f);
    } else {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 3.8f;
        params.idleAmplitude = 0.060f;
        params.moveAmplitude = 0.21f;
        params.hopFrequency = 6.6f;
        params.horizontalSquash = 0.22f;
        params.verticalStretch = 0.27f;
        params.airborneStretch = 0.28f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, true, params);
    }

    SetScale(Math::Lerp(GetScale(), targetScale, (std::min)(1.0f, deltaTime * 8.0f)));
}

void EnemyGiantSlime::SyncWorldCollisionRadius(float worldRadius, float worldCenterY) {
    const Vector3 scale = GetScale();
    const float maxScale = (std::max)({ 0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    const float yScale = (std::max)(0.001f, std::abs(scale.y));
    ColliderConfig config = GetColliderConfig();
    config.center.y = worldCenterY / yScale;
    SetColliderConfig(config);
    SetCollisionRadius(worldRadius / maxScale);
}

void EnemyGiantSlime::SyncGroundCollisionRadius() {
    SyncWorldCollisionRadius(kGroundCollisionWorldRadius, kGroundCollisionWorldCenterY);
}

void EnemyGiantSlime::SyncThrownCollisionRadius() {
    SyncWorldCollisionRadius(kThrownCollisionWorldRadius, kThrownCollisionWorldCenterY);
}

// 小型スライムへの分裂生成
void EnemyGiantSlime::SplitIntoSmallSlimes(ParticleSystem* particleSystem) {
    if (hasSplit_) return;
    hasSplit_ = true;
    isHookSplitPulled_ = false;
    HideAttackTelegraph();
    hookSplitWeakPoint_ = false;
    state_ = State::Idle;

    Vector3 splitCenter = GetTranslate();
    if (particleSystem) {
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        particleSystem->SpawnParticles(
            splitCenter,
            48,
            2.0f,
            &up,
            34.0f,
            { 0.35f, 0.85f, 1.0f, 1.0f },
            { 0.35f, 0.85f, 1.0f, 0.0f },
            0.25f,
            0.65f,
            0.85f,
            0.08f);
    }

    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (scene && scene->GetObject3dCommon()) {
        for (int i = 0; i < kSplitSlimeCount; ++i) {
            auto slime = EnemyFactory::GetInstance()->CreateEnemy("Slime", scene->GetObject3dCommon());
            if (!slime) continue;

            const float angle = (6.28318531f / static_cast<float>(kSplitSlimeCount)) * static_cast<float>(i);
            Vector3 dir = { std::sin(angle), 0.0f, std::cos(angle) };
            Vector3 pos = splitCenter + dir * kSplitSlimeRadius;
            pos.y += 0.45f;

            const float yaw = std::atan2(dir.x, dir.z) + kGiantSlimeModelYawOffset;
            slime->SetTranslate(pos);
            slime->SetRotation({ 0.0f, yaw, 0.0f });
            slime->SetScale({ 1.35f, 1.35f, 1.35f });
            slime->SetName("Enemy_GiantSplitSlime_" + std::to_string(i));
            slime->SetTarget(target_);
            slime->SetDetectionRange(18.0f);
            slime->SetVelocity({ dir.x * 4.0f, 4.0f, dir.z * 4.0f });
            if (slime->param_.has_value()) {
                slime->param_->hp = 25.0f;
                slime->param_->maxHp = 25.0f;
                slime->param_->speed = 0.22f;
                slime->param_->jumpPower = 5.0f;
                slime->param_->detectionRange = 18.0f;
            }
            scene->AddObject(std::move(slime));
        }
    }

    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    isDead = true;

    if (BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr) {
        scene->RequestRemoveObject(this);
    }
    DebugConsole::GetInstance()->AddLog("GiantSlime split into small slimes!");
}
