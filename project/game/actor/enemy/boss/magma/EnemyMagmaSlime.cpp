#define NOMINMAX
#include "game/actor/combat/CombatGroundQuery.h"
#include "game/actor/combat/CombatMath.h"
#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.h"
#include "game/actor/enemy/boss/core/BossChantVisual.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshRenderer.h"
#include "SceneManager.h"
#include "game/actor/enemy/animation/SlimeBounceAnimator.h"
#include "game/actor/enemy/animation/BossGroundHopVfx.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
constexpr float kLocomotionHopInterval = 0.70f;
constexpr float kLocomotionAnticipationDuration = 0.28f;
constexpr float kLocomotionHopPowerScale = 0.56f;
constexpr float kLocomotionLandingDuration = 0.28f;
constexpr const char* kMagmaMortarAttackId = "magma_mortar";
constexpr const char* kLavaRushAttackId = "lava_rush";
constexpr const char* kVolcanicSlamAttackId = "volcanic_slam";
constexpr const char* kEruptionFieldAttackId = "eruption_field";
constexpr const char* kLavaSpiralAttackId = "lava_spiral";
constexpr const char* kFireColumnAttackId = "fire_column";
constexpr const char* kMagmaFloodAttackId = "magma_flood";
constexpr const char* kFireChantEffect = "Resources/json/effect/effect_boss_chant_fire.json";
constexpr const char* kFireColumnLinkEffect = "Resources/json/effect/effect_magma_fire_column_link.json";
constexpr const char* kMagmaChantEffect = "Resources/json/effect/effect_boss_chant_magma.json";
constexpr const char* kFireChantMotesPreset = "boss_chant_fire_motes";
constexpr const char* kFireColumnStreamPreset = "magma_slime_fire_column_stream";
constexpr const char* kFloodSplashPreset = "magma_slime_flood_splash";

constexpr const char* kCoreEmberPreset = "magma_slime_core_embers";
constexpr const char* kChargePreset = "magma_slime_charge";
constexpr const char* kMortarTrailPreset = "magma_slime_mortar_trail";
constexpr const char* kImpactPreset = "magma_slime_impact";
constexpr const char* kRushWakePreset = "magma_slime_rush_wake";
constexpr const char* kRushSplashPreset = "magma_slime_rush_splash";
constexpr const char* kSlamBurstPreset = "magma_slime_slam_burst";
constexpr const char* kGeyserBurstPreset = "magma_slime_geyser_burst";
constexpr const char* kGeyserStreamPreset = "magma_slime_geyser_stream";
constexpr const char* kSpiralSurgePreset = "magma_slime_spiral_surge";
constexpr const char* kSpiralTrailPreset = "magma_slime_spiral_trail";
constexpr const char* kEncounterAppearanceSequence = "magma_midboss_appear_cue";

constexpr int kMagmaMaterialType = 9;
constexpr int kFireMaterialType = 11;
constexpr int kShockwaveMaterialType = 14;
constexpr float kPi = 3.1415926535f;
constexpr float kMagmaSlimeModelYawOffset = kPi;
constexpr float kGroundCollisionWorldRadius = 3.65f;
constexpr float kMortarInterval = 0.085f;
constexpr float kMortarTrailInterval = 0.055f;
constexpr float kRushWakeInterval = 0.045f;
constexpr float kRushPoolInterval = 0.115f;
constexpr float kEruptionInterval = 0.135f;
constexpr float kPoolDamageInterval = 0.72f;
constexpr float kPoolVerticalTolerance = 2.8f;
constexpr int kEncounterControlledActionMode = 1;
constexpr float kDefaultEncounterAppearanceDuration = 1.16f;
constexpr float kEncounterAppearanceRise = 2.35f;

using CombatMath::NormalizePlanar;

using CombatMath::PlanarDistance;

using CombatMath::SmoothStep01;

using CombatMath::EaseOutCubic;

using CombatMath::LerpVector3;

StatusEffectApplication MakeBurningStatus(
    float duration,
    float tickInterval,
    float tickDamage,
    const std::string& vfxPreset) {
    StatusEffectApplication status;
    status.type = StatusEffectType::Burning;
    status.duration = duration;
    status.tickInterval = tickInterval;
    status.tickDamage = tickDamage;
    status.vfxPreset = vfxPreset;
    return status;
}
}

EnemyMagmaSlime::EnemyMagmaSlime() = default;
EnemyMagmaSlime::~EnemyMagmaSlime() = default;

void EnemyMagmaSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_MagmaSlime");
    SetEnemyType("MagmaSlime");
    ReloadAttackProfile();

    // 本体はOBJ内の溶岩・黒曜石・発光亀裂PBR材質をそのまま使います。
    SetMaterialType(0);
    SetColor({ 1.0f, 0.94f, 0.86f, 1.0f });
    SetEmissive(1.18f);
    SetMetallic(0.12f);
    SetRoughness(0.26f);
    SetEnableEnvMap(true);
    SetEnvIntensity(0.42f);
    defaultColor_ = GetColor();

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncCollisionRadius();
}

void EnemyMagmaSlime::Update(float deltaTime) {
    if (UpdateEncounterState(deltaTime)) {
        return;
    }

    UpdateMagmaBlobs(deltaTime);
    UpdateMagmaPools(deltaTime);
    UpdateMagmaRings(deltaTime);
    UpdateMagmaPillars(deltaTime);
    UpdateMagmaSurges(deltaTime);
    UpdateMagmaFlood(deltaTime);

    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        ClearTransientVisuals();
        BaseEnemy::Update(deltaTime);
        return;
    }

    if (IsDormant() || IsThrowRecovering() || ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return;
    }

    EnsureBaseScale();
    idleTimer_ += deltaTime;
    locomotionLandingTimer_ = (std::max)(0.0f, locomotionLandingTimer_ - deltaTime);
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    landingPulseTimer_ = (std::max)(0.0f, landingPulseTimer_ - deltaTime);

    Vector3 targetDirection = lockedDirection_;
    float targetDistance = 9999.0f;
    if (target_) {
        const Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        targetDirection = NormalizePlanar(toTarget);
        targetDistance = PlanarDistance(target_->GetWorldPosition(), GetWorldPosition());
    }
    UpdateBossTargetEngagement(targetDistance);

    const Vector3 previousVelocity = GetVelocity();
    Vector3 velocity = previousVelocity;
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    UpdateBehavior(deltaTime, targetDirection, targetDistance, velocity);
    const float locomotionIntentSpeed = SlimeBounceAnimator::PlanarSpeed(velocity);
    if (attackState_ == AttackState::Idle && target_ && param_.has_value()) {
        velocity = CombatMath::SmoothPlanarVelocity(previousVelocity, velocity, deltaTime, 5.0f);
        const Vector3 candidate = GetTranslate() + Vector3{ velocity.x * 0.42f, 0.0f, velocity.z * 0.42f };
        if (isGrounded_ && !CombatGroundQuery::CanWalkTo(
            GetTranslate(), candidate, this, { 4.0f, 12.0f }, 1.2f)) {
            velocity.x = 0.0f;
            velocity.z = 0.0f;
        }
    }
    if (attackState_ == AttackState::Idle) {
        if (locomotionAirborne_ && isGrounded_) {
            locomotionLandingTimer_ = kLocomotionLandingDuration;
            locomotionAirborne_ = false;
            BossGroundHopVfx::EmitPuff(
                CombatGroundQuery::FindGroundPoint(GetTranslate(), this, { 4.0f, 12.0f }, GetTranslate().y) + Vector3{ 0.0f, 0.04f, 0.0f },
                { 1.0f, 0.52f, 0.18f, 0.78f }, 2.6f, true);
        }
        if (SlimeBounceAnimator::StepBossGroundHop(locomotionHopTimer_, velocity,
            locomotionIntentSpeed, isGrounded_ && param_.has_value(), deltaTime,
            kLocomotionHopInterval, kLocomotionAnticipationDuration, 0.22f)) {
            velocity.y = (std::max)(velocity.y,
                std::clamp(param_->jumpPower * kLocomotionHopPowerScale, 9.0f, 15.0f));
            locomotionAirborne_ = true;
            BossGroundHopVfx::EmitPuff(
                CombatGroundQuery::FindGroundPoint(GetTranslate(), this, { 4.0f, 12.0f }, GetTranslate().y) + Vector3{ 0.0f, 0.04f, 0.0f },
                { 1.0f, 0.52f, 0.18f, 0.62f }, 2.3f, false);
        }
    } else {
        locomotionHopTimer_ = 0.0f;
        locomotionLandingTimer_ = 0.0f;
        locomotionAirborne_ = false;
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    UpdateBodyHeat(deltaTime);
    SyncCollisionRadius();
    BaseEnemy::Update(deltaTime);
    UpdateChantVisual(deltaTime);
}

