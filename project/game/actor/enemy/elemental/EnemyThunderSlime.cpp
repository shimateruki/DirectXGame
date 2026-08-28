#include "EnemyThunderSlime.h"
#include "SlimeBounceAnimator.h"
#include "CameraManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EffectObject3d.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include <algorithm>
#include <cmath>

namespace {
// 雷スライムの放電範囲、タメ時間、常時オーラの調整値
constexpr const char* kShockAttackId = "radial_shock";
constexpr const char* kWildLineAttackId = "line_lightning";
constexpr float kMoveSpeedScale = 1.12f;
constexpr float kWildStrikeInterval = 0.13f;
constexpr float kWildStrikeStartDistance = 2.8f;
constexpr float kWildStrikeSpacing = 2.7f;
constexpr float kLightningStrikeHeight = 9.5f;
constexpr float kGroundCollisionWorldRadius = 0.82f;
constexpr float kThrownCollisionWorldRadius = 1.18f;
constexpr float kMoveHopInterval = 0.25f;
constexpr float kMoveHopPower = 5.0f;
constexpr float kThunderSlimeModelYawOffset = 3.1415926535f;
constexpr const char* kDischargePreset = "thunder_slime_discharge";
constexpr const char* kIdleSparkPreset = "thunder_slime_idle_spark";
constexpr const char* kRadialChargePreset = "thunder_slime_radial_charge";
constexpr const char* kLineChargePreset = "thunder_slime_charge";
constexpr const char* kLightningStrikeParticlePreset = "player_thunder_strike_impact";
constexpr const char* kConstantAuraEffectPath = "Resources/json/effect/effect_thunder_slime_constant_aura.json";
constexpr const char* kLightningWarningEffectPath = "Resources/json/effect/effect_player_thunder_warning.json";
constexpr const char* kLightningBoltEffectPath = "Resources/json/effect/effect_player_thunder_bolt.json";
constexpr const char* kLightningCoreEffectPath = "Resources/json/effect/effect_player_thunder_core.json";
constexpr const char* kLightningImpactEffectPath = "Resources/json/effect/effect_player_thunder_impact_ring.json";
constexpr const char* kChargeGroundEffectPath = "Resources/json/effect/effect_thunder_charge_ground.json";
constexpr const char* kScorchMarkEffectPath = "Resources/json/effect/effect_thunder_scorch_mark.json";
constexpr float kIdleSparkInterval = 0.095f;
constexpr float kTwoPi = 6.283185307f;
constexpr float kHalfPi = 1.570796327f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

Object3d* FindEnemyDamageTarget(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}
}

// 明示的な破棄処理は持たず、unique_ptr に任せる
EnemyThunderSlime::~EnemyThunderSlime() = default;

// 雷スライム本体と常時オーラの初期化
void EnemyThunderSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_ThunderSlime");
    SetEnemyType("ThunderSlime");
    ReloadAttackProfile();
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    defaultColor_ = GetColor();

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
    InitializeAuraEffect();
}

// 近距離放電、追跡、徘徊、オーラ同期の更新
void EnemyThunderSlime::Update(float deltaTime) {
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    EnsureBaseScale();
    UpdateWildTimers(deltaTime);

    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    UpdateWildBehavior(deltaTime, velocity);
    ApplyGroundMovementAndAnimation(deltaTime, velocity);
    BaseEnemy::Update(deltaTime);
    UpdateAuraEffect(deltaTime);
}

bool EnemyThunderSlime::UpdateInactiveState(float deltaTime) {
    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        isCharging_ = false;
        chargeWarningTriggered_ = false;
        chargeTimer_ = 0.0f;
        chargeParticleTimer_ = 0.0f;
        ResetWildLightning();
        shockSquashTimer_ = 0.0f;
        idleSparkTimer_ = kIdleSparkInterval;
        HideAuraEffect();
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        return true;
    }

    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        HideAuraEffect();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    idleTimer_ += deltaTime;
    UpdateIdleSpark(deltaTime);
    if (isCarried_) {
        HideAttackTelegraph();
        UpdateAuraEffect(deltaTime);
        return true;
    }
    return UpdateThrowRecoveryState(deltaTime);
}

bool EnemyThunderSlime::UpdateThrowRecoveryState(float deltaTime) {
    if (!IsThrowRecovering()) {
        return false;
    }
    if (IsThrownPhysics()) {
        SyncThrownCollisionRadius();
    } else {
        SyncGroundCollisionRadius();
    }
    BaseEnemy::Update(deltaTime);
    UpdateAuraEffect(deltaTime);
    return true;
}

