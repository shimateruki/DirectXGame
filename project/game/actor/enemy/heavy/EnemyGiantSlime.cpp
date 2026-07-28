#include "EnemyGiantSlime.h"
#include "SlimeBounceAnimator.h"
#include "BaseScene.h"
#include "CollisionConfig.h"
#include "DebugConsole.h"
#include "EnemyFactory.h"
#include "EventManager.h"
#include "HitEffectDirector.h"
#include "MeshRenderer.h"
#include "MeshEffectManager.h"
#include "ParticleSystem.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

namespace {
// 大型スライムの着地衝撃波とフック分裂の調整値
constexpr const char* kJumpPressAttackId = "jump_press";
constexpr float kHookSplitDuration = 1.85f;
constexpr float kHookSplitWeakDuration = 1.05f;
constexpr float kHookLandingSquashDuration = 0.24f;
constexpr float kTelegraphInterval = 0.18f;
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

float SmoothStep01(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}
}

// 大型スライムの初期化
void EnemyGiantSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_GiantSlime");
    SetEnemyType("GiantSlime");
    ReloadAttackProfile();
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

    if (hookSplitAwaitingLanding_) {
        if (isGrounded_ && GetVelocity().y <= 0.5f) {
            BeginGroundHookResistance(true);
        } else {
            Vector3 velocity = GetVelocity();
            velocity.x *= 0.88f;
            velocity.z *= 0.88f;
            SetVelocity(velocity);
            BaseEnemy::Update(deltaTime);
        }
    } else {
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        // 接地して踏ん張っている間はCharacter更新を止めるが、被弾反応の時間だけは進めます。
        UpdateDamageFeedbackTimers(deltaTime);
    }

    ApplyHookSplitVisual(deltaTime, hookSplitOwnerPosition_);
    if (hookSplitAwaitingLanding_) {
        SyncThrownCollisionRadius();
    } else {
        SyncGroundCollisionRadius();
    }
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
    const EnemyAttackDefinition& attack = GetAttackDefinition(kJumpPressAttackId);
    switch (state_) {
    case State::ChargeJump:
        UpdateJumpCharge(deltaTime);
        break;
    case State::Recovery:
        UpdateLandingRecovery(deltaTime);
        break;
    case State::Airborne:
        if (!landingWarningTriggered_ && GetVelocity().y < -0.1f) {
            const float height = (std::max)(0.0f, GetWorldPosition().y - landingTelegraphPosition_.y);
            const float fallSpeed = (std::max)(1.0f, -GetVelocity().y);
            const float estimatedTimeToLanding = height / fallSpeed;
            if (estimatedTimeToLanding <= attack.warningLeadTime || height <= 0.65f) {
                TriggerAttackTelegraphCue({ 1.0f, 0.32f, 0.78f, 1.0f });
                landingWarningTriggered_ = true;
            }
        }
        ShowAttackTelegraphCircle(
            HitEffectDirector::ResolveGroundEffectPosition(landingTelegraphPosition_),
            attack.radius,
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
    const EnemyAttackDefinition& attack = GetAttackDefinition(kJumpPressAttackId);
    if (isGrounded_) {
        Vector3 velocity = GetVelocity();
        velocity.x *= 0.65f;
        velocity.z *= 0.65f;
        SetVelocity(velocity);

        if (distance <= (std::min)(detectionRange_, attack.maxRange)) {
            jumpTimer_ += deltaTime;
            if (jumpTimer_ >= attack.cooldown) {
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
    landingWarningTriggered_ = false;
    isHookSplitPulled_ = false;
    hookSplitAwaitingLanding_ = false;
    hookSplitPhase_ = HookSplitPhase::None;
    ResetHookSplitVisual();
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
    landingWarningTriggered_ = false;
    SetVelocity({ 0.0f, 0.0f, 0.0f });

    SetRotation({ 0.0f, GetRotation().y, 0.0f });

    const EnemyAttackDefinition& attack = GetAttackDefinition(kJumpPressAttackId);
    const float jumpDistance = std::clamp(distance * 0.95f, attack.minRange, attack.maxRange);
    landingTelegraphPosition_ = GetTranslate() + direction * jumpDistance;
    landingTelegraphPosition_.y = GetTranslate().y;
    SpawnLandingTelegraph();
    SetColor({ 1.0f, 0.62f, 0.86f, 1.0f });
}

void EnemyGiantSlime::UpdateJumpCharge(float deltaTime) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kJumpPressAttackId);
    stateTimer_ += deltaTime;
    telegraphTimer_ += deltaTime;
    SetVelocity({ 0.0f, 0.0f, 0.0f });

    if (target_) {
        Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        toTarget.y = 0.0f;
        const float targetDistance = Math::Length(toTarget);
        if (targetDistance > 0.001f) {
            Vector3 direction = toTarget / targetDistance;
            const float jumpDistance = std::clamp(targetDistance * 0.95f, attack.minRange, attack.maxRange);
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

    const float windupDuration = (std::max)(0.01f, attack.windupDuration);
    const float t = std::clamp(stateTimer_ / windupDuration, 0.0f, 1.0f);
    ShowAttackTelegraphCircle(
        HitEffectDirector::ResolveGroundEffectPosition(landingTelegraphPosition_),
        attack.radius,
        t,
        { 1.0f, 0.36f, 0.82f, 0.82f });
    const float pulse = std::sin(t * 3.14159265f * 5.0f) * 0.05f;
    SetScale({
        baseScale_.x * (1.0f + t * 0.34f + pulse),
        baseScale_.y * (1.0f - t * 0.34f),
        baseScale_.z * (1.0f + t * 0.34f - pulse)
    });

    if (stateTimer_ >= windupDuration) {
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
    if (!landingWarningTriggered_) {
        TriggerAttackTelegraphCue({ 1.0f, 0.32f, 0.78f, 1.0f });
        landingWarningTriggered_ = true;
    }
    HideAttackTelegraph();
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetColor({ 1.0f, 0.44f, 0.78f, 1.0f });
    DispatchLandingShockwave();
    SpawnLandingEffects();
}

void EnemyGiantSlime::UpdateLandingRecovery(float deltaTime) {
    stateTimer_ += deltaTime;
    SetVelocity({ 0.0f, 0.0f, 0.0f });

    const float recoveryDuration = (std::max)(0.01f, GetAttackDefinition(kJumpPressAttackId).recoveryDuration);
    const float t = std::clamp(stateTimer_ / recoveryDuration, 0.0f, 1.0f);
    const float wobble = std::sin(stateTimer_ * 28.0f) * (1.0f - t) * 0.06f;
    SetScale({
        baseScale_.x * (1.24f - t * 0.24f + wobble),
        baseScale_.y * (0.58f + t * 0.42f),
        baseScale_.z * (1.24f - t * 0.24f - wobble)
    });

    if (stateTimer_ >= recoveryDuration) {
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

    const EnemyAttackDefinition& attack = GetAttackDefinition(kJumpPressAttackId);
    if (!attack.impactVfx.empty()) {
        if (auto* meshEffect = MeshEffectManager::GetInstance()) {
            meshEffect->SpawnEffectAt(attack.impactVfx, effectPos, { 0.0f, 0.0f, 0.0f }, { 2.0f, 1.0f, 2.0f });
        }
    }

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
    if (hasSplit_) return;

    EnsureBaseScale();

    isHookSplitPulled_ = true;
    HideAttackTelegraph();
    hookSplitWeakPoint_ = state_ == State::Recovery;
    hookSplitPullTimer_ = 0.0f;
    hookSplitPhaseTimer_ = 0.0f;
    hookSplitOwnerPosition_ = hookOwnerPos;
    hookSplitBasePosition_ = GetTranslate();
    hookSplitBaseScale_ = baseScale_;
    hookSplitDirection_ = NormalizePlanar(hookOwnerPos - GetTranslate());
    hookVisualScale_ = { 1.0f, 1.0f, 1.0f };
    hookVisualScaleVelocity_ = { 0.0f, 0.0f, 0.0f };
    hookVisualRotation_ = { 0.0f, 0.0f, 0.0f };
    hookVisualRotationVelocity_ = { 0.0f, 0.0f, 0.0f };
    hookVisualOffset_ = { 0.0f, 0.0f, 0.0f };
    hookVisualOffsetVelocity_ = { 0.0f, 0.0f, 0.0f };
    hookVisualShear_ = { 0.0f, 0.0f, 0.0f };
    hookVisualShearVelocity_ = { 0.0f, 0.0f, 0.0f };
    state_ = State::Idle;
    stateTimer_ = 0.0f;
    isJumpingAttack_ = false;
    jumpTimer_ = 0.0f;
    SetScale(baseScale_);

    hookSplitAwaitingLanding_ = !isGrounded_;
    if (hookSplitAwaitingLanding_) {
        hookSplitPhase_ = HookSplitPhase::AirborneTether;
        Vector3 velocity = GetVelocity();
        velocity.x *= 0.20f;
        velocity.z *= 0.20f;
        velocity.y = (std::min)(velocity.y, 1.5f);
        SetVelocity(velocity);
        SyncThrownCollisionRadius();
    } else {
        BeginGroundHookResistance(false);
    }
    SetColor(hookSplitWeakPoint_ ? Vector4{ 1.0f, 0.48f, 0.86f, 1.0f } : Vector4{ 0.42f, 0.88f, 1.0f, 1.0f });
}

bool EnemyGiantSlime::UpdateHookSplitPull(float deltaTime, const Vector3& hookOwnerPos, ParticleSystem* particleSystem) {
    if (hasSplit_) {
        return true;
    }
    if (!isHookSplitPulled_) {
        BeginHookSplitPull(hookOwnerPos);
    }

    hookSplitOwnerPosition_ = hookOwnerPos;
    Vector3 toOwner = hookOwnerPos - GetTranslate();
    const Vector3 nextDirection = NormalizePlanar(toOwner);
    if (Math::Length(nextDirection) > 0.001f) {
        hookSplitDirection_ = nextDirection;
    }

    if (hookSplitAwaitingLanding_) {
        if (isGrounded_ && GetVelocity().y <= 0.5f) {
            BeginGroundHookResistance(true);
        }
        ApplyHookSplitVisual(deltaTime, hookOwnerPos);
        return false;
    }

    if (hookSplitPhase_ == HookSplitPhase::LandingSquash) {
        hookSplitPhaseTimer_ += deltaTime;
        if (hookSplitPhaseTimer_ >= kHookLandingSquashDuration) {
            hookSplitPhase_ = HookSplitPhase::Brace;
            hookSplitPhaseTimer_ = 0.0f;
        }
    } else {
        hookSplitPullTimer_ += deltaTime;
        const float progress = GetHookSplitProgress();
        if (progress >= 0.84f) {
            hookSplitPhase_ = HookSplitPhase::Tear;
        } else if (progress >= 0.55f) {
            hookSplitPhase_ = HookSplitPhase::Slip;
        } else {
            hookSplitPhase_ = HookSplitPhase::Brace;
        }
    }

    ApplyHookSplitVisual(deltaTime, hookOwnerPos);
    const float progress = GetHookSplitProgress();

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
    hookSplitAwaitingLanding_ = false;
    hookSplitPullTimer_ = 0.0f;
    hookSplitPhaseTimer_ = 0.0f;
    hookSplitPhase_ = HookSplitPhase::None;
    state_ = State::Idle;
    stateTimer_ = 0.0f;
    SetColor(defaultColor_);
    SetScale(baseScale_);
    Vector3 rot = GetRotation();
    rot.x = 0.0f;
    rot.z = 0.0f;
    SetRotation(rot);
    ResetHookSplitVisual();
}

float EnemyGiantSlime::GetHookSplitProgress() const {
    if (hookSplitAwaitingLanding_ || hookSplitPhase_ == HookSplitPhase::LandingSquash) {
        return 0.0f;
    }
    const float duration = hookSplitWeakPoint_ ? kHookSplitWeakDuration : kHookSplitDuration;
    return std::clamp(hookSplitPullTimer_ / duration, 0.0f, 1.0f);
}

const char* EnemyGiantSlime::GetDebugHookSplitPhaseName() const {
    if (hasSplit_) return "分裂完了";
    switch (hookSplitPhase_) {
    case HookSplitPhase::AirborneTether:
        return "空中拘束・落下";
    case HookSplitPhase::LandingSquash:
        return "着地・受け止め";
    case HookSplitPhase::Brace:
        return "踏ん張り・抵抗";
    case HookSplitPhase::Slip:
        return "引きずられ";
    case HookSplitPhase::Tear:
        return "限界・分裂寸前";
    case HookSplitPhase::None:
    default:
        return "通常";
    }
}

Vector3 EnemyGiantSlime::GetHookAttachmentPosition() const {
    MeshRenderer* renderer = GetMeshRenderer();
    Model* model = renderer ? renderer->GetModel() : nullptr;
    if (!model) {
        return GetWorldPosition() + hookSplitDirection_ * 0.8f + Vector3{ 0.0f, 0.7f, 0.0f };
    }

    const Vector3 modelCenter = model->GetCenter();
    const Vector3 modelSize = model->GetSize();
    const float shearPivotY = modelCenter.y - modelSize.y * 0.5f;
    const Vector3 localAttachment = {
        modelCenter.x,
        modelCenter.y + modelSize.y * 0.18f,
        modelCenter.z - modelSize.z * 0.32f
    };

    Matrix4x4 shearLocal = Math::MakeIdentity4x4();
    shearLocal.m[1][0] = hookVisualShear_.x;
    shearLocal.m[1][2] = hookVisualShear_.z;
    shearLocal.m[3][0] = -shearPivotY * hookVisualShear_.x;
    shearLocal.m[3][2] = -shearPivotY * hookVisualShear_.z;
    const Matrix4x4 visualAffine = Math::MakeAffineMatrix(
        hookVisualScale_, hookVisualRotation_, hookVisualOffset_);
    const Matrix4x4 visualLocal = Math::Multiply(shearLocal, visualAffine);
    return Math::Transform(Math::Transform(localAttachment, visualLocal), GetWorldMatrix());
}

void EnemyGiantSlime::BeginGroundHookResistance(bool landedFromAir) {
    hookSplitAwaitingLanding_ = false;
    hookSplitBasePosition_ = GetTranslate();
    hookSplitBaseScale_ = baseScale_;
    hookSplitPullTimer_ = 0.0f;
    hookSplitPhaseTimer_ = 0.0f;
    hookSplitPhase_ = landedFromAir ? HookSplitPhase::LandingSquash : HookSplitPhase::Brace;
    SetGrounded(true);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SyncGroundCollisionRadius();
}

void EnemyGiantSlime::ApplyHookSplitVisual(float deltaTime, const Vector3& hookOwnerPos) {
    if (!isHookSplitPulled_ || hasSplit_) {
        return;
    }

    Vector3 toOwner = hookOwnerPos - GetTranslate();
    const float planarDistance = std::sqrt(toOwner.x * toOwner.x + toOwner.z * toOwner.z);
    Vector3 direction = NormalizePlanar(toOwner);
    hookSplitDirection_ = direction;
    const Vector3 side = { -direction.z, 0.0f, direction.x };
    const float targetYaw = std::atan2(direction.x, direction.z) + kGiantSlimeModelYawOffset;

    Vector3 targetVisualScale = { 1.0f, 1.0f, 1.0f };
    Vector3 targetVisualRotation = { 0.0f, 0.0f, 0.0f };
    Vector3 targetVisualShear = { 0.0f, 0.0f, 0.0f };
    Vector3 targetPosition = GetTranslate();
    bool pinGroundAnchor = false;

    if (hookSplitAwaitingLanding_) {
        const float fallRate = std::clamp(std::abs(GetVelocity().y) / 24.0f, 0.0f, 1.0f);
        const float tetherPulse = std::sin(idleTimer_ * 13.0f);
        targetVisualScale = {
            1.0f - fallRate * 0.025f,
            1.0f + fallRate * 0.045f,
            1.0f - fallRate * 0.020f
        };
        const float tetherPitch = std::atan2(toOwner.y, (std::max)(planarDistance, 0.001f));
        targetVisualRotation.x = std::clamp(-tetherPitch * 0.34f, -0.28f, 0.28f);
        targetVisualRotation.z = tetherPulse * 0.055f;
        targetVisualShear.z = -0.055f + tetherPulse * 0.018f;
    } else if (hookSplitPhase_ == HookSplitPhase::LandingSquash) {
        const float t = SmoothStep01(hookSplitPhaseTimer_ / kHookLandingSquashDuration);
        if (t < 0.48f) {
            const float impact = SmoothStep01(t / 0.48f);
            targetVisualScale = {
                1.0f + impact * 0.34f,
                1.0f - impact * 0.42f,
                1.0f + impact * 0.22f
            };
        } else {
            const float rebound = SmoothStep01((t - 0.48f) / 0.52f);
            targetVisualScale = {
                1.34f - rebound * 0.30f,
                0.58f + rebound * 0.48f,
                1.22f - rebound * 0.16f
            };
        }
        targetVisualRotation.z = std::sin(t * 3.14159265f) * 0.08f;
        pinGroundAnchor = true;
    } else {
        const float progress = GetHookSplitProgress();
        const float tearProgress = SmoothStep01((progress - 0.82f) / 0.18f);
        const float pullCycle = std::fmod(hookSplitPullTimer_ * 2.55f, 1.0f);
        const float yankRise = SmoothStep01(pullCycle / 0.14f);
        const float yankFall = 1.0f - SmoothStep01((pullCycle - 0.14f) / 0.28f);
        const float yankPulse = yankRise * yankFall;
        const float braceRise = SmoothStep01((pullCycle - 0.30f) / 0.18f);
        const float braceFall = 1.0f - SmoothStep01((pullCycle - 0.50f) / 0.30f);
        const float bracePulse = braceRise * braceFall;
        const float struggleSide = std::sin(hookSplitPullTimer_ * 22.0f) * (0.018f + tearProgress * 0.035f);

        // 伸ばすのではなく、引かれた上側を底面側が踏み戻す反復で抵抗を表現します。
        targetVisualScale = {
            1.0f + bracePulse * 0.055f + yankPulse * 0.025f + tearProgress * 0.025f,
            1.0f - bracePulse * 0.075f - yankPulse * 0.025f - tearProgress * 0.035f,
            1.0f + yankPulse * 0.025f - bracePulse * 0.020f + tearProgress * 0.025f
        };
        targetVisualRotation.x = bracePulse * 0.018f - yankPulse * 0.025f;
        targetVisualRotation.z = struggleSide + (bracePulse - yankPulse) * 0.025f;
        targetVisualShear.z =
            -0.055f
            - yankPulse * (0.18f + progress * 0.14f)
            + bracePulse * 0.085f * (1.0f - progress)
            - std::abs(std::sin(hookSplitPullTimer_ * 28.0f)) * tearProgress * 0.11f;
        targetVisualShear.x = struggleSide * (0.8f + tearProgress * 0.7f);

        const Vector3 baseToOwner = hookOwnerPos - hookSplitBasePosition_;
        const float basePlanarDistance = std::sqrt(
            baseToOwner.x * baseToOwner.x + baseToOwner.z * baseToOwner.z);
        const float maximumSlip = (std::min)(basePlanarDistance * 0.12f, 1.65f);
        const float slipStep1 = SmoothStep01((progress - 0.50f) / 0.10f);
        const float slipStep2 = SmoothStep01((progress - 0.68f) / 0.09f);
        const float slipStep3 = SmoothStep01((progress - 0.84f) / 0.09f);
        const float permanentSlip = maximumSlip * (
            slipStep1 * 0.28f + slipStep2 * 0.32f + slipStep3 * 0.40f);
        const float temporaryYank = yankPulse * (0.055f + progress * 0.12f);
        const float pushBack = bracePulse * 0.045f * (1.0f - progress);
        const float pullDistance = (std::max)(0.0f, permanentSlip + temporaryYank - pushBack);
        targetPosition = hookSplitBasePosition_ + direction * pullDistance;
        targetPosition += side * struggleSide * (0.45f + tearProgress * 0.55f);
        targetPosition.y = hookSplitBasePosition_.y;
        pinGroundAnchor = true;
    }

    targetVisualScale.x = std::clamp(targetVisualScale.x, 0.82f, 1.38f);
    targetVisualScale.y = std::clamp(targetVisualScale.y, 0.56f, 1.16f);
    targetVisualScale.z = std::clamp(targetVisualScale.z, 0.82f, 1.28f);

    SlimeBounceAnimator::StepDampedSpring(
        hookVisualScale_, hookVisualScaleVelocity_, targetVisualScale, deltaTime, 20.0f, 0.62f);
    SlimeBounceAnimator::StepDampedSpring(
        hookVisualRotation_, hookVisualRotationVelocity_, targetVisualRotation, deltaTime, 18.0f, 0.70f);
    SlimeBounceAnimator::StepDampedSpring(
        hookVisualShear_, hookVisualShearVelocity_, targetVisualShear, deltaTime, 24.0f, 0.64f);

    Vector3 targetVisualOffset = pinGroundAnchor
        ? CalculateHookAnchorOffset(hookVisualScale_, hookVisualRotation_)
        : Vector3{ 0.0f, 0.0f, 0.0f };
    SlimeBounceAnimator::StepDampedSpring(
        hookVisualOffset_, hookVisualOffsetVelocity_, targetVisualOffset, deltaTime, 22.0f, 0.78f);

    Vector3 renderedScale = hookVisualScale_;
    Vector3 renderedRotation = hookVisualRotation_;
    Vector3 renderedOffset = hookVisualOffset_;
    ApplyDamageReactionPose(renderedScale, renderedRotation, &renderedOffset, 0.72f);

    SetScale(baseScale_);
    SetRotation({ 0.0f, targetYaw, 0.0f });
    if (!hookSplitAwaitingLanding_) {
        SetTranslate(targetPosition);
    }
    if (MeshRenderer* renderer = GetMeshRenderer()) {
        renderer->SetVisualTransform(renderedScale, renderedRotation, renderedOffset);
        float shearPivotY = 0.0f;
        if (Model* model = renderer->GetModel()) {
            shearPivotY = model->GetCenter().y - model->GetSize().y * 0.5f;
        }
        renderer->SetVisualShear(hookVisualShear_, shearPivotY);
    }

    const float progress = GetHookSplitProgress();
    const float strainPulse = std::abs(std::sin(hookSplitPullTimer_ * 18.0f));
    SetColor({
        0.42f + progress * 0.52f,
        0.88f - progress * 0.30f + strainPulse * 0.06f,
        1.0f - progress * 0.10f,
        1.0f
    });
    UpdateLocalMatrix();
    UpdateWorldMatrix();
}

Vector3 EnemyGiantSlime::CalculateHookAnchorOffset(const Vector3& visualScale, const Vector3& visualRotation) const {
    MeshRenderer* renderer = GetMeshRenderer();
    Model* model = renderer ? renderer->GetModel() : nullptr;
    if (!model) {
        return {
            0.0f,
            0.5f * (1.0f - visualScale.y),
            0.5f * (1.0f - visualScale.z)
        };
    }

    const Vector3 modelCenter = model->GetCenter();
    const Vector3 modelSize = model->GetSize();
    // 描画姿勢が変わっても、プレイヤーと反対側の接地点を同じ場所に保ちます。
    const Vector3 rearGroundAnchor = {
        modelCenter.x,
        modelCenter.y - modelSize.y * 0.5f,
        modelCenter.z + modelSize.z * 0.5f
    };
    const Matrix4x4 visualMatrix = Math::MakeAffineMatrix(
        visualScale, visualRotation, { 0.0f, 0.0f, 0.0f });
    const Vector3 deformedAnchor = Math::Transform(rearGroundAnchor, visualMatrix);
    return rearGroundAnchor - deformedAnchor;
}

void EnemyGiantSlime::ResetHookSplitVisual() {
    hookVisualScale_ = { 1.0f, 1.0f, 1.0f };
    hookVisualScaleVelocity_ = { 0.0f, 0.0f, 0.0f };
    hookVisualRotation_ = { 0.0f, 0.0f, 0.0f };
    hookVisualRotationVelocity_ = { 0.0f, 0.0f, 0.0f };
    hookVisualOffset_ = { 0.0f, 0.0f, 0.0f };
    hookVisualOffsetVelocity_ = { 0.0f, 0.0f, 0.0f };
    hookVisualShear_ = { 0.0f, 0.0f, 0.0f };
    hookVisualShearVelocity_ = { 0.0f, 0.0f, 0.0f };
    if (MeshRenderer* renderer = GetMeshRenderer()) {
        renderer->ResetVisualTransform();
    }
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
    const EnemyAttackDefinition& attack = GetAttackDefinition(kJumpPressAttackId);
    if (distance > attack.radius) return;

    Vector3 direction = distance > 0.001f ? diff / distance : Vector3{ 0.0f, 0.0f, 1.0f };
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = attack.damage;
    damageEvent.knockbackVelocity = { direction.x * 18.0f, 13.0f, direction.z * 18.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);
}

void EnemyGiantSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    Vector3 targetRotation = { 0.0f, GetRotation().y, 0.0f };
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

    ApplyDamageReactionPose(targetScale, targetRotation, nullptr, 0.82f);
    const float poseRate = (std::min)(1.0f, deltaTime * 8.0f);
    SetScale(Math::Lerp(GetScale(), targetScale, poseRate));
    const Vector3 currentRotation = GetRotation();
    SetRotation({
        currentRotation.x + (targetRotation.x - currentRotation.x) * poseRate,
        currentRotation.y,
        currentRotation.z + (targetRotation.z - currentRotation.z) * poseRate
    });
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
    hookSplitAwaitingLanding_ = false;
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
            slime->SetName(GetName() + "_SplitSlime_" + std::to_string(i));
            slime->SetTarget(target_);
            slime->SetDetectionRange(18.0f);
            slime->SetVelocity({ dir.x * 4.0f, 4.0f, dir.z * 4.0f });
            if (IsEditorInternal()) {
                slime->SetEditorInternal(true);
                slime->SetIsLocked(true);
                slime->SetCollisionAttribute(0);
                slime->SetCollisionMask(0);
            }
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