void EnemyMagmaSlime::Draw(
    ID3D12Resource* pointLightResource,
    ID3D12Resource* spotLightResource) {
    BaseEnemy::Draw(pointLightResource, spotLightResource);
    if (chantVisual_) {
        chantVisual_->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& sigil : fireColumnSigils_) {
        if (sigil && sigil->GetIsVisible()) {
            sigil->RefreshRenderCameraData();
            sigil->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const auto& link : fireColumnLinks_) {
        if (link && link->GetIsVisible()) {
            link->RefreshRenderCameraData();
            link->Draw(pointLightResource, spotLightResource);
        }
    }
}

void EnemyMagmaSlime::OnSwitchEvent(bool active) {
    if (!IsEncounterControlled()) {
        BaseEnemy::OnSwitchEvent(active);
        return;
    }

    encounterRequestedActive_ = active;
    if (!encounterInitializedForPlay_) {
        InitializeEncounterState();
    }
    if (active && encounterState_ == EncounterState::Dormant) {
        BeginEncounterAppearance();
    } else if (!active && encounterState_ != EncounterState::Dormant) {
        ApplyDormantEncounterState();
    }
}

std::unique_ptr<Object3d> EnemyMagmaSlime::Clone() const {
    auto clone = std::make_unique<EnemyMagmaSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    clone->baseScale_ = baseScale_;
    clone->hasBaseScale_ = true;
    return clone;
}

void EnemyMagmaSlime::ApplyManagedScale(const Vector3& scale) {
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
    SyncCollisionRadius();
}

bool EnemyMagmaSlime::HasOwnedSpecialMaterialVisuals() const {
    if (!GetIsVisible()) {
        return false;
    }
    return !magmaBlobs_.empty() || !magmaPools_.empty() || !magmaRings_.empty() ||
        !magmaPillars_.empty() || !magmaSurges_.empty() || !magmaFloodCells_.empty() ||
        rushFlameVisuals_[0] != nullptr || fireColumnVisuals_[0] != nullptr;
}

void EnemyMagmaSlime::DrawOwnedSpecialMaterialVisuals(
    uint32_t depthSrvHandle,
    uint32_t grabSrvHandle) {
    for (const MagmaBlob& blob : magmaBlobs_) {
        if (blob.visual && blob.visual->GetIsVisible()) {
            blob.visual->RefreshRenderCameraData();
            blob.visual->DrawMagma(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const MagmaPool& pool : magmaPools_) {
        if (pool.visual && pool.visual->GetIsVisible()) {
            pool.visual->RefreshRenderCameraData();
            pool.visual->DrawMagma(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const MagmaRing& ring : magmaRings_) {
        if (ring.visual && ring.visual->GetIsVisible()) {
            ring.visual->RefreshRenderCameraData();
            ring.visual->DrawShockwave(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const MagmaPillar& pillar : magmaPillars_) {
        if (pillar.visual && pillar.visual->GetIsVisible()) {
            pillar.visual->RefreshRenderCameraData();
            pillar.visual->DrawFire(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const MagmaSurge& surge : magmaSurges_) {
        if (surge.visual && surge.visual->GetIsVisible()) {
            surge.visual->RefreshRenderCameraData();
            surge.visual->DrawMagma(depthSrvHandle, grabSrvHandle);
        }
        if (surge.crestVisual && surge.crestVisual->GetIsVisible()) {
            surge.crestVisual->RefreshRenderCameraData();
            surge.crestVisual->DrawFire(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const std::unique_ptr<Object3d>& flame : rushFlameVisuals_) {
        if (flame && flame->GetIsVisible()) {
            flame->RefreshRenderCameraData();
            flame->DrawFire(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const std::unique_ptr<Object3d>& flame : fireColumnVisuals_) {
        if (flame && flame->GetIsVisible()) {
            flame->RefreshRenderCameraData();
            flame->DrawFire(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const MagmaFloodCell& cell : magmaFloodCells_) {
        if (cell.surface && cell.surface->GetIsVisible()) {
            cell.surface->RefreshRenderCameraData();
            cell.surface->DrawMagma(depthSrvHandle, grabSrvHandle);
        }
        if (cell.crest && cell.crest->GetIsVisible()) {
            cell.crest->RefreshRenderCameraData();
            cell.crest->DrawMagma(depthSrvHandle, grabSrvHandle);
        }
    }
}

void EnemyMagmaSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    debugPreviewAttackId_ = attackId;
    currentAttack_ = AttackKind::None;
    attackState_ = AttackState::Idle;
    attackCooldown_ = 0.05f;
    attackTimer_ = 0.0f;
    HideAttackTelegraph();
    ClearTransientVisuals();
}

const char* EnemyMagmaSlime::GetDebugAttackPhaseName() const {
    switch (attackState_) {
    case AttackState::Windup: return "予兆";
    case AttackState::Active: return "攻撃";
    case AttackState::Recovery: return "硬直";
    default: return "待機";
    }
}

void EnemyMagmaSlime::EnsureBaseScale() {
    if (hasBaseScale_) {
        return;
    }
    baseScale_ = GetScale();
    hasBaseScale_ = true;
}

void EnemyMagmaSlime::UpdateBehavior(
    float deltaTime,
    const Vector3& targetDirection,
    float targetDistance,
    Vector3& velocity) {
    if (attackState_ != AttackState::Idle) {
        UpdateAttack(deltaTime, targetDirection, targetDistance, velocity);
        return;
    }

    if (!target_ || !param_.has_value()) {
        const float verticalVelocity = velocity.y;
        velocity = CalculateWanderVelocity(deltaTime, 0.48f, 0.38f);
        velocity.y = verticalVelocity;
        UpdateFacing(velocity);
        return;
    }

    UpdateFacing(targetDirection);
    if (UpdateNoticeReaction(deltaTime, targetDistance, detectionRange_, targetDirection)) {
        return;
    }

    const AttackKind debugAttack = ResolveDebugAttack();
    const AttackKind plannedAttack = debugAttack != AttackKind::None
        ? debugAttack
        : ResolveAutomaticAttack(targetDistance);
    const EnemyAttackDefinition& planned = GetAttackDefinition(GetAttackId(plannedAttack));
    const bool inRange = targetDistance >= planned.minRange && targetDistance <= planned.maxRange;
    if ((!locomotionAirborne_ || isGrounded_) && IsBossTargetEngaged() && attackCooldown_ <= 0.0f && inRange) {
        StartAttack(plannedAttack, targetDirection);
        return;
    }

    // 射程外では次の技を待ち続けず、攻撃できる距離まで素早く近づきます。
    const float moveSpeed = targetDistance > planned.maxRange
        ? (std::max)(5.5f, param_->speed * 3.0f)
        : (std::max)(0.70f, param_->speed * 0.82f);
    const float preferredDistance = (std::clamp)(planned.recommendedTargetDistance, 4.5f, 11.0f);
    const Vector3 spacing = CombatMath::MakePacedSpacingVelocity(
        targetDirection, targetDistance, preferredDistance - 1.2f,
        preferredDistance + 1.4f, moveSpeed, 0.48f, 0.36f,
        idleTimer_, 6.2f);
    velocity.x = spacing.x;
    velocity.z = spacing.z;
}

void EnemyMagmaSlime::StartAttack(AttackKind kind, const Vector3& targetDirection) {
    if (kind == AttackKind::None) {
        return;
    }

    currentAttack_ = kind;
    attackState_ = AttackState::Windup;
    lockedDirection_ = NormalizePlanar(targetDirection);
    lockedTargetPosition_ = target_
        ? FindGroundPoint(target_->GetWorldPosition())
        : FindGroundPoint(GetTranslate() + lockedDirection_ * 7.0f);
    attackStartPosition_ = GetTranslate();
    attackEndPosition_ = attackStartPosition_;

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackStateDuration_ = (std::max)(0.12f, attack.windupDuration);
    attackTimer_ = attackStateDuration_;
    actionTimer_ = 0.0f;
    effectTimer_ = 0.0f;
    actionIndex_ = 0;
    warningTriggered_ = false;
    attackDamageApplied_ = false;
    rushImpactSpawned_ = false;
    slamImpactSpawned_ = false;

    if (kind == AttackKind::MagmaMortar) {
        mortarPatternPhase_ = std::fmod(
            idleTimer_ * 1.731f + static_cast<float>(automaticAttackSerial_) * 0.917f,
            kPi * 2.0f);
        PrepareMortarTargets();
    } else if (kind == AttackKind::EruptionField) {
        PrepareEruptionTargets();
    }
    if (kind == AttackKind::FireColumn) {
        PrepareFireColumnTargets();
        CreateFireColumnSigils();
    }
    // 突進とプレス以外の術式には、共通の術者中心の詠唱演出を使います。
    if (kind == AttackKind::MagmaMortar || kind == AttackKind::EruptionField ||
        kind == AttackKind::LavaSpiral || kind == AttackKind::FireColumn ||
        kind == AttackKind::MagmaFlood) {
        BossChantStyle style;
        style.sigilEffectPath = kind == AttackKind::FireColumn
            ? kFireChantEffect : kMagmaChantEffect;
        style.motePreset = kFireChantMotesPreset;
        style.releasePreset = kind == AttackKind::MagmaFlood ? kFloodSplashPreset : "";
        style.accentColor = kind == AttackKind::FireColumn
            ? Vector4{ 1.0f, 0.42f, 0.08f, 1.0f }
            : Vector4{ 0.95f, 0.20f, 0.035f, 1.0f };
        style.radius = std::clamp(baseScale_.x * 1.35f, 2.4f, 4.0f);
        style.fadeStart = (std::max)(0.0f, attack.activeDuration - 0.22f);
        chantVisual_ = std::make_unique<BossChantVisual>();
        if (!chantVisual_->Start(common_, std::move(style))) {
            chantVisual_.reset();
        }
    }
    if (debugPreviewAttackId_.empty()) {
        lastAutomaticAttack_ = kind;
        ++attackCycleIndex_;
        ++automaticAttackSerial_;
    }
}

void EnemyMagmaSlime::UpdateAttack(
    float deltaTime,
    const Vector3& targetDirection,
    float targetDistance,
    Vector3& velocity) {
    if (attackState_ == AttackState::Windup) {
        if (attackTimer_ > GetCurrentAttackDefinition().warningLeadTime && target_) {
            lockedDirection_ = NormalizePlanar(targetDirection);
            lockedTargetPosition_ = FindGroundPoint(target_->GetWorldPosition());
            if (currentAttack_ == AttackKind::MagmaMortar) {
                PrepareMortarTargets();
            } else if (currentAttack_ == AttackKind::EruptionField) {
                PrepareEruptionTargets();
            }
        }
        UpdateFacing(lockedDirection_);
        UpdateWindup(deltaTime, targetDirection);
        return;
    }
    if (attackState_ == AttackState::Active) {
        UpdateActive(deltaTime, targetDistance, velocity);
        return;
    }
    if (attackState_ == AttackState::Recovery) {
        attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        HideAttackTelegraph();
        if (attackTimer_ <= 0.0f) {
            FinishAttack();
        }
    }
}

void EnemyMagmaSlime::UpdateWindup(float deltaTime, const Vector3&) {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    effectTimer_ -= deltaTime;
    const float progress = 1.0f - std::clamp(
        attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
    const Vector4 warningColor = { 1.0f, 0.19f, 0.015f, 0.84f };

    switch (currentAttack_) {
    case AttackKind::MagmaMortar:
        ShowAttackTelegraphImpactAreas(
            mortarTargets_.data(), mortarTargets_.size(), attack.radius, progress, warningColor);
        break;
    case AttackKind::LavaRush:
        ShowAttackTelegraphLine(
            GetTranslate(), lockedDirection_, attack.maxRange, attack.radius * 2.0f, progress, warningColor);
        break;
    case AttackKind::VolcanicSlam:
        ShowAttackTelegraphCircle(lockedTargetPosition_, attack.radius, progress, warningColor);
        break;
    case AttackKind::EruptionField:
        ShowAttackTelegraphImpactAreas(
            eruptionTargets_.data(), eruptionTargets_.size(), attack.radius, progress, warningColor);
        break;
    case AttackKind::LavaSpiral:
        ShowAttackTelegraphCircle(GetTranslate(), attack.maxRange, progress, warningColor);
        break;
    case AttackKind::FireColumn:
        break;
    case AttackKind::MagmaFlood:
        ShowAttackTelegraphLine(GetTranslate(), lockedDirection_, attack.maxRange,
            attack.radius * 2.0f, progress, warningColor);
        break;
    default:
        break;
    }

    if (currentAttack_ == AttackKind::FireColumn) {
        PrepareFireColumnTargets();
        UpdateFireColumnSigils(deltaTime);
    }

    if (effectTimer_ <= 0.0f && currentAttack_ != AttackKind::FireColumn) {
        const char* preset = attack.windupVfx.empty() ? kChargePreset : attack.windupVfx.c_str();
        EmitDirectedPreset(
            preset,
            GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.55f, 0.0f },
            { -lockedDirection_.x * 0.18f, 1.0f, -lockedDirection_.z * 0.18f },
            0.82f + progress * 0.72f);
        effectTimer_ += (std::max)(0.055f, 0.14f - progress * 0.075f);
    }

    if (!warningTriggered_ && attackTimer_ <= attack.warningLeadTime) {
        if (currentAttack_ != AttackKind::FireColumn) {
            TriggerAttackTelegraphCue({ 1.0f, 0.18f, 0.015f, 1.0f });
        }
        warningTriggered_ = true;
    }
    if (attackTimer_ <= 0.0f) {
        BeginActive();
    }
}

void EnemyMagmaSlime::BeginActive() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackState_ = AttackState::Active;
    attackStateDuration_ = (std::max)(0.16f, attack.activeDuration);
    attackTimer_ = attackStateDuration_;
    actionTimer_ = 0.0f;
    effectTimer_ = 0.0f;
    actionIndex_ = 0;
    HideAttackTelegraph();

    if (currentAttack_ == AttackKind::LavaRush) {
        BeginRush();
    } else if (currentAttack_ == AttackKind::VolcanicSlam) {
        BeginVolcanicSlam();
    } else if (currentAttack_ == AttackKind::LavaSpiral) {
        BeginLavaSpiral();
    } else if (currentAttack_ == AttackKind::FireColumn) {
        BeginFireColumn();
    } else if (currentAttack_ == AttackKind::MagmaFlood) {
        BeginMagmaFlood();
    }
}

void EnemyMagmaSlime::UpdateActive(
    float deltaTime,
    float targetDistance,
    Vector3& velocity) {
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    actionTimer_ -= deltaTime;
    effectTimer_ -= deltaTime;

    switch (currentAttack_) {
    case AttackKind::MagmaMortar:
        while (actionIndex_ < static_cast<int>(mortarTargets_.size()) && actionTimer_ <= 0.0f) {
            LaunchNextMagmaBlob();
            ++actionIndex_;
            actionTimer_ += kMortarInterval;
        }
        break;
    case AttackKind::LavaRush:
        UpdateRush(deltaTime, targetDistance, velocity);
        break;
    case AttackKind::VolcanicSlam:
        UpdateVolcanicSlam(deltaTime, velocity);
        break;
    case AttackKind::EruptionField:
        while (actionIndex_ < static_cast<int>(eruptionTargets_.size()) && actionTimer_ <= 0.0f) {
            SpawnNextMagmaPillar();
            ++actionIndex_;
            actionTimer_ += kEruptionInterval;
        }
        break;
    case AttackKind::LavaSpiral:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        break;
    case AttackKind::FireColumn:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateFireColumn(deltaTime);
        break;
    case AttackKind::MagmaFlood:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        break;
    default:
        break;
    }

    if (attackTimer_ <= 0.0f) {
        BeginRecovery();
    }
}

void EnemyMagmaSlime::BeginRecovery() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackState_ = AttackState::Recovery;
    attackStateDuration_ = (std::max)(0.10f, attack.recoveryDuration);
    attackTimer_ = attackStateDuration_;
    attackCooldown_ = (std::max)(0.22f, attack.cooldown);
    landingPulseTimer_ = 0.34f;
    SetGrounded(true);
    HideAttackTelegraph();
    for (auto& flame : fireColumnVisuals_) flame.reset();
    for (auto& sigil : fireColumnSigils_) sigil.reset();
    for (auto& link : fireColumnLinks_) link.reset();
}

void EnemyMagmaSlime::FinishAttack() {
    currentAttack_ = AttackKind::None;
    attackState_ = AttackState::Idle;
    attackTimer_ = 0.0f;
    actionTimer_ = 0.0f;
    effectTimer_ = 0.0f;
    actionIndex_ = 0;
    warningTriggered_ = false;
    attackDamageApplied_ = false;
    rushImpactSpawned_ = false;
    slamImpactSpawned_ = false;
    chantVisual_.reset();
}

void EnemyMagmaSlime::PrepareAttackCycle() {
    attackCycle_ = {
        AttackKind::MagmaMortar, AttackKind::LavaRush, AttackKind::VolcanicSlam,
        AttackKind::EruptionField, AttackKind::LavaSpiral, AttackKind::FireColumn,
        AttackKind::MagmaFlood,
    };
    std::shuffle(attackCycle_.begin(), attackCycle_.end(), attackRandom_);
    if (attackCycle_[0] == lastAutomaticAttack_) {
        std::swap(attackCycle_[0], attackCycle_[1]);
    }
    attackCycleIndex_ = 0;
}

EnemyMagmaSlime::AttackKind EnemyMagmaSlime::ResolveAutomaticAttack(float targetDistance) {
    if (attackCycleIndex_ >= attackCycle_.size()) {
        PrepareAttackCycle();
    }
    for (std::size_t index = attackCycleIndex_; index < attackCycle_.size(); ++index) {
        if (attackCycleIndex_ == 0 && attackCycle_[index] == lastAutomaticAttack_) {
            continue;
        }
        const EnemyAttackDefinition& attack = GetAttackDefinition(GetAttackId(attackCycle_[index]));
        if (targetDistance >= attack.minRange && targetDistance <= attack.maxRange) {
            std::swap(attackCycle_[attackCycleIndex_], attackCycle_[index]);
            break;
        }
    }
    return attackCycle_[attackCycleIndex_];
}

EnemyMagmaSlime::AttackKind EnemyMagmaSlime::ResolveDebugAttack() const {
    if (debugPreviewAttackId_ == kMagmaMortarAttackId) return AttackKind::MagmaMortar;
    if (debugPreviewAttackId_ == kLavaRushAttackId) return AttackKind::LavaRush;
    if (debugPreviewAttackId_ == kVolcanicSlamAttackId) return AttackKind::VolcanicSlam;
    if (debugPreviewAttackId_ == kEruptionFieldAttackId) return AttackKind::EruptionField;
    if (debugPreviewAttackId_ == kLavaSpiralAttackId) return AttackKind::LavaSpiral;
    if (debugPreviewAttackId_ == kFireColumnAttackId) return AttackKind::FireColumn;
    if (debugPreviewAttackId_ == kMagmaFloodAttackId) return AttackKind::MagmaFlood;
    return AttackKind::None;
}

const char* EnemyMagmaSlime::GetAttackId(AttackKind kind) const {
    switch (kind) {
    case AttackKind::MagmaMortar: return kMagmaMortarAttackId;
    case AttackKind::LavaRush: return kLavaRushAttackId;
    case AttackKind::VolcanicSlam: return kVolcanicSlamAttackId;
    case AttackKind::EruptionField: return kEruptionFieldAttackId;
    case AttackKind::LavaSpiral: return kLavaSpiralAttackId;
    case AttackKind::FireColumn: return kFireColumnAttackId;
    case AttackKind::MagmaFlood: return kMagmaFloodAttackId;
    default: return "";
    }
}

const EnemyAttackDefinition& EnemyMagmaSlime::GetCurrentAttackDefinition() const {
    return GetAttackDefinition(GetAttackId(currentAttack_));
}

void EnemyMagmaSlime::PrepareMortarTargets() {
    constexpr float kGoldenAngle = 2.39996323f;
    const Vector3 origin = FindGroundPoint(GetTranslate());
    const Vector3 spreadCenter = LerpVector3(origin, lockedTargetPosition_, 0.54f);
    for (std::size_t index = 0; index < mortarTargets_.size(); ++index) {
        const float indexValue = static_cast<float>(index + 1);
        const float rawNoise = std::sin(
            indexValue * 12.9898f + mortarPatternPhase_ * 31.733f) * 43758.5453f;
        const float random01 = rawNoise - std::floor(rawNoise);
        const float secondaryNoise = std::sin(
            indexValue * 43.133f + mortarPatternPhase_ * 7.913f) * 15731.743f;
        const float secondary01 = secondaryNoise - std::floor(secondaryNoise);
        const float angle = mortarPatternPhase_ + kGoldenAngle * static_cast<float>(index) +
            (random01 - 0.5f) * 1.08f;
        const float radius = 2.8f + random01 * 5.7f + secondary01 * 1.35f;
        const Vector3 scatterDirection = { std::sin(angle), 0.0f, std::cos(angle) };
        mortarTargets_[index] = FindGroundPoint(spreadCenter + scatterDirection * radius);
    }
}

void EnemyMagmaSlime::LaunchNextMagmaBlob() {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(mortarTargets_.size())) {
        return;
    }

    MagmaBlob blob;
    blob.visual = CreateMagmaVisual(
        "MagmaSlime_Mortar_" + std::to_string(actionIndex_),
        "Primitives/sphere",
        kMagmaMaterialType,
        { 1.0f, 0.16f, 0.008f, 0.97f },
        2.6f);
    if (!blob.visual) {
        return;
    }

    const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };
    const int withinWave = actionIndex_ % 5;
    const int wave = actionIndex_ / 5;
    const float sideOffset = (static_cast<float>(withinWave) - 2.0f) * 0.46f;
    blob.start = GetWorldPosition() + side * sideOffset +
        Vector3{ 0.0f, baseScale_.y * 0.68f + 0.72f + static_cast<float>(wave) * 0.18f, 0.0f };
    blob.target = mortarTargets_[static_cast<std::size_t>(actionIndex_)];
    const Vector3 launchDirection = NormalizePlanar(blob.target - blob.start);
    blob.control = blob.start + launchDirection * (2.2f + static_cast<float>(wave) * 0.55f) +
        Vector3{ 0.0f, 21.0f + static_cast<float>(wave) * 1.65f, 0.0f };
    blob.flightDuration = 1.18f + static_cast<float>(withinWave) * 0.04f + static_cast<float>(wave) * 0.10f;
    blob.visual->SetTranslate(blob.start);
    blob.visual->SetScale({ 0.92f, 0.92f, 0.92f });
    blob.visual->UpdateWorldMatrix(true);

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const char* activePreset = attack.activeVfx.empty() ? kMortarTrailPreset : attack.activeVfx.c_str();
    EmitDirectedPreset(activePreset, blob.start, lockedDirection_ + Vector3{ 0.0f, 1.0f, 0.0f }, 1.18f);
    magmaBlobs_.push_back(std::move(blob));
}

void EnemyMagmaSlime::UpdateMagmaBlobs(float deltaTime) {
    auto blob = magmaBlobs_.begin();
    while (blob != magmaBlobs_.end()) {
        if (!blob->visual) {
            blob = magmaBlobs_.erase(blob);
            continue;
        }

        blob->age += (std::max)(deltaTime, 0.0f);
        const float progress = std::clamp(blob->age / (std::max)(blob->flightDuration, 0.01f), 0.0f, 1.0f);
        const float inverse = 1.0f - progress;
        Vector3 position = blob->start * (inverse * inverse) +
            blob->control * (2.0f * inverse * progress) +
            blob->target * (progress * progress);
        blob->spin += deltaTime * (7.0f + progress * 3.0f);
        const float pulse = 1.0f + std::sin(blob->age * 18.0f) * 0.075f;
        blob->visual->SetTranslate(position);
        blob->visual->SetRotation({ blob->spin * 0.72f, blob->spin, blob->spin * 0.36f });
        blob->visual->SetScale({ 0.96f * pulse, 0.96f / pulse, 0.96f * pulse });
        blob->visual->SetColor({ 1.0f, 0.12f + progress * 0.10f, 0.006f, 0.98f });
        blob->visual->Update(deltaTime);
        blob->visual->UpdateWorldMatrix(true);

        if (std::fmod(blob->age, kMortarTrailInterval) < deltaTime) {
            EmitDirectedPreset(kMortarTrailPreset, position, NormalizePlanar(blob->target - blob->start) * -1.0f, 1.1f);
        }
        if (progress >= 1.0f && !blob->impacted) {
            ImpactMagmaBlob(*blob);
            blob->impacted = true;
        }
        if (blob->impacted) {
            blob = magmaBlobs_.erase(blob);
            continue;
        }
        ++blob;
    }
}

void EnemyMagmaSlime::ImpactMagmaBlob(MagmaBlob& blob) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kMagmaMortarAttackId);
    const char* impactPreset = attack.impactVfx.empty() ? kImpactPreset : attack.impactVfx.c_str();
    EmitDirectedPreset(impactPreset, blob.target + Vector3{ 0.0f, 0.12f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 1.48f);
    SpawnMagmaPool(blob.target, (std::max)(1.05f, attack.radius * 0.58f), 2.85f);
    DamageTargetAt(
        blob.target,
        attack.radius,
        attack.damage,
        { lockedDirection_.x * 4.0f, 5.4f, lockedDirection_.z * 4.0f },
        attack.statusDuration,
        attack.statusTickInterval,
        attack.statusTickDamage);
}

void EnemyMagmaSlime::SpawnMagmaPool(const Vector3& position, float radius, float lifetime) {
    MagmaPool pool;
    pool.visual = CreateMagmaVisual(
        "MagmaSlime_Pool_" + std::to_string(magmaPools_.size()),
        "Primitives/cylinder",
        kMagmaMaterialType,
        { 1.0f, 0.13f, 0.005f, 0.86f },
        2.2f);
    if (!pool.visual) {
        return;
    }
    pool.position = FindGroundPoint(position);
    pool.position.y += 0.055f;
    pool.radius = radius;
    pool.lifetime = lifetime;
    pool.damageTimer = 0.0f;
    pool.visual->SetTranslate(pool.position);
    pool.visual->SetScale({ radius * 0.32f, 0.045f, radius * 0.32f });
    pool.visual->UpdateWorldMatrix(true);
    magmaPools_.push_back(std::move(pool));
}

void EnemyMagmaSlime::UpdateMagmaPools(float deltaTime) {
    auto pool = magmaPools_.begin();
    while (pool != magmaPools_.end()) {
        if (!pool->visual) {
            pool = magmaPools_.erase(pool);
            continue;
        }

        pool->age += (std::max)(deltaTime, 0.0f);
        pool->damageTimer -= deltaTime;
        const float appear = EaseOutCubic(std::clamp(pool->age / 0.28f, 0.0f, 1.0f));
        const float fade = std::clamp((pool->lifetime - pool->age) / 0.62f, 0.0f, 1.0f);
        const float ripple = 1.0f + std::sin(pool->age * 4.8f) * 0.035f;
        pool->visual->SetScale({
            pool->radius * appear * ripple,
            0.055f,
            pool->radius * appear / ripple,
        });
        pool->visual->SetRotationY(pool->age * 0.26f);
        pool->visual->SetColor({ 1.0f, 0.11f + ripple * 0.05f, 0.004f, 0.82f * fade });
        pool->visual->SetIsVisible(fade > 0.01f);
        pool->visual->Update(deltaTime);
        pool->visual->UpdateWorldMatrix(true);

        if (pool->damageTimer <= 0.0f && target_) {
            const Vector3 targetPosition = target_->GetWorldPosition();
            if (std::abs(targetPosition.y - pool->position.y) <= kPoolVerticalTolerance &&
                PlanarDistance(targetPosition, pool->position) <= pool->radius + 0.55f) {
                DamageTargetAt(
                    pool->position,
                    pool->radius + 0.55f,
                    0.55f,
                    { 0.0f, 2.2f, 0.0f },
                    2.2f,
                    0.55f,
                    0.22f);
                pool->damageTimer = kPoolDamageInterval;
            }
        }

        if (pool->age >= pool->lifetime) {
            pool = magmaPools_.erase(pool);
            continue;
        }
        ++pool;
    }
}

void EnemyMagmaSlime::BeginRush() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackStartPosition_ = FindGroundPoint(GetTranslate());
    if (target_) {
        lockedTargetPosition_ = FindGroundPoint(target_->GetWorldPosition());
        lockedDirection_ = NormalizePlanar(lockedTargetPosition_ - attackStartPosition_);
    }
    float rushDistance = PlanarDistance(attackStartPosition_, lockedTargetPosition_) + 2.2f;
    rushDistance = std::clamp(rushDistance, 7.0f, (std::max)(7.0f, attack.maxRange));
    attackEndPosition_ = FindGroundPoint(attackStartPosition_ + lockedDirection_ * rushDistance);
    SetGrounded(true);
    SetRotationY(std::atan2(lockedDirection_.x, lockedDirection_.z) + kMagmaSlimeModelYawOffset);
    EnsureRushFlameVisuals();
    UpdateRushFlameVisuals(attackStartPosition_, 0.0f, 0.0f);
    EmitDirectedPreset(
        attack.activeVfx.empty() ? kRushWakePreset : attack.activeVfx.c_str(),
        attackStartPosition_ + Vector3{ 0.0f, 0.24f, 0.0f },
        lockedDirection_ * -1.0f,
        1.42f);
}

void EnemyMagmaSlime::UpdateRush(float deltaTime, float, Vector3& velocity) {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const float progress = 1.0f - std::clamp(
        attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
    const float travelRate = SmoothStep01(std::clamp(progress / 0.82f, 0.0f, 1.0f));
    Vector3 position = LerpVector3(attackStartPosition_, attackEndPosition_, travelRate);
    position.y += std::sin(travelRate * kPi) * 0.32f;
    SetTranslate(position);
    SetGrounded(true);
    velocity = { 0.0f, 0.0f, 0.0f };
    SetRotationY(std::atan2(lockedDirection_.x, lockedDirection_.z) + kMagmaSlimeModelYawOffset);
    UpdateRushFlameVisuals(position, progress, deltaTime);

    if (effectTimer_ <= 0.0f && progress <= 0.90f) {
        const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };
        EmitDirectedPreset(
            attack.activeVfx.empty() ? kRushWakePreset : attack.activeVfx.c_str(),
            position + Vector3{ 0.0f, 0.20f, 0.0f },
            lockedDirection_ * -1.0f + Vector3{ 0.0f, 0.18f, 0.0f },
            1.25f + progress * 0.52f);
        EmitDirectedPreset(
            kRushSplashPreset,
            position + side * attack.radius * 0.56f + Vector3{ 0.0f, 0.18f, 0.0f },
            side + Vector3{ 0.0f, 0.52f, 0.0f },
            1.15f + progress * 0.38f);
        EmitDirectedPreset(
            kRushSplashPreset,
            position - side * attack.radius * 0.56f + Vector3{ 0.0f, 0.18f, 0.0f },
            side * -1.0f + Vector3{ 0.0f, 0.52f, 0.0f },
            1.15f + progress * 0.38f);
        effectTimer_ += kRushWakeInterval;
    }

    if (actionTimer_ <= 0.0f && progress <= 0.84f) {
        SpawnMagmaPool(position, (std::max)(0.82f, attack.radius * 0.46f), 2.55f);
        actionTimer_ += kRushPoolInterval;
    }

    if (!attackDamageApplied_ && target_ &&
        PlanarDistance(target_->GetWorldPosition(), position) <= attack.radius + target_->GetCollisionRadius()) {
        DamageTargetAt(
            position,
            attack.radius + target_->GetCollisionRadius(),
        attack.damage,
        { lockedDirection_.x * 12.0f, 5.2f, lockedDirection_.z * 12.0f },
        attack.statusDuration,
        attack.statusTickInterval,
        attack.statusTickDamage);
        attackDamageApplied_ = true;
    }
    if (progress >= 0.84f && !rushImpactSpawned_) {
        FinishRush();
    }
}

void EnemyMagmaSlime::FinishRush() {
    rushImpactSpawned_ = true;
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    EmitDirectedPreset(
        attack.impactVfx.empty() ? kImpactPreset : attack.impactVfx.c_str(),
        attackEndPosition_ + Vector3{ 0.0f, 0.12f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        1.35f);
    const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };
    SpawnMagmaPool(attackEndPosition_, (std::max)(1.55f, attack.radius * 0.86f), 3.1f);
    SpawnMagmaPool(attackEndPosition_ + side * attack.radius * 0.88f, attack.radius * 0.48f, 2.5f);
    SpawnMagmaPool(attackEndPosition_ - side * attack.radius * 0.88f, attack.radius * 0.48f, 2.5f);
    EmitDirectedPreset(kRushSplashPreset, attackEndPosition_, side + Vector3{ 0.0f, 0.72f, 0.0f }, 1.62f);
    EmitDirectedPreset(kRushSplashPreset, attackEndPosition_, side * -1.0f + Vector3{ 0.0f, 0.72f, 0.0f }, 1.62f);
    ClearRushFlameVisuals();
}

void EnemyMagmaSlime::EnsureRushFlameVisuals() {
    for (std::size_t index = 0; index < rushFlameVisuals_.size(); ++index) {
        if (rushFlameVisuals_[index]) {
            continue;
        }
        rushFlameVisuals_[index] = CreateMagmaVisual(
            "MagmaSlime_RushFlame_" + std::to_string(index),
            "Primitives/sphere",
            kFireMaterialType,
            { 1.0f, 0.19f, 0.018f, 0.78f },
            2.9f);
        Object3d* flame = rushFlameVisuals_[index].get();
        if (!flame) {
            continue;
        }
        flame->SetIsVisible(false);
        if (MeshRenderer* renderer = flame->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                fire->effectType = 2.0f;
                fire->waveSpeed = 3.4f;
                fire->waveFrequency = 3.2f;
                fire->effectScale = 1.08f;
                fire->effectSoftness = 0.54f;
                fire->effectIntensity = 1.62f;
                fire->billboardScale = 0.92f;
                fire->effectScaleX = 1.08f;
                fire->effectScaleY = 0.86f;
            }
        }
    }
}

void EnemyMagmaSlime::UpdateRushFlameVisuals(
    const Vector3& position,
    float progress,
    float deltaTime) {
    constexpr std::array<float, 5> sideOffsets = { -0.74f, 0.0f, 0.74f, -0.42f, 0.42f };
    constexpr std::array<float, 5> backOffsets = { 0.20f, -0.08f, 0.20f, 0.92f, 0.92f };
    constexpr std::array<float, 5> heightOffsets = { 0.52f, 0.70f, 0.52f, 0.36f, 0.36f };
    constexpr std::array<float, 5> sizeMultipliers = { 1.04f, 1.34f, 1.04f, 0.82f, 0.82f };
    const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };
    const float horizontalScale = (std::max)(1.0f, (std::abs(baseScale_.x) + std::abs(baseScale_.z)) * 0.5f);
    const float verticalScale = (std::max)(1.0f, std::abs(baseScale_.y));
    const float speedPulse = 1.0f + std::sin(idleTimer_ * 22.0f) * 0.08f;
    const float envelope = SmoothStep01(std::clamp(progress / 0.10f, 0.0f, 1.0f)) *
        SmoothStep01(std::clamp((1.0f - progress) / 0.10f, 0.0f, 1.0f));

    for (std::size_t index = 0; index < rushFlameVisuals_.size(); ++index) {
        Object3d* flame = rushFlameVisuals_[index].get();
        if (!flame) {
            continue;
        }
        Vector3 flamePosition = position;
        flamePosition = flamePosition + side * (sideOffsets[index] * horizontalScale);
        flamePosition = flamePosition - lockedDirection_ * (backOffsets[index] * horizontalScale);
        flamePosition.y += heightOffsets[index] * verticalScale;
        const float scale = horizontalScale * sizeMultipliers[index] * speedPulse *
            (0.78f + envelope * 0.44f);
        flame->SetTranslate(flamePosition);
        flame->SetScale({ scale, scale * (0.88f + verticalScale * 0.08f), scale });
        flame->SetColor({
            1.0f,
            0.14f + envelope * 0.12f + static_cast<float>(index) * 0.008f,
            0.008f,
            0.52f + envelope * 0.34f,
        });
        flame->SetIsVisible(envelope > 0.01f || progress <= 0.02f);
        if (MeshRenderer* renderer = flame->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                fire->flowSpeedX = sideOffsets[index] * -0.18f;
                fire->flowSpeedY = 1.0f;
                fire->effectIntensity = 1.45f + envelope * 0.32f;
            }
        }
        flame->Update(deltaTime);
        flame->UpdateWorldMatrix(true);
    }
}

void EnemyMagmaSlime::ClearRushFlameVisuals() {
    for (std::unique_ptr<Object3d>& flame : rushFlameVisuals_) {
        flame.reset();
    }
}

void EnemyMagmaSlime::BeginVolcanicSlam() {
    attackStartPosition_ = GetTranslate();
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 toTarget = lockedTargetPosition_ - attackStartPosition_;
    const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (distance > attack.maxRange && distance > 0.001f) {
        lockedTargetPosition_ = attackStartPosition_ + NormalizePlanar(toTarget) * attack.maxRange;
    }
    attackEndPosition_ = FindGroundPoint(lockedTargetPosition_);
    SetGrounded(false);
    EmitDirectedPreset(
        attack.activeVfx.empty() ? kChargePreset : attack.activeVfx.c_str(),
        attackStartPosition_ + Vector3{ 0.0f, baseScale_.y * 0.55f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        1.46f);
}

void EnemyMagmaSlime::UpdateVolcanicSlam(float, Vector3& velocity) {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const float progress = 1.0f - std::clamp(
        attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
    const float travelRate = SmoothStep01(progress);
    Vector3 position = LerpVector3(attackStartPosition_, attackEndPosition_, travelRate);
    position.y += std::sin(progress * kPi) * (6.8f + PlanarDistance(attackStartPosition_, attackEndPosition_) * 0.08f);
    SetTranslate(position);
    velocity = { 0.0f, 0.0f, 0.0f };
    SetGrounded(progress >= 0.93f);

    if (effectTimer_ <= 0.0f && progress < 0.90f) {
        EmitDirectedPreset(
            attack.activeVfx.empty() ? kMortarTrailPreset : attack.activeVfx.c_str(),
            position + Vector3{ 0.0f, 0.20f, 0.0f },
            { -lockedDirection_.x * 0.22f, -1.0f, -lockedDirection_.z * 0.22f },
            1.18f);
        effectTimer_ += 0.058f;
    }
    if (progress >= 0.92f && !slamImpactSpawned_) {
        ImpactVolcanicSlam();
    }
}

void EnemyMagmaSlime::ImpactVolcanicSlam() {
    slamImpactSpawned_ = true;
    SetTranslate(attackEndPosition_);
    SetGrounded(true);
    landingPulseTimer_ = 0.42f;
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    EmitDirectedPreset(
        attack.impactVfx.empty() ? kSlamBurstPreset : attack.impactVfx.c_str(),
        attackEndPosition_ + Vector3{ 0.0f, 0.15f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        1.52f);
    SpawnMagmaPool(attackEndPosition_, (std::max)(1.7f, attack.radius * 0.52f), 3.0f);
    SpawnSlamRings(attackEndPosition_);
    for (int ringIndex = 0; ringIndex < 2; ++ringIndex) {
        const float eruptionRadius = ringIndex == 0 ? 3.7f : 6.4f;
        const float angleOffset = ringIndex == 0 ? 0.0f : kPi / 6.0f;
        for (int index = 0; index < 6; ++index) {
            const float angle = angleOffset + static_cast<float>(index) * kPi / 3.0f;
            const Vector3 direction = { std::sin(angle), 0.0f, std::cos(angle) };
            SpawnMagmaPillar(
                attackEndPosition_ + direction * eruptionRadius,
                1.15f + static_cast<float>(ringIndex) * 0.18f,
                5.8f + static_cast<float>(ringIndex) * 1.15f,
                0.84f,
                static_cast<float>(ringIndex) * 0.13f + static_cast<float>(index) * 0.035f,
                kVolcanicSlamAttackId);
        }
    }
    DamageTargetAt(
        attackEndPosition_,
        (std::max)(2.0f, attack.radius * 0.62f),
        attack.damage,
        { lockedDirection_.x * 6.0f, 8.0f, lockedDirection_.z * 6.0f },
        attack.statusDuration,
        attack.statusTickInterval,
        attack.statusTickDamage);
}

void EnemyMagmaSlime::SpawnSlamRings(const Vector3& position) {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    constexpr std::array<float, 3> delays = { 0.0f, 0.14f, 0.29f };
    for (std::size_t index = 0; index < delays.size(); ++index) {
        MagmaRing ring;
        ring.visual = CreateMagmaVisual(
            "MagmaSlime_Shockwave_" + std::to_string(index),
            "Primitives/cylinder",
            kShockwaveMaterialType,
            { 1.0f, 0.22f, 0.015f, 0.82f },
            2.4f);
        if (!ring.visual) {
            continue;
        }
        ring.position = FindGroundPoint(position);
        ring.position.y += 0.065f + static_cast<float>(index) * 0.012f;
        ring.delay = delays[index];
        ring.lifetime = 0.72f + static_cast<float>(index) * 0.06f;
        ring.startRadius = 0.75f + static_cast<float>(index) * 0.25f;
        ring.endRadius = (std::max)(6.5f, attack.radius * 1.65f) + static_cast<float>(index) * 1.35f;
        ring.previousRadius = ring.startRadius;
        ring.visual->SetTranslate(ring.position);
        ring.visual->SetScale({ ring.startRadius, 0.055f, ring.startRadius });
        ring.visual->SetIsVisible(index == 0);
        ring.visual->UpdateWorldMatrix(true);
        magmaRings_.push_back(std::move(ring));
    }
}

void EnemyMagmaSlime::UpdateMagmaRings(float deltaTime) {
    auto ring = magmaRings_.begin();
    while (ring != magmaRings_.end()) {
        if (!ring->visual) {
            ring = magmaRings_.erase(ring);
            continue;
        }

        ring->age += (std::max)(deltaTime, 0.0f);
        const float localAge = ring->age - ring->delay;
        if (localAge < 0.0f) {
            ++ring;
            continue;
        }

        const float progress = std::clamp(localAge / (std::max)(ring->lifetime, 0.01f), 0.0f, 1.0f);
        const float radius = Math::Lerp(ring->startRadius, ring->endRadius, EaseOutCubic(progress));
        const float thickness = 0.16f + (1.0f - progress) * 0.09f;
        ring->visual->SetIsVisible(progress < 1.0f);
        ring->visual->SetScale({ radius, thickness, radius });
        ring->visual->SetRotationY(ring->age * 0.72f);
        ring->visual->SetColor({ 1.0f, 0.16f + (1.0f - progress) * 0.20f, 0.01f, (1.0f - progress) * 0.84f });
        ring->visual->Update(deltaTime);
        ring->visual->UpdateWorldMatrix(true);

        if (!ring->hitTarget && target_) {
            const float targetDistance = PlanarDistance(target_->GetWorldPosition(), ring->position);
            const float hitPadding = target_->GetCollisionRadius() + 0.72f;
            if (targetDistance >= ring->previousRadius - hitPadding && targetDistance <= radius + hitPadding &&
                std::abs(target_->GetWorldPosition().y - ring->position.y) <= 3.0f) {
                const EnemyAttackDefinition& attack = GetAttackDefinition(kVolcanicSlamAttackId);
                const Vector3 knockbackDirection = NormalizePlanar(target_->GetWorldPosition() - ring->position);
                DamageEvent event;
                event.target = target_;
                event.attacker = this;
                event.damageAmount = attack.damage * 0.34f;
                event.damageType = DamageType::Fire;
                event.knockbackVelocity = {
                    knockbackDirection.x * 6.0f,
                    4.8f,
                    knockbackDirection.z * 6.0f,
                };
                event.statusEffect = MakeBurningStatus(
                    attack.statusDuration,
                    attack.statusTickInterval,
                    attack.statusTickDamage,
                    attack.statusVfx);
                EventManager::GetInstance()->Dispatch(event);
                ring->hitTarget = true;
            }
        }
        ring->previousRadius = radius;

        if (progress >= 1.0f) {
            ring = magmaRings_.erase(ring);
            continue;
        }
        ++ring;
    }
}

void EnemyMagmaSlime::PrepareEruptionTargets() {
    const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };
    const std::array<Vector3, 9> pattern = {
        lockedTargetPosition_,
        lockedTargetPosition_ + lockedDirection_ * 3.0f,
        lockedTargetPosition_ - lockedDirection_ * 2.7f,
        lockedTargetPosition_ + side * 3.0f,
        lockedTargetPosition_ - side * 3.0f,
        lockedTargetPosition_ + lockedDirection_ * 2.15f + side * 2.55f,
        lockedTargetPosition_ + lockedDirection_ * 2.15f - side * 2.55f,
        lockedTargetPosition_ - lockedDirection_ * 2.0f + side * 2.65f,
        lockedTargetPosition_ - lockedDirection_ * 2.0f - side * 2.65f,
    };
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        eruptionTargets_[index] = FindGroundPoint(pattern[index]);
    }
}

void EnemyMagmaSlime::SpawnNextMagmaPillar() {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(eruptionTargets_.size())) {
        return;
    }
    const EnemyAttackDefinition& attack = GetAttackDefinition(kEruptionFieldAttackId);
    const float step = static_cast<float>(actionIndex_);
    SpawnMagmaPillar(
        eruptionTargets_[static_cast<std::size_t>(actionIndex_)],
        (std::max)(1.05f, attack.radius * (0.72f + std::fmod(step, 3.0f) * 0.06f)),
        6.8f + std::fmod(step, 3.0f) * 0.72f,
        0.92f,
        0.0f,
        kEruptionFieldAttackId);
}

void EnemyMagmaSlime::SpawnMagmaPillar(
    const Vector3& position,
    float radius,
    float height,
    float lifetime,
    float delay,
    const char* attackId) {
    MagmaPillar pillar;
    pillar.visual = CreateMagmaVisual(
        "MagmaSlime_Eruption_" + std::to_string(magmaPillars_.size()),
        "Primitives/plane",
        kFireMaterialType,
        { 1.0f, 0.18f, 0.012f, 0.94f },
        2.8f);
    if (!pillar.visual) {
        return;
    }
    pillar.position = FindGroundPoint(position);
    pillar.position.y += 0.04f;
    pillar.radius = radius;
    pillar.height = height;
    pillar.lifetime = lifetime;
    pillar.delay = delay;
    pillar.attackId = attackId ? attackId : kEruptionFieldAttackId;
    pillar.particleTimer = 0.0f;
    pillar.visual->SetTranslate(pillar.position);
    pillar.visual->SetScale({ 0.01f, 0.01f, 0.01f });
    pillar.visual->SetIsVisible(delay <= 0.0f);
    pillar.visual->UpdateWorldMatrix(true);
    magmaPillars_.push_back(std::move(pillar));
}

void EnemyMagmaSlime::UpdateMagmaPillars(float deltaTime) {
    auto pillar = magmaPillars_.begin();
    while (pillar != magmaPillars_.end()) {
        if (!pillar->visual) {
            pillar = magmaPillars_.erase(pillar);
            continue;
        }

        pillar->age += (std::max)(deltaTime, 0.0f);
        const float localAge = pillar->age - pillar->delay;
        if (localAge < 0.0f) {
            ++pillar;
            continue;
        }

        if (!pillar->burstSpawned) {
            EmitDirectedPreset(
                kGeyserBurstPreset,
                pillar->position,
                { 0.0f, 1.0f, 0.0f },
                1.20f + pillar->height * 0.035f);
            SpawnMagmaPool(pillar->position, pillar->radius * 0.82f, 2.45f);
            pillar->burstSpawned = true;
        }

        const float progress = std::clamp(localAge / (std::max)(pillar->lifetime, 0.01f), 0.0f, 1.0f);
        const float rise = SmoothStep01(progress / 0.14f);
        const float fall = SmoothStep01((1.0f - progress) / 0.24f);
        const float envelope = (std::min)(rise, fall);
        const float activeHeight = (std::max)(0.16f, pillar->height * envelope);
        const float halfHeight = activeHeight * 0.5f;
        const float radiusPulse = 0.92f + std::sin(localAge * 19.0f) * 0.08f;
        pillar->visual->SetTranslate(pillar->position + Vector3{ 0.0f, halfHeight, 0.0f });
        pillar->visual->SetScale({ halfHeight, halfHeight, halfHeight });
        pillar->visual->SetColor({ 1.0f, 0.12f + envelope * 0.13f, 0.006f, 0.62f + envelope * 0.34f });
        pillar->visual->SetIsVisible(envelope > 0.015f);
        if (MeshRenderer* renderer = pillar->visual->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                fire->effectScaleX = std::clamp(
                    (pillar->radius * 2.0f * radiusPulse) / activeHeight, 0.18f, 2.6f);
                fire->effectScaleY = 1.0f;
                fire->flowSpeedX = std::sin(localAge * 2.2f) * 0.16f;
                fire->flowSpeedY = 1.04f;
            }
        }
        pillar->visual->Update(deltaTime);
        pillar->visual->UpdateWorldMatrix(true);

        pillar->particleTimer -= deltaTime;
        if (pillar->particleTimer <= 0.0f && envelope > 0.18f) {
            EmitDirectedPreset(
                kGeyserStreamPreset,
                pillar->position + Vector3{ 0.0f, activeHeight * 0.56f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                0.72f + envelope * 0.54f);
            pillar->particleTimer += 0.075f;
        }

        if (!pillar->damageApplied && envelope >= 0.62f) {
            const EnemyAttackDefinition& attack = GetAttackDefinition(pillar->attackId);
            Vector3 knockbackDirection = target_
                ? NormalizePlanar(target_->GetWorldPosition() - pillar->position)
                : lockedDirection_;
            const float damageScale = pillar->attackId == kVolcanicSlamAttackId ? 0.28f : 0.78f;
            DamageTargetAt(
                pillar->position,
                pillar->radius + (target_ ? target_->GetCollisionRadius() : 0.0f),
                attack.damage * damageScale,
                { knockbackDirection.x * 4.2f, 8.2f, knockbackDirection.z * 4.2f },
                attack.statusDuration,
                attack.statusTickInterval,
                attack.statusTickDamage);
            pillar->damageApplied = true;
        }

        if (progress >= 1.0f) {
            pillar = magmaPillars_.erase(pillar);
            continue;
        }
        ++pillar;
    }
}

void EnemyMagmaSlime::BeginLavaSpiral() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kLavaSpiralAttackId);
    const Vector3 origin = FindGroundPoint(GetTranslate());
    const float baseAngle = std::atan2(lockedDirection_.x, lockedDirection_.z);
    constexpr int kWaveCount = 3;
    constexpr int kSpokesPerWave = 6;
    for (int wave = 0; wave < kWaveCount; ++wave) {
        const float waveOffset = static_cast<float>(wave) * kPi / 6.0f;
        for (int spoke = 0; spoke < kSpokesPerWave; ++spoke) {
            const float angle = baseAngle + waveOffset + static_cast<float>(spoke) * kPi / 3.0f;
            MagmaSurge surge;
            surge.visual = CreateMagmaVisual(
                "MagmaSlime_Spiral_" + std::to_string(magmaSurges_.size()),
                "Primitives/Semicircle",
                kMagmaMaterialType,
                { 1.0f, 0.12f, 0.004f, 0.98f },
                3.0f);
            if (!surge.visual) {
                continue;
            }
            surge.crestVisual = CreateMagmaVisual(
                "MagmaSlime_SpiralCrest_" + std::to_string(magmaSurges_.size()),
                "Primitives/sphere",
                kFireMaterialType,
                { 1.0f, 0.19f, 0.012f, 0.88f },
                2.85f);
            surge.origin = origin;
            surge.direction = { std::sin(angle), 0.0f, std::cos(angle) };
            surge.delay = static_cast<float>(wave) * 0.20f + static_cast<float>(spoke) * 0.018f;
            surge.lifetime = 1.16f + static_cast<float>(wave) * 0.08f;
            surge.maxDistance = attack.maxRange * (0.84f + static_cast<float>(wave) * 0.08f);
            surge.radius = (std::max)(0.85f, attack.radius * (0.70f + static_cast<float>(wave) * 0.05f));
            surge.curveAmount = (wave % 2 == 0 ? 1.0f : -1.0f) * (0.28f + static_cast<float>(wave) * 0.06f);
            surge.leavesPool = spoke % 2 == 0;
            surge.visual->SetTranslate(origin + Vector3{ 0.0f, 0.07f, 0.0f });
            surge.visual->SetRotationY(angle);
            surge.visual->SetScale({ surge.radius * 2.35f, 0.24f, surge.radius * 1.72f });
            surge.visual->SetIsVisible(false);
            surge.visual->UpdateWorldMatrix(true);
            if (surge.crestVisual) {
                surge.crestVisual->SetTranslate(origin + Vector3{ 0.0f, surge.radius * 0.46f, 0.0f });
                surge.crestVisual->SetScale({ surge.radius * 1.54f, surge.radius * 1.05f, surge.radius * 1.54f });
                surge.crestVisual->SetIsVisible(false);
                if (MeshRenderer* renderer = surge.crestVisual->GetMeshRenderer()) {
                    if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                        fire->effectType = 2.0f;
                        fire->waveSpeed = 3.1f;
                        fire->waveFrequency = 3.6f;
                        fire->effectScale = 1.04f;
                        fire->effectSoftness = 0.48f;
                        fire->effectIntensity = 1.58f;
                        fire->billboardScale = 0.88f;
                        fire->effectScaleX = 1.20f;
                        fire->effectScaleY = 0.74f;
                    }
                }
                surge.crestVisual->UpdateWorldMatrix(true);
            }
            magmaSurges_.push_back(std::move(surge));
        }
    }
    EmitDirectedPreset(kSpiralSurgePreset, origin, { 0.0f, 1.0f, 0.0f }, 1.52f);
}

void EnemyMagmaSlime::UpdateMagmaSurges(float deltaTime) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kLavaSpiralAttackId);
    auto surge = magmaSurges_.begin();
    while (surge != magmaSurges_.end()) {
        if (!surge->visual) {
            surge = magmaSurges_.erase(surge);
            continue;
        }

        surge->age += (std::max)(deltaTime, 0.0f);
        const float localAge = surge->age - surge->delay;
        if (localAge < 0.0f) {
            ++surge;
            continue;
        }

        const float progress = std::clamp(localAge / (std::max)(surge->lifetime, 0.01f), 0.0f, 1.0f);
        const float baseAngle = std::atan2(surge->direction.x, surge->direction.z);
        const float curvedAngle = baseAngle + surge->curveAmount * SmoothStep01(progress);
        const Vector3 curvedDirection = { std::sin(curvedAngle), 0.0f, std::cos(curvedAngle) };
        const float distance = EaseOutCubic(progress) * surge->maxDistance;
        Vector3 position = FindGroundPoint(surge->origin + curvedDirection * distance);
        position.y += 0.075f;
        const float fade = std::clamp((1.0f - progress) / 0.18f, 0.0f, 1.0f);
        const float pulse = 1.0f + std::sin(localAge * 21.0f) * 0.12f;
        surge->visual->SetTranslate(position);
        surge->visual->SetRotationY(curvedAngle);
        surge->visual->SetScale({
            surge->radius * (2.35f + progress * 0.34f) * pulse,
            0.22f + (1.0f - progress) * 0.12f,
            surge->radius * 1.72f / pulse,
        });
        surge->visual->SetColor({ 1.0f, 0.10f + (1.0f - progress) * 0.15f, 0.003f, 0.98f * fade });
        surge->visual->SetIsVisible(progress < 1.0f);
        surge->visual->Update(deltaTime);
        surge->visual->UpdateWorldMatrix(true);
        if (surge->crestVisual) {
            const float crestScale = surge->radius * (1.34f + (1.0f - progress) * 0.30f) * pulse;
            surge->crestVisual->SetTranslate(
                position - curvedDirection * (surge->radius * 0.12f) +
                Vector3{ 0.0f, surge->radius * (0.48f + (1.0f - progress) * 0.12f), 0.0f });
            surge->crestVisual->SetScale({ crestScale * 1.26f, crestScale * 0.86f, crestScale * 1.26f });
            surge->crestVisual->SetColor({
                1.0f,
                0.16f + (1.0f - progress) * 0.14f,
                0.008f,
                0.58f * fade + (1.0f - progress) * 0.24f,
            });
            surge->crestVisual->SetIsVisible(progress < 1.0f);
            if (MeshRenderer* renderer = surge->crestVisual->GetMeshRenderer()) {
                if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                    fire->flowSpeedX = surge->curveAmount * 0.42f;
                    fire->flowSpeedY = 0.92f;
                    fire->effectIntensity = 1.48f + (1.0f - progress) * 0.24f;
                }
            }
            surge->crestVisual->Update(deltaTime);
            surge->crestVisual->UpdateWorldMatrix(true);
        }

        surge->trailTimer -= deltaTime;
        if (surge->trailTimer <= 0.0f && progress < 0.92f) {
            EmitDirectedPreset(
                kSpiralTrailPreset,
                position,
                curvedDirection * -1.0f + Vector3{ 0.0f, 0.28f, 0.0f },
                0.92f + progress * 0.38f);
            surge->trailTimer += 0.065f;
        }

        if (!surge->hitTarget && target_ &&
            std::abs(target_->GetWorldPosition().y - position.y) <= kPoolVerticalTolerance &&
            PlanarDistance(target_->GetWorldPosition(), position) <= surge->radius + target_->GetCollisionRadius()) {
            DamageTargetAt(
                position,
                surge->radius + target_->GetCollisionRadius(),
                attack.damage * 0.52f,
                { curvedDirection.x * 6.8f, 4.8f, curvedDirection.z * 6.8f },
                attack.statusDuration,
                attack.statusTickInterval,
                attack.statusTickDamage);
            surge->hitTarget = true;
        }

        if (progress >= 1.0f) {
            if (surge->leavesPool && !surge->poolSpawned) {
                SpawnMagmaPool(position, surge->radius * 0.78f, 2.25f);
                surge->poolSpawned = true;
            }
            surge = magmaSurges_.erase(surge);
            continue;
        }
        ++surge;
    }
}