void EnemyThunderSlime::EnsureBaseScale() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyThunderSlime::UpdateWildTimers(float deltaTime) {
    shockSquashTimer_ = (std::max)(0.0f, shockSquashTimer_ - deltaTime);
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
}

void EnemyThunderSlime::UpdateWildBehavior(float deltaTime, Vector3& velocity) {
    if (wildLightningState_ != WildLightningState::Idle) {
        UpdateFacing(wildLightningDirection_);
        UpdateWildLightning(deltaTime);
        return;
    }

    if (target_ && param_.has_value()) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        UpdateFacing(direction);
        UpdateCombatBehavior(deltaTime, velocity, direction, distance);
    }
    if (!isCharging_ && wildLightningState_ == WildLightningState::Idle) {
        HideAttackTelegraph();
    }
}

void EnemyThunderSlime::UpdateCombatBehavior(float deltaTime, Vector3& velocity, const Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kShockAttackId);
    const EnemyAttackDefinition& lineAttack = GetAttackDefinition(kWildLineAttackId);
    const bool forceShockPreview = debugPreviewAttackId_ == kShockAttackId;
    const bool forceLinePreview = debugPreviewAttackId_ == kWildLineAttackId;
    if (!isCharging_ && UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateFacing(direction);
        return;
    }

    if (isCharging_) {
        UpdateCharge(deltaTime, direction);
    }
    else if (!forceLinePreview &&
        distance >= attack.minRange && distance <= attack.maxRange && attackCooldown_ <= 0.0f) {
        lastShockDirection_ = direction;
        StartCharge();
    }
    else if (!forceShockPreview &&
        distance >= lineAttack.minRange && distance <= lineAttack.maxRange && attackCooldown_ <= 0.0f) {
        StartWildLightning(direction);
    }
    else if (distance <= detectionRange_) {
        const float speed = (std::max)(0.0f, param_->speed) * kMoveSpeedScale;
        velocity.x = direction.x * speed;
        velocity.z = direction.z * speed;
    }
    else {
        UpdateWanderBehavior(deltaTime, velocity);
    }
}

void EnemyThunderSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    debugPreviewAttackId_ = attackId;
    attackCooldown_ = 0.0f;
    isCharging_ = false;
    chargeTimer_ = 0.0f;
    ResetWildLightning();
}

const char* EnemyThunderSlime::GetDebugAttackPhaseName() const {
    if (wildLightningState_ == WildLightningState::Charging) {
        return "連続落雷・溜め";
    }
    if (wildLightningState_ == WildLightningState::Striking) {
        return "連続落雷・落雷中";
    }
    if (isCharging_) {
        return "近距離放電・溜め";
    }
    if (shockSquashTimer_ > 0.0f) {
        return "放電後・反発";
    }
    if (attackCooldown_ > 0.0f) {
        return "再使用待ち";
    }
    return "待機・移動";
}

// 中距離では、狙った方向へ予告を並べてから連続落雷を走らせます。
void EnemyThunderSlime::StartWildLightning(const Vector3& direction) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kWildLineAttackId);
    wildLightningState_ = WildLightningState::Charging;
    wildLightningDirection_ = NormalizePlanar(direction);
    wildLightningTimer_ = (std::max)(0.12f, attack.windupDuration);
    wildLightningEffectTimer_ = 0.0f;
    wildStrikeIndex_ = 0;
    wildLightningHitTarget_ = false;
    chargeWarningTriggered_ = false;
    attackCooldown_ = (std::max)(0.1f, attack.cooldown);
    HideAttackTelegraph();

    const Vector3 origin = GetWorldPosition();
    MeshEffectManager* meshEffects = MeshEffectManager::GetInstance();
    for (std::size_t index = 0; index < wildStrikePositions_.size(); ++index) {
        const float distance = kWildStrikeStartDistance + kWildStrikeSpacing * static_cast<float>(index);
        const Vector3 samplePosition = origin + wildLightningDirection_ * distance;
        wildStrikePositions_[index] = FindStrikeGround(samplePosition, this, origin.y);
        if (meshEffects) {
            Vector3 warningPosition = wildStrikePositions_[index];
            warningPosition.y += 0.045f;
            meshEffects->SpawnEffectAt(kLightningWarningEffectPath, warningPosition, { 0.0f, 0.0f, 0.0f });
        }
    }

    const Vector3 chargeGround = FindStrikeGround(origin, this, origin.y);
    SpawnChargeGroundEffect(chargeGround, 1.12f);
    EmitLineChargeEffect(
        attack.windupVfx.empty() ? kLineChargePreset : attack.windupVfx.c_str(),
        0.0f);
}

void EnemyThunderSlime::UpdateWildLightning(float deltaTime) {
    if (wildLightningState_ == WildLightningState::Idle) {
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kWildLineAttackId);
    wildLightningTimer_ -= deltaTime;
    wildLightningEffectTimer_ -= deltaTime;

    if (wildLightningState_ == WildLightningState::Charging) {
        const float windupDuration = (std::max)(0.01f, attack.windupDuration);
        const float progress = 1.0f - std::clamp(wildLightningTimer_ / windupDuration, 0.0f, 1.0f);
        ShowAttackTelegraphImpactAreas(
            wildStrikePositions_.data(),
            wildStrikePositions_.size(),
            attack.radius,
            progress,
            { 1.0f, 0.94f, 0.16f, 0.84f });
        if (wildLightningEffectTimer_ <= 0.0f) {
            EmitLineChargeEffect(
                attack.windupVfx.empty() ? kLineChargePreset : attack.windupVfx.c_str(),
                progress);
            wildLightningEffectTimer_ = 0.072f - progress * 0.030f;
        }
        if (!chargeWarningTriggered_ && wildLightningTimer_ <= attack.warningLeadTime) {
            TriggerAttackTelegraphCue({ 0.92f, 0.98f, 0.20f, 1.0f });
            chargeWarningTriggered_ = true;
        }
        if (wildLightningTimer_ <= 0.0f) {
            wildLightningState_ = WildLightningState::Striking;
            wildLightningTimer_ = 0.0f;
        }
        return;
    }

    while (wildLightningTimer_ <= 0.0f &&
        wildStrikeIndex_ < static_cast<int>(wildStrikePositions_.size())) {
        SpawnWildLightningStrike(wildStrikePositions_[wildStrikeIndex_]);
        ++wildStrikeIndex_;
        wildLightningTimer_ += kWildStrikeInterval;
    }

    if (wildStrikeIndex_ < static_cast<int>(wildStrikePositions_.size())) {
        const std::size_t remainingCount = wildStrikePositions_.size() - static_cast<std::size_t>(wildStrikeIndex_);
        const float strikeProgress = 1.0f - std::clamp(
            wildLightningTimer_ / kWildStrikeInterval,
            0.0f,
            1.0f);
        ShowAttackTelegraphImpactAreas(
            wildStrikePositions_.data() + wildStrikeIndex_,
            remainingCount,
            attack.radius,
            strikeProgress,
            { 1.0f, 0.94f, 0.16f, 0.76f });
    }

    if (wildStrikeIndex_ >= static_cast<int>(wildStrikePositions_.size())) {
        HideAttackTelegraph();
        wildLightningState_ = WildLightningState::Idle;
        wildLightningTimer_ = 0.0f;
        wildLightningEffectTimer_ = 0.0f;
        wildStrikeIndex_ = 0;
        wildLightningHitTarget_ = false;
        shockSquashTimer_ = (std::max)(shockSquashTimer_, attack.recoveryDuration);
    }
}

void EnemyThunderSlime::SpawnWildLightningStrike(const Vector3& groundPosition) {
    SpawnLightningStrikeVisual(groundPosition, wildLightningDirection_);
    DispatchWildLightningDamage(groundPosition);
}

void EnemyThunderSlime::DispatchWildLightningDamage(const Vector3& groundPosition) {
    if (!target_ || wildLightningHitTarget_) {
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kWildLineAttackId);
    const Vector3 targetPosition = target_->GetWorldPosition();
    const Vector3 horizontal = {
        targetPosition.x - groundPosition.x,
        0.0f,
        targetPosition.z - groundPosition.z,
    };
    const float verticalDistance = targetPosition.y - groundPosition.y;
    if (Math::Length(horizontal) > attack.radius ||
        verticalDistance < -0.65f || verticalDistance > kLightningStrikeHeight) {
        return;
    }

    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = attack.damage;
    damageEvent.damageType = DamageType::Electric;
    damageEvent.knockbackVelocity = {
        wildLightningDirection_.x * 7.5f,
        6.5f,
        wildLightningDirection_.z * 7.5f,
    };
    EventManager::GetInstance()->Dispatch(damageEvent);
    wildLightningHitTarget_ = true;
}

void EnemyThunderSlime::ResetWildLightning() {
    wildLightningState_ = WildLightningState::Idle;
    wildLightningTimer_ = 0.0f;
    wildLightningEffectTimer_ = 0.0f;
    wildStrikeIndex_ = 0;
    wildLightningHitTarget_ = false;
}