void EnemyMagmaSlime::PrepareFireColumnTargets() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kFireColumnAttackId);
    const Vector3 casterGround = FindGroundPoint(GetTranslate());
    Vector3 toTarget = lockedTargetPosition_ - casterGround;
    const float targetDistance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    const Vector3 forward = NormalizePlanar(lockedDirection_);
    // 炎柱の外周は中央の攻撃半径より広く取り、3本の柱と線を読み分けやすくします。
    const float orbitRadius = attack.radius * 1.6f;
    const AABB bounds = GetModelWorldAABB();
    const float bossRadius = (std::max)({
        (bounds.max.x - bounds.min.x) * 0.5f,
        (bounds.max.z - bounds.min.z) * 0.5f,
        baseScale_.x * 0.84f });
    const float maxDistance = (std::max)(attack.maxRange, 1.0f);
    const float minimumDistance = (std::min)(maxDistance, (std::max)(attack.minRange,
        bossRadius + orbitRadius * 0.5f + attack.radius + 0.6f));
    const float distance = std::clamp(targetDistance, minimumDistance, maxDistance);
    toTarget = (targetDistance > 0.001f ? NormalizePlanar(toTarget) : forward) * distance;
    fireColumnCenter_ = FindGroundPoint(casterGround + Vector3{ toTarget.x, 0.0f, toTarget.z });
    const Vector3 side = { forward.z, 0.0f, -forward.x };
    for (std::size_t index = 0; index < fireColumnOuterPositions_.size(); ++index) {
        const float angle = static_cast<float>(index) * (2.0f * kPi / 3.0f);
        fireColumnOuterPositions_[index] = FindGroundPoint(
            fireColumnCenter_ + forward * (std::cos(angle) * orbitRadius) +
            side * (std::sin(angle) * orbitRadius));
    }
}

void EnemyMagmaSlime::CreateFireColumnSigils() {
    if (!common_) return;
    for (std::size_t index = 0; index < fireColumnSigils_.size(); ++index) {
        auto sigil = std::make_unique<EffectObject3d>();
        sigil->Initialize(common_);
        if (!sigil->LoadFromJson(kFireChantEffect)) continue;
        sigil->SetName("MagmaSlime_FireColumnSigil_" + std::to_string(index));
        sigil->SetBlendMode(BlendMode::kAdd);
        sigil->SetRevealProgress(0.0f);
        sigil->SetIsVisible(false);
        fireColumnSigils_[index] = std::move(sigil);
    }
    for (std::size_t index = 0; index < fireColumnLinks_.size(); ++index) {
        auto link = std::make_unique<EffectObject3d>();
        link->Initialize(common_);
        if (!link->LoadFromJson(kFireColumnLinkEffect)) continue;
        link->SetName("MagmaSlime_FireColumnLink_" + std::to_string(index));
        link->SetBlendMode(BlendMode::kAdd);
        link->SetRevealMode(1);
        link->SetRevealProgress(0.0f);
        link->SetIsVisible(false);
        fireColumnLinks_[index] = std::move(link);
    }
}

void EnemyMagmaSlime::UpdateFireColumnSigils(float deltaTime) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kFireColumnAttackId);
    const bool windingUp = attackState_ == AttackState::Windup;
    const float windupProgress = windingUp
        ? 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f)
        : 1.0f;
    const float elapsed = windingUp ? 0.0f :
        (std::max)(0.0f, attackStateDuration_ - attackTimer_);
    const float circleStart = attackStateDuration_ * 0.22f;
    const float blastStart = attackStateDuration_ * 0.49f;
    const float fade = windingUp ? 1.0f :
        SmoothStep01(std::clamp(attackTimer_ / 0.95f, 0.0f, 1.0f));
    for (std::size_t index = 0; index < fireColumnSigils_.size(); ++index) {
        EffectObject3d* sigil = fireColumnSigils_[index].get();
        if (!sigil) continue;
        const bool center = index == fireColumnOuterPositions_.size();
        const float assembly = center
            ? (windingUp ? 0.0f : SmoothStep01(std::clamp(
                (elapsed - circleStart) / (blastStart - circleStart), 0.0f, 1.0f)))
            : SmoothStep01(std::clamp((windupProgress - index * 0.15f) / 0.55f,
                0.0f, 1.0f));
        const Vector3 ground = center ? fireColumnCenter_ : fireColumnOuterPositions_[index];
        // 元の平面は2.4m幅なので、魔法陣の半径を炎柱と判定の半径に揃えます。
        const float scale = attack.radius * (center ? (1.0f / 1.2f) : 0.31f);
        sigil->Update(deltaTime);
        sigil->SetRevealProgress(assembly);
        sigil->SetIsVisible(assembly > 0.005f && fade > 0.01f);
        sigil->SetTranslate(ground + Vector3{ 0.0f, 0.09f, 0.0f });
        sigil->SetRotation({ 0.0f, idleTimer_ * (center ? 0.38f : -0.24f), 0.0f });
        sigil->SetScale({ scale, 1.0f, scale });
        sigil->SetColor({ 1.0f, 0.75f, 0.36f,
            (0.28f + assembly * 0.45f) * fade });
        sigil->SetIntensity(1.25f + assembly * 0.45f);
        sigil->UpdateLocalMatrix();
        sigil->UpdateWorldMatrix();
    }
    const float linkReveal = SmoothStep01(std::clamp(
        (windupProgress - 0.38f) / 0.52f, 0.0f, 1.0f));
    for (std::size_t index = 0; index < fireColumnLinks_.size(); ++index) {
        EffectObject3d* link = fireColumnLinks_[index].get();
        if (!link) continue;
        const Vector3& a = fireColumnOuterPositions_[index];
        const Vector3& b = fireColumnOuterPositions_[(index + 1) % fireColumnOuterPositions_.size()];
        const Vector3 delta = b - a;
        const float length = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        const float yaw = std::atan2(-delta.z, delta.x);
        link->Update(deltaTime);
        link->SetRevealProgress(linkReveal);
        link->SetIsVisible(linkReveal > 0.005f && fade > 0.01f);
        link->SetTranslate({ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f + 0.12f,
            (a.z + b.z) * 0.5f });
        link->SetRotation({ 0.0f, yaw, 0.0f });
        link->SetScale({ length / 2.4f, 1.0f, 0.46f });
        link->SetColor({ 1.0f, 0.54f, 0.16f, 0.70f * fade });
        link->SetIntensity(1.7f);
        link->UpdateLocalMatrix();
        link->UpdateWorldMatrix();
    }
}