void EnemyThunderSlime::UpdateWanderBehavior(float deltaTime, Vector3& velocity) {
    const float speed = (std::max)(0.65f, param_->speed * 0.48f);
    velocity = CalculateWanderVelocity(deltaTime, speed, 0.68f);
    UpdateFacing({ velocity.x, 0.0f, velocity.z });
}

void EnemyThunderSlime::ApplyGroundMovementAndAnimation(float deltaTime, Vector3& velocity) {
    velocity.y = (std::min)(GetVelocity().y, 0.0f);
    if (isCharging_ || wildLightningState_ != WildLightningState::Idle) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        groundHopTimer_ = 0.0f;
    } else if (SlimeBounceAnimator::StepGroundHop(groundHopTimer_, velocity, isGrounded_, deltaTime, kMoveHopInterval, 0.10f)) {
        velocity.y = (std::max)(velocity.y, kMoveHopPower);
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    SyncGroundCollisionRadius();
}

void EnemyThunderSlime::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    BaseEnemy::Draw(pointLightResource, spotLightResource);
    if (auraEffect_ && auraEffect_->GetIsVisible()) {
        auraEffect_->Draw(pointLightResource, spotLightResource);
    }
}

std::unique_ptr<Object3d> EnemyThunderSlime::Clone() const {
    auto clone = std::make_unique<EnemyThunderSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyThunderSlime::BeginThrown(const Vector3& initialVelocity) {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        const float capturedMaxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
        if (capturedMaxScale < 1.2f) {
            baseScale_ = { 2.0f, 2.0f, 2.0f };
        }
        hasBaseScale_ = true;
    }
    isCharging_ = false;
    chargeWarningTriggered_ = false;
    chargeTimer_ = 0.0f;
    chargeParticleTimer_ = 0.0f;
    shockSquashTimer_ = 0.0f;
    ResetWildLightning();
    HideAttackTelegraph();
    SetScale(baseScale_);
    SyncThrownCollisionRadius();
    BaseEnemy::BeginThrown(initialVelocity);
}

Vector3 EnemyThunderSlime::FindStrikeGround(
    const Vector3& samplePosition,
    Object3d* ignoredObject,
    float fallbackHeight) const {
    Vector3 rayStart = samplePosition;
    rayStart.y += 10.0f;

    PhysicsQueryFilter filter;
    filter.mask = kAllGround;
    filter.ignoredObject = ignoredObject;
    const RaycastHit hit = CollisionManager::GetInstance()->Raycast(
        rayStart, { 0.0f, -1.0f, 0.0f }, 22.0f, filter);
    if (hit.isHit) {
        return hit.hitPoint;
    }

    Vector3 fallback = samplePosition;
    fallback.y = fallbackHeight;
    return fallback;
}

void EnemyThunderSlime::SpawnLightningStrikeVisual(const Vector3& groundPosition, const Vector3& direction) {
    Vector3 strikeCenter = groundPosition;
    strikeCenter.y += kLightningStrikeHeight * 0.5f;
    const float strikeYaw = std::atan2(direction.x, direction.z);

    if (MeshEffectManager* meshEffects = MeshEffectManager::GetInstance()) {
        meshEffects->SpawnEffectAt(kLightningBoltEffectPath, strikeCenter, { kHalfPi, strikeYaw, 0.0f });
        meshEffects->SpawnEffectAt(
            kLightningBoltEffectPath,
            strikeCenter,
            { kHalfPi, strikeYaw + kHalfPi, 0.0f },
            { 0.82f, 1.0f, 0.94f });
        const Vector3 branchSide = { direction.z, 0.0f, -direction.x };
        meshEffects->SpawnEffectAt(
            kLightningBoltEffectPath,
            strikeCenter + branchSide * 0.34f + direction * 0.18f,
            { kHalfPi, strikeYaw + 0.31f, 0.0f },
            { 0.46f, 1.0f, 0.76f });
        meshEffects->SpawnEffectAt(
            kLightningBoltEffectPath,
            strikeCenter - branchSide * 0.30f - direction * 0.14f,
            { kHalfPi, strikeYaw - 0.27f, 0.0f },
            { 0.38f, 1.0f, 0.68f });
        meshEffects->SpawnEffectAt(kLightningCoreEffectPath, strikeCenter, { 0.0f, 0.0f, 0.0f });

        Vector3 impactPosition = groundPosition;
        impactPosition.y += 0.055f;
        meshEffects->SpawnEffectAt(kLightningImpactEffectPath, impactPosition, { 0.0f, 0.0f, 0.0f });
        meshEffects->SpawnEffectAt(
            kLightningImpactEffectPath,
            impactPosition + Vector3{ 0.0f, 0.025f, 0.0f },
            { 0.0f, strikeYaw + 0.35f, 0.0f },
            { 1.32f, 1.0f, 1.32f });
        meshEffects->SpawnEffectAt(
            kScorchMarkEffectPath,
            impactPosition + Vector3{ 0.0f, 0.012f, 0.0f },
            { 0.0f, strikeYaw, 0.0f });
    }

    Vector3 particlePosition = groundPosition;
    particlePosition.y += 0.22f;
    EmitThunderPreset(kLightningStrikeParticlePreset, particlePosition);
    EmitThunderPreset(kLightningStrikeParticlePreset, particlePosition + Vector3{ 0.22f, 0.08f, -0.18f });
    EmitThunderPreset(kDischargePreset, particlePosition);
}