void EnemyMagmaSlime::BeginFireColumn() {
    fireColumnDamageTimer_ = 0.0f;
    PrepareFireColumnTargets();

    for (std::size_t index = 0; index < fireColumnVisuals_.size(); ++index) {
        auto& flame = fireColumnVisuals_[index];
        flame = CreateMagmaVisual(
            "MagmaSlime_FireColumn_" + std::to_string(index),
            "Primitives/cylinder", kFireMaterialType,
            { 1.0f, 0.44f, 0.06f, 0.82f }, 3.0f);
        if (flame) {
            if (MeshRenderer* renderer = flame->GetMeshRenderer()) {
                if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                    // 閉じたメッシュの内部に連続した炎柱を描きます。
                    fire->effectType = 6.0f;
                    fire->waveSpeed = index < 3 ? 1.35f : 1.55f;
                    fire->waveFrequency = index < 3 ? 2.2f : 2.6f;
                    fire->waveHeight = index < 3 ? 0.32f : 0.42f;
                    fire->effectScale = 1.0f;
                    fire->effectSoftness = 0.0f;
                    fire->effectIntensity = index < 3 ? 0.90f : 1.05f;
                    fire->billboardScale = 1.0f;
                    fire->effectScaleX = 1.0f;
                    fire->effectScaleY = 1.0f;
                    fire->effectScaleZ = 1.0f;
                    fire->uvOffsetX = 2.73f * static_cast<float>(index + 1);
                    fire->uvOffsetY = 4.17f * static_cast<float>(index + 1);
                }
            }
            flame->SetIsVisible(false);
        }
    }
}