void EnemyThunderSlime::SpawnChargeGroundEffect(const Vector3& groundPosition, float scale) {
    if (MeshEffectManager* meshEffects = MeshEffectManager::GetInstance()) {
        Vector3 effectPosition = groundPosition;
        effectPosition.y += 0.035f;
        meshEffects->SpawnEffectAt(
            kChargeGroundEffectPath,
            effectPosition,
            { 0.0f, idleTimer_ * 0.8f, 0.0f },
            { scale, 1.0f, scale });
    }
}

// 放電攻撃と火花演出
void EnemyThunderSlime::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;
    const float targetYaw = std::atan2(direction.x, direction.z) + kThunderSlimeModelYawOffset;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.16f));
}

void EnemyThunderSlime::StartCharge() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
    isCharging_ = true;
    chargeWarningTriggered_ = false;
    const EnemyAttackDefinition& attack = GetAttackDefinition(kShockAttackId);
    chargeTimer_ = attack.windupDuration;
    chargeParticleTimer_ = 0.0f;
    attackCooldown_ = attack.cooldown;
    const Vector3 origin = GetWorldPosition();
    SpawnChargeGroundEffect(FindStrikeGround(origin, this, origin.y), 1.18f);
    EmitOuterThunderEffect(
        attack.windupVfx.empty() ? kRadialChargePreset : attack.windupVfx.c_str(),
        2,
        0.18f);
    SyncGroundCollisionRadius();
}

void EnemyThunderSlime::UpdateCharge(float deltaTime, const Vector3& direction) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kShockAttackId);
    lastShockDirection_ = direction;
    chargeTimer_ = (std::max)(0.0f, chargeTimer_ - deltaTime);
    chargeParticleTimer_ -= deltaTime;
    const float windupDuration = (std::max)(0.01f, attack.windupDuration);
    const float progress = 1.0f - (std::clamp)(chargeTimer_ / windupDuration, 0.0f, 1.0f);
    ShowAttackTelegraphCircle(
        GetTranslate(),
        attack.radius,
        progress,
        { 1.0f, 0.95f, 0.16f, 0.82f });

    if (chargeParticleTimer_ <= 0.0f) {
        const int arcCount = progress >= 0.68f ? 3 : 2;
        EmitOuterThunderEffect(
            attack.windupVfx.empty() ? kRadialChargePreset : attack.windupVfx.c_str(),
            arcCount,
            0.55f + progress * 1.4f);
        chargeParticleTimer_ = 0.070f - progress * 0.032f;
    }

    if (!chargeWarningTriggered_ && chargeTimer_ <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue({ 1.0f, 0.92f, 0.12f, 1.0f });
        chargeWarningTriggered_ = true;
    }

    if (chargeTimer_ <= 0.0f) {
        ReleaseShock(lastShockDirection_);
    }
}

void EnemyThunderSlime::ReleaseShock(const Vector3& direction) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kShockAttackId);
    isCharging_ = false;
    if (!chargeWarningTriggered_) {
        TriggerAttackTelegraphCue({ 1.0f, 0.92f, 0.12f, 1.0f });
        chargeWarningTriggered_ = true;
    }
    HideAttackTelegraph();
    shockSquashTimer_ = attack.recoveryDuration;
    SyncGroundCollisionRadius();

    Vector3 center = GetTranslate();
    center.y += (std::max)(0.5f, baseScale_.y * 0.22f);
    EmitOuterThunderEffect(attack.activeVfx.empty() ? kDischargePreset : attack.activeVfx.c_str(), 5, 0.35f);
    if (MeshEffectManager* meshEffects = MeshEffectManager::GetInstance()) {
        const Vector3 origin = GetWorldPosition();
        Vector3 scorchPosition = FindStrikeGround(origin, this, origin.y);
        scorchPosition.y += 0.045f;
        meshEffects->SpawnEffectAt(kScorchMarkEffectPath, scorchPosition, { 0.0f, GetRotation().y, 0.0f }, { 1.25f, 1.0f, 1.25f });
    }
    DispatchShockDamage(center, direction, attack.radius, attack.damage);
}