void EnemyMagmaSlime::UpdateFireColumn(float deltaTime) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kFireColumnAttackId);
    const float elapsed = (std::max)(0.0f, attackStateDuration_ - attackTimer_);
    const float outerSpacing = attackStateDuration_ * 0.09f;
    const float blastStart = attackStateDuration_ * 0.49f;
    const float ending = SmoothStep01(std::clamp(attackTimer_ / 0.85f, 0.0f, 1.0f));
    const float dissolve = SmoothStep01(std::clamp(
        1.0f - attackTimer_ / 1.05f, 0.0f, 1.0f));

    // 三本を順に立て、中央の紋章が完成してから主柱を噴き上げます。
    for (std::size_t index = 0; index < fireColumnVisuals_.size(); ++index) {
        Object3d* flame = fireColumnVisuals_[index].get();
        if (!flame) continue;
        const bool center = index == fireColumnOuterPositions_.size();
        const float start = center ? blastStart : static_cast<float>(index) * outerSpacing;
        const float age = elapsed - start;
        const float rise = SmoothStep01(std::clamp(age / (center ? 0.56f : 0.36f), 0.0f, 1.0f));
        const float envelope = ending;
        // シェーダー内の実際の炎半径は立体メッシュ半幅のおよそ0.62倍です。
        const float radius = attack.radius * (center ? 0.81f : 0.31f);
        const float height = center ? 18.0f : 12.5f;
        const Vector3 ground = center ? fireColumnCenter_ : fireColumnOuterPositions_[index];
        if (age > 0.0f && age - deltaTime <= 0.0f) {
            EmitDirectedPreset(kImpactPreset, ground + Vector3{ 0.0f, 0.12f, 0.0f },
                { 0.0f, 1.0f, 0.0f }, center ? 1.75f : 0.90f);
        }
        const float activeHeight = height * rise;
        const float flicker = 1.0f + 0.035f * std::sin(idleTimer_ * 12.0f + index * 1.7f);
        flame->SetTranslate(ground + Vector3{ 0.0f, activeHeight * 0.5f, 0.0f });
        flame->SetScale({ radius * 2.0f * flicker, activeHeight * 0.5f,
            radius * 2.0f * flicker });
        flame->SetColor({ 1.0f, center ? 0.58f : 0.38f, 0.035f,
            (center ? 0.94f : 0.75f) * envelope });
        if (MeshRenderer* renderer = flame->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                // 炎柱だけでディゾルブ進行値として使い、下部から徐々にほどけさせます。
                fire->effectSoftness = dissolve;
            }
        }
        flame->SetIsVisible(age > 0.0f && rise > 0.01f && envelope > 0.01f);
        flame->Update(deltaTime);
        flame->UpdateWorldMatrix(true);
    }

    UpdateFireColumnSigils(deltaTime);

    // 終了前に発生を止めると、残った火の粉が炎柱より遅れて自然に消えます。
    if (effectTimer_ <= 0.0f && attackTimer_ > 0.78f) {
        const float layerY = (static_cast<int>(elapsed / 0.16f) % 2 == 0) ? 2.0f : 6.0f;
        for (std::size_t index = 0; index < fireColumnOuterPositions_.size(); ++index) {
            if (elapsed <= static_cast<float>(index) * outerSpacing) continue;
            EmitDirectedPreset(kFireColumnStreamPreset,
                fireColumnOuterPositions_[index] + Vector3{ 0.0f, layerY, 0.0f },
                { 0.0f, 1.0f, 0.0f }, 0.82f);
        }
        if (elapsed >= blastStart) {
            EmitDirectedPreset(kFireColumnStreamPreset,
                fireColumnCenter_ + Vector3{ 0.0f, layerY + 1.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f }, 1.16f);
            for (int sparkIndex = 0; sparkIndex < 3; ++sparkIndex) {
                const float angle = idleTimer_ * 0.95f +
                    static_cast<float>(sparkIndex) * (2.0f * kPi / 3.0f);
                const Vector3 rim = {
                    std::cos(angle) * attack.radius * 0.77f,
                    layerY + 0.8f,
                    std::sin(angle) * attack.radius * 0.77f,
                };
                EmitDirectedPreset(kFireColumnStreamPreset,
                    fireColumnCenter_ + rim, { 0.0f, 1.0f, 0.0f }, 1.05f);
            }
        }
        effectTimer_ += 0.16f;
    }
    fireColumnDamageTimer_ -= deltaTime;
    if (elapsed >= blastStart + 0.22f && attackTimer_ > 0.30f &&
        fireColumnDamageTimer_ <= 0.0f) {
        DamageTargetAt(fireColumnCenter_, attack.radius +
            (target_ ? target_->GetCollisionRadius() : 0.0f), attack.damage,
            { lockedDirection_.x * 4.0f, 6.0f, lockedDirection_.z * 4.0f },
            attack.statusDuration, attack.statusTickInterval, attack.statusTickDamage);
        fireColumnDamageTimer_ = 0.52f;
    }
}

void EnemyMagmaSlime::BeginMagmaFlood() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kMagmaFloodAttackId);
    const Vector3 origin = FindGroundPoint(GetTranslate());
    const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };
    floodDamageTimer_ = 0.0f;
    constexpr int kCellCount = 10;
    for (int index = 0; index < kCellCount; ++index) {
        MagmaFloodCell cell;
        const float distance = attack.maxRange *
            (static_cast<float>(index) + 0.55f) / static_cast<float>(kCellCount);
        const float meander = std::sin(static_cast<float>(index) * 1.4f) * 0.20f;
        cell.position = FindGroundPoint(origin + lockedDirection_ * distance + side * meander);
        cell.direction = lockedDirection_;
        cell.position.y += 0.09f;
        cell.delay = static_cast<float>(index) * 0.115f;
        cell.lifetime = 1.65f + static_cast<float>(index) * 0.035f;
        cell.radius = attack.radius * (0.93f + 0.07f * std::sin(index * 2.1f));
        cell.leavesPool = index == 2 || index == 5 || index == 8;
        cell.surface = CreateMagmaVisual(
            "MagmaSlime_FloodSurface_" + std::to_string(index),
            "Primitives/cylinder", kMagmaMaterialType,
            { 0.80f, 0.09f, 0.008f, 0.93f }, 2.3f);
        cell.crest = CreateMagmaVisual(
            "MagmaSlime_FloodCrest_" + std::to_string(index),
            "Primitives/sphere", kMagmaMaterialType,
            { 1.0f, 0.26f, 0.03f, 0.92f }, 2.9f);
        if (!cell.surface) continue;
        cell.surface->SetIsVisible(false);
        if (cell.crest) cell.crest->SetIsVisible(false);
        magmaFloodCells_.push_back(std::move(cell));
    }
}

void EnemyMagmaSlime::UpdateMagmaFlood(float deltaTime) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kMagmaFloodAttackId);
    floodDamageTimer_ = (std::max)(0.0f, floodDamageTimer_ - deltaTime);
    for (auto cell = magmaFloodCells_.begin(); cell != magmaFloodCells_.end();) {
        cell->age += (std::max)(0.0f, deltaTime);
        const float localAge = cell->age - cell->delay;
        if (localAge < 0.0f) { ++cell; continue; }
        if (!cell->burstSpawned) {
            EmitDirectedPreset(kFloodSplashPreset, cell->position,
                cell->direction + Vector3{ 0.0f, 0.72f, 0.0f }, 1.2f);
            if (cell->leavesPool) {
                SpawnMagmaPool(cell->position, cell->radius * 0.82f, 2.75f);
            }
            cell->burstSpawned = true;
        }
        const float appear = SmoothStep01(std::clamp(localAge / 0.22f, 0.0f, 1.0f));
        const float fade = std::clamp((cell->lifetime - localAge) / 0.38f, 0.0f, 1.0f);
        const float surge = std::exp(-localAge * 3.2f);
        const float pulse = 1.0f + std::sin(localAge * 5.0f) * 0.035f;
        const float yaw = std::atan2(cell->direction.x, cell->direction.z);
        cell->surface->SetTranslate(cell->position + Vector3{ 0.0f, 0.18f * appear, 0.0f });
        cell->surface->SetRotationY(yaw);
        cell->surface->SetScale({ cell->radius * 1.12f * appear * pulse,
            0.16f + 0.12f * surge, cell->radius * 1.15f * appear / pulse });
        cell->surface->SetColor({ 0.77f, 0.09f + 0.07f * surge, 0.004f, 0.90f * fade });
        cell->surface->SetIsVisible(appear > 0.01f && fade > 0.01f);
        cell->surface->Update(deltaTime);
        cell->surface->UpdateWorldMatrix(true);
        if (cell->crest) {
            cell->crest->SetTranslate(cell->position + cell->direction * (cell->radius * 0.52f) +
                Vector3{ 0.0f, 0.35f + surge * 0.55f, 0.0f });
            cell->crest->SetScale({ cell->radius * 0.95f * appear,
                (0.23f + surge * 0.45f) * appear, cell->radius * 0.70f * appear });
            cell->crest->SetColor({ 1.0f, 0.25f, 0.02f, 0.82f * fade * appear });
            cell->crest->SetIsVisible(appear > 0.02f && fade > 0.01f);
            cell->crest->Update(deltaTime);
            cell->crest->UpdateWorldMatrix(true);
        }
        if (target_ && floodDamageTimer_ <= 0.0f && appear > 0.60f && fade > 0.30f &&
            std::abs(target_->GetWorldPosition().y - cell->position.y) <= kPoolVerticalTolerance &&
            PlanarDistance(target_->GetWorldPosition(), cell->position) <=
                cell->radius + target_->GetCollisionRadius()) {
            DamageTargetAt(cell->position, cell->radius + target_->GetCollisionRadius(),
                attack.damage, { cell->direction.x * 4.0f, 3.0f, cell->direction.z * 4.0f },
                attack.statusDuration, attack.statusTickInterval, attack.statusTickDamage);
            floodDamageTimer_ = 0.68f;
        }
        if (localAge >= cell->lifetime) {
            cell = magmaFloodCells_.erase(cell);
        } else {
            ++cell;
        }
    }
}

void EnemyMagmaSlime::UpdateChantVisual(float deltaTime) {
    if (!chantVisual_) return;
    BossChantFrame frame;
    frame.casterPosition = GetWorldPosition();
    frame.groundPosition = FindGroundPoint(frame.casterPosition);
    const AABB bounds = GetModelWorldAABB();
    frame.casterRadius = (std::max)({
        (bounds.max.x - bounds.min.x) * 0.5f,
        (bounds.max.z - bounds.min.z) * 0.5f,
        baseScale_.x * 0.84f });
    frame.casterHeight = (std::max)(bounds.max.y - bounds.min.y, baseScale_.y * 1.2f);
    frame.sigilCenter = frame.casterPosition +
        lockedDirection_ * std::clamp(frame.casterRadius * 1.55f, 2.4f, 5.0f) +
        Vector3{ 0.0f, currentAttack_ == AttackKind::MagmaFlood
            ? frame.casterHeight * 0.35f : frame.casterHeight * 0.58f, 0.0f };
    frame.forward = lockedDirection_;
    frame.casting = attackState_ == AttackState::Windup;
    frame.releasing = attackState_ == AttackState::Active;
    frame.windupProgress = frame.casting
        ? 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f)
        : 1.0f;
    frame.activeElapsed = frame.releasing
        ? (std::max)(0.0f, attackStateDuration_ - attackTimer_) : 0.0f;
    frame.time = idleTimer_;
    frame.deltaTime = deltaTime;
    chantVisual_->Update(frame);
}

std::unique_ptr<Object3d> EnemyMagmaSlime::CreateMagmaVisual(
    const std::string& name,
    const std::string& model,
    int materialType,
    const Vector4& color,
    float emissive) const {
    if (!common_) {
        return nullptr;
    }

    auto visual = std::make_unique<Object3d>();
    visual->Initialize(common_);
    visual->SetName(name);
    visual->SetClassName("EnemyVisualPart");
    visual->SetModel(model);
    visual->SetColliderType(ColliderType::kNone);
    visual->SetCollisionAttribute(0);
    visual->SetCollisionMask(0);
    visual->SetMaterialType(materialType);
    visual->SetBlendMode(BlendMode::kNormal);
    visual->SetSelectedLighting(0);
    visual->SetEnableLighting(false);
    visual->SetEnableEnvMap(false);
    visual->SetColor(color);
    visual->SetEmissive(emissive);
    visual->SetRoughness(0.20f);
    visual->SetMetallic(0.0f);

    if (MeshRenderer* renderer = visual->GetMeshRenderer()) {
        if (MeshRenderer::WaterParamForGPU* parameter = renderer->GetWaterParamData()) {
            const bool isShockwave = materialType == kShockwaveMaterialType;
            const bool isFire = materialType == kFireMaterialType;
            parameter->waveSpeed = isShockwave ? 1.8f : (isFire ? 3.4f : 3.2f);
            parameter->waveHeight = isShockwave ? 0.16f : (isFire ? 0.58f : 0.42f);
            parameter->waveFrequency = isShockwave ? 5.4f : 2.8f;
            parameter->flowSpeedX = 0.06f;
            parameter->flowSpeedY = isFire ? 1.04f : 0.22f;
            parameter->effectType = isShockwave ? 1.0f : 0.0f;
            parameter->effectScale = isShockwave ? 0.74f : (isFire ? 1.0f : 1.06f);
            parameter->effectSoftness = isFire ? 0.48f : 0.36f;
            parameter->effectIntensity = isShockwave ? 1.55f : (isFire ? 1.48f : 1.34f);
            parameter->billboardScale = 1.0f;
            if (isFire) {
                parameter->effectScaleX = 0.46f;
                parameter->effectScaleY = 1.0f;
                parameter->effectScaleZ = 1.0f;
                parameter->uvOffsetX = 2.17f;
                parameter->uvOffsetY = 5.31f;
            }
        }
    }
    return visual;
}

Vector3 EnemyMagmaSlime::FindGroundPoint(const Vector3& samplePosition) const {
    return CombatGroundQuery::FindGroundPoint(
        samplePosition, this, { 12.0f, 28.0f }, GetTranslate().y);
}

void EnemyMagmaSlime::DamageTargetAt(
    const Vector3& center,
    float radius,
    float damage,
    const Vector3& knockback,
    float burnDuration,
    float burnTickInterval,
    float burnTickDamage) {
    if (!target_ || target_->isDead || !target_->GetIsVisible()) {
        return;
    }

    const Vector3 targetPosition = target_->GetWorldPosition();
    if (std::abs(targetPosition.y - center.y) > kPoolVerticalTolerance + 1.0f ||
        PlanarDistance(targetPosition, center) > radius) {
        return;
    }

    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = damage;
    event.damageType = DamageType::Fire;
    event.knockbackVelocity = knockback;
    event.statusEffect = MakeBurningStatus(
        burnDuration,
        burnTickInterval,
        burnTickDamage,
        "status_burning_flame");
    EventManager::GetInstance()->Dispatch(event);
}