void EnemyThunderSlime::DispatchShockDamage(const Vector3& center, const Vector3& direction, float radius, float damage) {
    if (!target_) {
        return;
    }

    Vector3 diff = target_->GetTranslate() - center;
    diff.y *= 0.45f;
    const float distance = Math::Length(diff);
    if (distance > radius) {
        return;
    }

    const float distanceRate = 1.0f - (std::clamp)(distance / radius, 0.0f, 1.0f);
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = damage;
    damageEvent.damageType = DamageType::Electric;
    damageEvent.knockbackVelocity = {
        direction.x * (8.0f + distanceRate * 5.0f),
        5.8f + distanceRate * 2.0f,
        direction.z * (8.0f + distanceRate * 5.0f)
    };
    EventManager::GetInstance()->Dispatch(damageEvent);
}

void EnemyThunderSlime::UpdateIdleSpark(float deltaTime) {
    // 攻撃の溜め中は専用電弧だけを使い、待機火花との二重発生で輪郭が潰れるのを防ぎます。
    if (isCharging_ || wildLightningState_ == WildLightningState::Charging) {
        idleSparkTimer_ = kIdleSparkInterval;
        return;
    }

    idleSparkTimer_ -= deltaTime;
    if (idleSparkTimer_ > 0.0f) {
        return;
    }

    EmitOuterThunderEffect(kIdleSparkPreset, isCharging_ ? 3 : 2);
    if (isCharging_) {
        idleSparkTimer_ = kIdleSparkInterval * 0.62f;
    }
    else if (isCarried_) {
        idleSparkTimer_ = kIdleSparkInterval * 0.76f;
    }
    else {
        idleSparkTimer_ = kIdleSparkInterval;
    }
}

// 常時オーラの表示位置と形状同期
void EnemyThunderSlime::InitializeAuraEffect() {
    if (!common_ || auraEffect_) {
        return;
    }

    auraEffect_ = std::make_unique<EffectObject3d>();
    auraEffect_->Initialize(common_);
    auraEffect_->SetName("ThunderSlime_RoundAura");
    if (!auraEffect_->LoadFromJson(kConstantAuraEffectPath)) {
        auraEffect_.reset();
        return;
    }
    auraEffect_->SetIsVisible(false);
    auraEffect_->Play(99999.0f);
}

void EnemyThunderSlime::UpdateAuraEffect(float deltaTime) {
    if (!auraEffect_) {
        InitializeAuraEffect();
    }
    if (!auraEffect_) {
        return;
    }

    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        const float capturedMaxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
        if (capturedMaxScale < 1.2f) {
            baseScale_ = { 2.0f, 2.0f, 2.0f };
        }
        hasBaseScale_ = true;
    }

    Vector3 auraPos{};
    float horizontalDiameter = 1.0f;
    float verticalDiameter = 1.0f;
    CalculateAuraShape(auraPos, horizontalDiameter, verticalDiameter);

    float yaw = GetRotation().y;
    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;
    if (camera) {
        const Vector3 toCamera = camera->GetEye() - auraPos;
        if (std::abs(toCamera.x) + std::abs(toCamera.z) > 0.001f) {
            yaw = std::atan2(toCamera.x, toCamera.z);
        }
    }

    auraEffect_->SetIsVisible(true);
    auraEffect_->Update(deltaTime);
    auraEffect_->SetTranslate(auraPos);
    auraEffect_->SetRotation({ 1.5707963f, yaw, 0.0f });
    auraEffect_->SetScale({ horizontalDiameter, 1.0f, verticalDiameter });
    auraEffect_->UpdateLocalMatrix();
    auraEffect_->UpdateWorldMatrix();
}

void EnemyThunderSlime::HideAuraEffect() {
    if (auraEffect_) {
        auraEffect_->SetIsVisible(false);
    }
}

void EnemyThunderSlime::CalculateAuraShape(Vector3& center, float& horizontalDiameter, float& verticalDiameter) const {
    const Vector3 scale = GetScale();
    const float maxXZ = (std::max)({ std::abs(scale.x), std::abs(scale.z), 1.0f });
    const float yScale = (std::max)(std::abs(scale.y), 0.18f);

    horizontalDiameter = (std::max)(2.55f, maxXZ * 1.35f);
    verticalDiameter = (std::max)(0.46f, yScale * 1.28f);

    center = GetWorldPosition();
    center.y += (std::max)(0.22f, yScale * 0.43f);
}