void EnemyMagmaSlime::ApplySlimeAnimation(float deltaTime) {
    SlimeBounceAnimator::Params params;
    params.speedForFullBounce = 2.8f;
    params.idleAmplitude = 0.045f;
    params.moveAmplitude = 0.16f;
    params.hopFrequency = 4.9f;
    params.horizontalSquash = 0.19f;
    params.verticalStretch = 0.24f;
    params.airborneStretch = 0.34f;

    Vector3 targetScale = SlimeBounceAnimator::MakeScale(
        baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    Vector3 targetRotation = { 0.0f, GetRotation().y, 0.0f };

    if (attackState_ == AttackState::Windup) {
        const float progress = SmoothStep01(1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f));
        const float tremble = std::sin(progress * kPi * 15.0f) * progress * 0.035f;
        if (currentAttack_ == AttackKind::VolcanicSlam) {
            targetScale = {
                baseScale_.x * (1.0f + progress * 0.42f + tremble),
                baseScale_.y * (1.0f - progress * 0.42f),
                baseScale_.z * (1.0f + progress * 0.34f - tremble),
            };
        } else if (currentAttack_ == AttackKind::LavaRush) {
            targetScale = {
                baseScale_.x * (1.0f + progress * 0.24f + tremble),
                baseScale_.y * (1.0f - progress * 0.29f),
                baseScale_.z * (1.0f + progress * 0.18f - tremble),
            };
            targetRotation.x = -progress * 0.10f;
        } else if (currentAttack_ == AttackKind::EruptionField) {
            targetScale = {
                baseScale_.x * (1.0f + progress * 0.34f + tremble),
                baseScale_.y * (1.0f - progress * 0.30f),
                baseScale_.z * (1.0f + progress * 0.34f - tremble),
            };
            targetRotation.z = tremble * 1.8f;
        } else if (currentAttack_ == AttackKind::LavaSpiral) {
            const float coil = std::sin(progress * kPi * 6.0f) * progress;
            targetScale = {
                baseScale_.x * (1.0f + progress * 0.22f + coil * 0.06f),
                baseScale_.y * (1.0f - progress * 0.18f),
                baseScale_.z * (1.0f + progress * 0.22f - coil * 0.06f),
            };
            targetRotation.z = coil * 0.12f;
        } else {
            targetScale = SlimeBounceAnimator::MakeChargeSquash(baseScale_, progress, idleTimer_, 0.92f);
            targetRotation.z = tremble * 1.2f;
        }
    } else if (attackState_ == AttackState::Active) {
        const float progress = 1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f);
        if (currentAttack_ == AttackKind::MagmaMortar) {
            const float recoil = std::sin(progress * kPi * 6.0f);
            targetScale = {
                baseScale_.x * (1.14f + std::abs(recoil) * 0.08f),
                baseScale_.y * (0.84f - recoil * 0.06f),
                baseScale_.z * (1.10f + recoil * 0.04f),
            };
        } else if (currentAttack_ == AttackKind::LavaRush) {
            const float speedPulse = std::sin(progress * kPi * 10.0f) * 0.035f;
            targetScale = {
                baseScale_.x * (1.18f + speedPulse),
                baseScale_.y * 0.72f,
                baseScale_.z * (1.42f - speedPulse),
            };
            targetRotation.x = -0.12f;
        } else if (currentAttack_ == AttackKind::VolcanicSlam) {
            const float airborne = std::sin(progress * kPi);
            const float landing = SmoothStep01(std::clamp((progress - 0.86f) / 0.14f, 0.0f, 1.0f));
            targetScale = {
                baseScale_.x * (0.88f + landing * 0.45f),
                baseScale_.y * (1.34f + airborne * 0.22f - landing * 0.62f),
                baseScale_.z * (0.88f + landing * 0.45f),
            };
            targetRotation.x = -airborne * 0.075f;
        } else if (currentAttack_ == AttackKind::EruptionField) {
            const float eruptionPulse = std::sin(progress * kPi * 18.0f);
            targetScale = {
                baseScale_.x * (1.18f + std::abs(eruptionPulse) * 0.09f),
                baseScale_.y * (0.78f - eruptionPulse * 0.08f),
                baseScale_.z * (1.18f + std::abs(eruptionPulse) * 0.09f),
            };
            targetRotation.z = eruptionPulse * 0.065f;
        } else if (currentAttack_ == AttackKind::LavaSpiral) {
            const float spiralPulse = std::sin(progress * kPi * 12.0f);
            targetScale = {
                baseScale_.x * (1.22f + spiralPulse * 0.08f),
                baseScale_.y * (0.76f - std::abs(spiralPulse) * 0.06f),
                baseScale_.z * (1.22f - spiralPulse * 0.08f),
            };
            targetRotation.z = std::sin(progress * kPi * 4.0f) * 0.16f;
        }
    } else if (attackState_ == AttackState::Recovery || landingPulseTimer_ > 0.0f) {
        const float remaining = attackState_ == AttackState::Recovery
            ? std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f)
            : std::clamp(landingPulseTimer_ / 0.34f, 0.0f, 1.0f);
        const float bounce = std::sin((1.0f - remaining) * kPi * 2.6f) * remaining;
        targetScale = {
            baseScale_.x * (1.0f + bounce * 0.18f),
            baseScale_.y * (1.0f - bounce * 0.25f),
            baseScale_.z * (1.0f + bounce * 0.18f),
        };
    }

    if (attackState_ == AttackState::Idle) {
        SlimeBounceAnimator::ApplyTravelPose(targetScale, targetRotation,
            baseScale_, GetVelocity(), GetRotation().y - kMagmaSlimeModelYawOffset,
            3.4f, 0.065f, 0.045f);
        SlimeBounceAnimator::ApplyGroundHopPose(targetScale, baseScale_,
            isGrounded_, locomotionHopTimer_, kLocomotionHopInterval,
            kLocomotionAnticipationDuration,
            locomotionLandingTimer_, kLocomotionLandingDuration);
    }
    ApplyDamageReactionPose(targetScale, targetRotation);
    const float rate = 1.0f - std::exp(-deltaTime * 12.0f);
    const Vector3 currentScale = GetScale();
    SetScale({
        Math::Lerp(currentScale.x, targetScale.x, rate),
        Math::Lerp(currentScale.y, targetScale.y, rate),
        Math::Lerp(currentScale.z, targetScale.z, rate),
    });
    const Vector3 currentRotation = GetRotation();
    SetRotation({
        Math::Lerp(currentRotation.x, targetRotation.x, rate),
        currentRotation.y,
        Math::Lerp(currentRotation.z, targetRotation.z, rate),
    });
}

void EnemyMagmaSlime::UpdateFacing(const Vector3& direction) {
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z) + kMagmaSlimeModelYawOffset;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.10f));
}

void EnemyMagmaSlime::UpdateBodyHeat(float deltaTime) {
    ambientEmberTimer_ -= deltaTime;
    const bool attacking = attackState_ == AttackState::Windup || attackState_ == AttackState::Active;
    const float attackGlow = attacking ? 0.58f : 0.0f;
    const float pulse = std::sin(idleTimer_ * (attacking ? 9.0f : 3.8f)) * (attacking ? 0.16f : 0.08f);
    SetEmissive(1.16f + attackGlow * 0.62f + pulse * 0.45f);
    defaultColor_ = {
        1.0f,
        0.94f + attackGlow * 0.05f,
        0.84f + attackGlow * 0.07f + pulse * 0.04f,
        1.0f,
    };
    SetColor(defaultColor_);

    if (ambientEmberTimer_ <= 0.0f) {
        EmitDirectedPreset(
            kCoreEmberPreset,
            GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.58f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            attacking ? 1.22f : 0.78f);
        ambientEmberTimer_ += attacking ? 0.085f : 0.19f;
    }
}

void EnemyMagmaSlime::EmitPreset(const char* presetName, const Vector3& position) const {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyMagmaSlime::EmitDirectedPreset(
    const char* presetName,
    const Vector3& position,
    const Vector3& direction,
    float speedScale) const {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyMagmaSlime::SyncCollisionRadius() {
    const Vector3 scale = GetScale();
    const float maximumScale = (std::max)({ 0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    SetCollisionRadius(kGroundCollisionWorldRadius / maximumScale);
}

void EnemyMagmaSlime::ClearTransientVisuals() {
    ClearRushFlameVisuals();
    for (auto& flame : fireColumnVisuals_) flame.reset();
    for (auto& sigil : fireColumnSigils_) sigil.reset();
    for (auto& link : fireColumnLinks_) link.reset();
    chantVisual_.reset();
    magmaBlobs_.clear();
    magmaPools_.clear();
    magmaRings_.clear();
    magmaPillars_.clear();
    magmaSurges_.clear();
    magmaFloodCells_.clear();
}

bool EnemyMagmaSlime::UpdateEncounterState(float deltaTime) {
    if (!IsEncounterControlled()) {
        encounterState_ = EncounterState::Normal;
        encounterInitializedForPlay_ = false;
        return false;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->IsPlaying()) {
        ResetEncounterStateForEditor();
        return true;
    }

    if (!encounterInitializedForPlay_) {
        InitializeEncounterState();
    }
    if (encounterState_ == EncounterState::Dormant) {
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        ClearTransientVisuals();
        HideAttackTelegraph();
        return true;
    }
    if (encounterState_ != EncounterState::Appearing) {
        return false;
    }

    encounterTimer_ += (std::max)(0.0f, deltaTime);
    idleTimer_ += (std::max)(0.0f, deltaTime);
    const float progress = std::clamp(
        encounterTimer_ / GetEncounterAppearanceDuration(), 0.0f, 1.0f);
    const float eased = SmoothStep01(progress);
    const float landingPulse = std::sin(progress * kPi) * 0.16f;
    const float horizontalScale = 0.24f + eased * 0.76f + landingPulse;
    const float verticalScale = 0.12f + eased * 0.88f + landingPulse * 0.55f;

    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetTranslate({
        encounterBasePosition_.x,
        encounterBasePosition_.y + (1.0f - eased) * kEncounterAppearanceRise,
        encounterBasePosition_.z,
    });
    SetRotation({
        encounterBaseRotation_.x,
        encounterBaseRotation_.y + (1.0f - eased) * kPi * 0.36f,
        encounterBaseRotation_.z,
    });
    SetScale({
        encounterBaseScale_.x * horizontalScale,
        encounterBaseScale_.y * verticalScale,
        encounterBaseScale_.z * horizontalScale,
    });
    SetColor({
        encounterBaseColor_.x,
        encounterBaseColor_.y,
        encounterBaseColor_.z,
        (std::max)(0.01f, eased),
    });
    SetEmissive(encounterBaseEmissive_ + (1.0f - std::abs(progress * 2.0f - 1.0f)) * 1.75f);
    SyncCollisionRadius();

    if (progress >= 1.0f) {
        FinishEncounterAppearance();
    }
    return true;
}

void EnemyMagmaSlime::CaptureEncounterAuthoredState(bool refreshVisualTransform) {
    if (encounterAuthoredStateCaptured_ && !refreshVisualTransform) {
        return;
    }

    encounterBasePosition_ = GetTransform()->translate;
    encounterBaseScale_ = GetScale();
    encounterBaseRotation_ = GetRotation();
    encounterBaseColor_ = GetColor();
    encounterBaseEmissive_ = GetEmissive();
    encounterMaterialType_ = GetMaterialType();
    const uint32_t collisionAttribute = GetCollisionAttribute();
    const uint32_t collisionMask = GetCollisionMask();
    // 待機中に保存された衝突無効値を、登場後へ持ち越さないよう補います。
    if (collisionAttribute != 0) {
        encounterCollisionAttribute_ = collisionAttribute;
    } else if (!encounterAuthoredStateCaptured_) {
        encounterCollisionAttribute_ = kEnemy;
    }
    if (collisionMask != 0) {
        encounterCollisionMask_ = collisionMask;
    } else if (!encounterAuthoredStateCaptured_) {
        encounterCollisionMask_ = kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet;
    }
    baseScale_ = encounterBaseScale_;
    hasBaseScale_ = true;
    encounterAuthoredStateCaptured_ = true;
}

void EnemyMagmaSlime::InitializeEncounterState() {
    encounterInitializedForPlay_ = true;
    CaptureEncounterAuthoredState();
    attackCycleIndex_ = attackCycle_.size();
    lastAutomaticAttack_ = AttackKind::None;
    const bool startActive = param_.has_value() && param_->startActive;
    if (startActive || encounterRequestedActive_) {
        BeginEncounterAppearance();
    } else {
        ApplyDormantEncounterState();
    }
}

void EnemyMagmaSlime::ResetEncounterStateForEditor() {
    if (encounterInitializedForPlay_) {
        SetTranslate(encounterBasePosition_);
        SetScale(encounterBaseScale_);
        SetRotation(encounterBaseRotation_);
        SetColor(encounterBaseColor_);
        SetMaterialType(encounterMaterialType_);
        SetEmissive(encounterBaseEmissive_);
        SyncCollisionRadius();
    }
    CaptureEncounterAuthoredState(true);
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    encounterInitializedForPlay_ = false;
    encounterRequestedActive_ = false;
    attackCycleIndex_ = attackCycle_.size();
    lastAutomaticAttack_ = AttackKind::None;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearTransientVisuals();
    HideAttackTelegraph();
}

void EnemyMagmaSlime::BeginEncounterAppearance() {
    encounterState_ = EncounterState::Appearing;
    encounterTimer_ = 0.0f;
    attackCooldown_ = 1.1f;
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    ClearTransientVisuals();
    HideAttackTelegraph();
    VFXSequencer::PlayOneShot(
        kEncounterAppearanceSequence,
        encounterBasePosition_ + Vector3{ 0.0f, 0.18f, 0.0f },
        { 1.55f, 1.55f, 1.55f });
}

void EnemyMagmaSlime::ApplyDormantEncounterState() {
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    SetTranslate(encounterBasePosition_);
    SetScale(encounterBaseScale_);
    SetRotation(encounterBaseRotation_);
    SetColor(encounterBaseColor_);
    SetMaterialType(encounterMaterialType_);
    SetEmissive(encounterBaseEmissive_);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearTransientVisuals();
    HideAttackTelegraph();
}

void EnemyMagmaSlime::FinishEncounterAppearance() {
    encounterState_ = EncounterState::Active;
    encounterTimer_ = 0.0f;
    SetTranslate(encounterBasePosition_);
    SetScale(encounterBaseScale_);
    SetRotation(encounterBaseRotation_);
    SetColor(encounterBaseColor_);
    SetMaterialType(encounterMaterialType_);
    SetEmissive(encounterBaseEmissive_);
    SetCollisionAttribute(encounterCollisionAttribute_);
    SetCollisionMask(encounterCollisionMask_);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetIsVisible(true);
    SyncCollisionRadius();
}

bool EnemyMagmaSlime::IsEncounterControlled() const {
    return param_.has_value() && param_->actionMode == kEncounterControlledActionMode;
}

float EnemyMagmaSlime::GetEncounterAppearanceDuration() const {
    return param_.has_value()
        ? (std::clamp)(param_->shakeDuration, 0.45f, 3.0f)
        : kDefaultEncounterAppearanceDuration;
}

bool EnemyMagmaSlime::IsEncounterHudActive() const {
    if (!IsEncounterControlled() || isDead) {
        return false;
    }
    return encounterState_ == EncounterState::Appearing || encounterState_ == EncounterState::Active;
}

float EnemyMagmaSlime::GetEncounterCurrentHp() const {
    return param_.has_value() ? (std::max)(0.0f, param_->hp) : 0.0f;
}

float EnemyMagmaSlime::GetEncounterMaximumHp() const {
    return param_.has_value() ? (std::max)(1.0f, param_->maxHp) : 1.0f;
}

float EnemyMagmaSlime::GetEncounterAppearanceProgress() const {
    if (encounterState_ == EncounterState::Active) {
        return 1.0f;
    }
    if (encounterState_ != EncounterState::Appearing) {
        return 0.0f;
    }
    return std::clamp(encounterTimer_ / GetEncounterAppearanceDuration(), 0.0f, 1.0f);
}