void EnemyThunderSlime::EmitOuterThunderEffect(const char* presetName, int count, float phaseOffset) {
    if (count <= 0) {
        return;
    }

    Vector3 center{};
    float horizontalDiameter = 1.0f;
    float verticalDiameter = 1.0f;
    CalculateAuraShape(center, horizontalDiameter, verticalDiameter);

    const float horizontalRadius = horizontalDiameter * 0.52f;
    const float verticalRadius = verticalDiameter * 0.38f;
    const float phase = idleTimer_ * 3.4f + phaseOffset;
    for (int i = 0; i < count; ++i) {
        const float angle = phase + (kTwoPi * static_cast<float>(i) / static_cast<float>(count));
        Vector3 pos = center;
        pos.x += std::cos(angle) * horizontalRadius;
        pos.z += std::sin(angle) * horizontalRadius;
        pos.y += std::sin(angle * 1.37f) * verticalRadius;
        EmitThunderPreset(presetName, pos);
    }
}

// 中距離落雷では、外周の電弧を上方へ絞り込み、次に縦方向の雷が来ることを形で示します。
void EnemyThunderSlime::EmitLineChargeEffect(const char* presetName, float progress) {
    if (!presetName || presetName[0] == '\0') {
        return;
    }

    Vector3 center{};
    float horizontalDiameter = 1.0f;
    float verticalDiameter = 1.0f;
    CalculateAuraShape(center, horizontalDiameter, verticalDiameter);

    const float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
    const float radius = horizontalDiameter * (0.42f - clampedProgress * 0.17f);
    const int arcCount = clampedProgress >= 0.62f ? 3 : 2;
    const float phase = idleTimer_ * 4.8f + clampedProgress * 2.3f;
    for (int index = 0; index < arcCount; ++index) {
        const float angle = phase + kTwoPi * static_cast<float>(index) / static_cast<float>(arcCount);
        Vector3 position = center;
        position.x += std::cos(angle) * radius;
        position.z += std::sin(angle) * radius;
        position.y += verticalDiameter * (0.24f + 0.16f * std::sin(angle * 1.7f));
        position.y += 0.18f + clampedProgress * (0.32f + 0.16f * static_cast<float>(index));
        EmitThunderPreset(presetName, position);
    }

    if (clampedProgress >= 0.74f) {
        Vector3 crownPosition = center;
        crownPosition.y += verticalDiameter * 0.58f + 0.54f + clampedProgress * 0.42f;
        EmitThunderPreset(presetName, crownPosition);
    }
}

void EnemyThunderSlime::EmitThunderPreset(const char* presetName, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->Emit(presetName, position);
}

void EnemyThunderSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    Vector3 targetRotation = { 0.0f, GetRotation().y, 0.0f };
    if (isCharging_) {
        const float windupDuration = (std::max)(0.01f, GetAttackDefinition(kShockAttackId).windupDuration);
        const float t = std::clamp(1.0f - (chargeTimer_ / windupDuration), 0.0f, 1.0f);
        const float slam = t * t * (3.0f - 2.0f * t);
        const float chargePulse = std::sin(t * kTwoPi * 3.0f) * (0.018f + slam * 0.035f);
        const float finalClamp = std::clamp((t - 0.76f) / 0.24f, 0.0f, 1.0f);
        targetScale.x = baseScale_.x * (1.0f + slam * 0.30f + finalClamp * 0.18f + chargePulse);
        targetScale.y = baseScale_.y * (1.0f - slam * 0.18f - finalClamp * 0.22f - std::abs(chargePulse) * 0.25f);
        targetScale.z = baseScale_.z * (1.0f + slam * 0.27f + finalClamp * 0.16f - chargePulse);
        targetRotation.x = chargePulse * 0.8f;
        targetRotation.z = std::sin(t * kTwoPi * 4.0f) * 0.035f * slam;
        SetColor({ 1.0f, 0.98f, 0.70f + std::sin(t * 58.0f) * 0.08f, 1.0f });
    }
    else if (wildLightningState_ != WildLightningState::Idle) {
        const EnemyAttackDefinition& lineAttack = GetAttackDefinition(kWildLineAttackId);
        const float windupDuration = (std::max)(0.01f, lineAttack.windupDuration);
        const bool isWindup = wildLightningState_ == WildLightningState::Charging;
        const float progress = isWindup
            ? std::clamp(1.0f - wildLightningTimer_ / windupDuration, 0.0f, 1.0f)
            : std::clamp(static_cast<float>(wildStrikeIndex_) / static_cast<float>(wildStrikePositions_.size()), 0.0f, 1.0f);
        const float pulse = std::sin(progress * kTwoPi * (isWindup ? 3.5f : 5.0f));
        const float gather = progress * progress * (3.0f - 2.0f * progress);
        targetScale = {
            baseScale_.x * (1.0f + gather * 0.24f + pulse * 0.045f),
            baseScale_.y * (1.0f - gather * 0.17f - std::abs(pulse) * 0.025f),
            baseScale_.z * (1.0f + gather * 0.22f - pulse * 0.038f),
        };
        targetRotation.x = pulse * 0.045f;
        targetRotation.z = std::sin(progress * kTwoPi * 4.0f) * 0.055f * (0.35f + gather * 0.65f);
        SetColor({ 1.0f, 0.98f, 0.72f + std::abs(pulse) * 0.18f, 1.0f });
    }
    else if (shockSquashTimer_ > 0.0f) {
        const float recoveryDuration = (std::max)(0.01f, GetAttackDefinition(kShockAttackId).recoveryDuration);
        const float remaining = (std::clamp)(shockSquashTimer_ / recoveryDuration, 0.0f, 1.0f);
        const float elapsed = 1.0f - remaining;
        Vector3 multiplier = { 1.0f, 1.0f, 1.0f };
        if (elapsed < 0.28f) {
            const float phase = elapsed / 0.28f;
            const float eased = phase * phase * (3.0f - 2.0f * phase);
            multiplier = {
                1.82f + (0.90f - 1.82f) * eased,
                0.48f + (1.25f - 0.48f) * eased,
                1.76f + (0.94f - 1.76f) * eased
            };
        } else if (elapsed < 0.64f) {
            const float phase = (elapsed - 0.28f) / 0.36f;
            const float eased = phase * phase * (3.0f - 2.0f * phase);
            multiplier = {
                0.90f + (1.12f - 0.90f) * eased,
                1.25f + (0.86f - 1.25f) * eased,
                0.94f + (1.10f - 0.94f) * eased
            };
        } else {
            const float phase = (elapsed - 0.64f) / 0.36f;
            const float eased = phase * phase * (3.0f - 2.0f * phase);
            multiplier = {
                1.12f + (1.0f - 1.12f) * eased,
                0.86f + (1.0f - 0.86f) * eased,
                1.10f + (1.0f - 1.10f) * eased
            };
        }
        targetScale = {
            baseScale_.x * multiplier.x,
            baseScale_.y * multiplier.y,
            baseScale_.z * multiplier.z
        };
        targetRotation.z = std::sin(elapsed * kTwoPi * 1.5f) * remaining * 0.07f;
        SetColor({ 1.0f, 0.98f, 0.70f + remaining * 0.16f, 1.0f });
    }
    else {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 2.1f;
        params.idleAmplitude = 0.085f;
        params.moveAmplitude = 0.30f;
        params.hopFrequency = 11.0f;
        params.horizontalSquash = 0.30f;
        params.verticalStretch = 0.38f;
        params.airborneStretch = 0.36f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
        SetColor(defaultColor_);
    }

    ApplyDamageReactionPose(targetScale, targetRotation);
    Vector3 scale = GetScale();
    const float lerpRate = isCharging_ || wildLightningState_ != WildLightningState::Idle || shockSquashTimer_ > 0.0f
        ? (std::min)(1.0f, deltaTime * 24.0f)
        : (std::min)(1.0f, deltaTime * 11.0f);
    scale = Math::Lerp(scale, targetScale, lerpRate);
    SetScale(scale);
    const Vector3 currentRotation = GetRotation();
    SetRotation({
        currentRotation.x + (targetRotation.x - currentRotation.x) * lerpRate,
        currentRotation.y,
        currentRotation.z + (targetRotation.z - currentRotation.z) * lerpRate
    });
}

void EnemyThunderSlime::SyncWorldCollisionRadius(float worldRadius) {
    const Vector3 scale = GetScale();
    const float maxScale = (std::max)({ 0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    SetCollisionRadius(worldRadius / maxScale);
}

void EnemyThunderSlime::SyncGroundCollisionRadius() {
    SyncWorldCollisionRadius(kGroundCollisionWorldRadius);
}

void EnemyThunderSlime::SyncThrownCollisionRadius() {
    SyncWorldCollisionRadius(kThrownCollisionWorldRadius);
}
