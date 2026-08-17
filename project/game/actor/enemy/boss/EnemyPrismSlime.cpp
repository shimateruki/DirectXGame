#define NOMINMAX
#include "EnemyPrismSlime.h"

#include "BulletManager.h"
#include "Character.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "DebrisEffectManager.h"
#include "EffectObject3d.h"
#include "EnemyFactory.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "SlimeBounceAnimator.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
constexpr const char* kCrystalSpikesAttackId = "crystal_spikes";
constexpr const char* kCrystalLanceVolleyAttackId = "crystal_lance_volley";
constexpr const char* kFireFanAttackId = "fire_fan";
constexpr const char* kThunderChainAttackId = "thunder_chain";
constexpr const char* kWindWaveAttackId = "wind_wave";
constexpr const char* kSlimeSummonAttackId = "slime_summon";

constexpr const char* kPrismChargePreset = "prism_slime_charge";
constexpr const char* kPrismPhasePulsePreset = "prism_slime_pulse";
constexpr const char* kPrismSpikeWarningPreset = "prism_spike_warning";
constexpr const char* kPrismSpikeBurstPreset = "prism_spike_burst";
constexpr const char* kPrismSpikeShatterDebrisPreset = "prism_crystal_shatter";
constexpr const char* kPrismLanceTrailPreset = "prism_lance_trail";
constexpr const char* kPrismSpellTelegraphTexture = "Resources/sprite/effect/prism/prism_spell_circle.dds";
constexpr const char* kPrismSpikeGroundEffect = "Resources/json/effect/effect_prism_spike_ground_flash.json";
constexpr const char* kFireCastPreset = "fire_slime_cast";
constexpr const char* kThunderChargePreset = "thunder_slime_charge";
constexpr const char* kThunderImpactPreset = "player_thunder_strike_impact";
constexpr const char* kWindChargePreset = "wind_slime_charge";
constexpr const char* kWindStreamPreset = "wind_slime_breath_stream";
constexpr const char* kWindImpactPreset = "wind_slime_gust_impact";

constexpr const char* kThunderWarningEffect = "Resources/json/effect/effect_player_thunder_warning.json";
constexpr const char* kThunderBoltEffect = "Resources/json/effect/effect_player_thunder_bolt.json";
constexpr const char* kThunderCoreEffect = "Resources/json/effect/effect_player_thunder_core.json";
constexpr const char* kThunderImpactEffect = "Resources/json/effect/effect_player_thunder_impact_ring.json";
constexpr const char* kThunderScorchEffect = "Resources/json/effect/effect_thunder_scorch_mark.json";
constexpr const char* kWindRingEffect = "Resources/json/effect/effect_wind_gust_ring.json";

constexpr int kFireMaterialType = 11;
constexpr int kPrismCrystalMaterialType = 27;
constexpr float kPi = 3.1415926535f;
constexpr float kHalfPi = kPi * 0.5f;
constexpr float kGroundCollisionWorldRadius = 3.25f;
constexpr float kPrismSpikeInterval = 0.055f;
constexpr float kPrismSpikeRiseDuration = 0.24f;
constexpr float kPrismSpikeHoldDuration = 0.92f;
constexpr float kPrismSpikeFractureDuration = 0.10f;
constexpr float kPrismSpikeLifetime = kPrismSpikeRiseDuration + kPrismSpikeHoldDuration + kPrismSpikeFractureDuration;
constexpr float kCrystalLanceInterval = 0.12f;
constexpr float kCrystalLanceTrailInterval = 0.045f;
constexpr int kCrystalLanceCount = 5;
constexpr float kFireFanInterval = 0.095f;
constexpr int kFireFanProjectileCount = 5;
constexpr float kThunderStrikeInterval = 0.115f;
constexpr float kWindPushInterval = 0.12f;
constexpr float kSummonInterval = 0.17f;
constexpr float kSummonPortalFadeDuration = 0.34f;
constexpr int kSummonPortalCount = 3;
constexpr int kMaxSummonWaves = 2;

float NextSummonRandom01(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

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

Vector3 RotatePlanar(const Vector3& direction, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return NormalizePlanar({
        direction.x * cosine + direction.z * sine,
        0.0f,
        direction.z * cosine - direction.x * sine,
    });
}

Vector3 MakeUpAxisAimRotation(const Vector3& direction) {
    const Vector3 normalized = Math::Normalize(direction);
    const float horizontalLength = std::sqrt(
        normalized.x * normalized.x + normalized.z * normalized.z);
    return {
        std::atan2(horizontalLength, normalized.y),
        std::atan2(normalized.x, normalized.z),
        0.0f,
    };
}

float DistanceSquaredToSegment(const Vector3& point, const Vector3& start, const Vector3& end) {
    const Vector3 segment = end - start;
    const float lengthSquared = Math::Dot(segment, segment);
    if (lengthSquared <= 0.000001f) {
        const Vector3 difference = point - start;
        return Math::Dot(difference, difference);
    }
    const float rate = std::clamp(Math::Dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
    const Vector3 nearest = start + segment * rate;
    const Vector3 difference = point - nearest;
    return Math::Dot(difference, difference);
}

BulletVisualConfig MakePrismFireVisual() {
    BulletVisualConfig visual;
    visual.materialType = kFireMaterialType;
    visual.blendMode = BlendMode::kNormal;
    visual.color = { 1.0f, 0.30f, 0.055f, 0.98f };
    visual.emissive = 3.0f;
    visual.visualScale = 1.22f;
    visual.effectType = 1.0f;
    visual.effectScale = 1.18f;
    visual.effectSoftness = 0.38f;
    visual.effectIntensity = 1.08f;
    visual.billboardScale = 0.74f;
    visual.trailPreset = "fire_slime_breath_embers";
    visual.impactPreset = "fire_slime_cast";
    visual.trailInterval = 0.045f;
    visual.trailSpeedScale = 1.35f;
    return visual;
}

StatusEffectApplication MakeBurningStatus(const EnemyAttackDefinition& attack) {
    StatusEffectApplication status;
    status.type = StatusEffectType::Burning;
    status.duration = attack.statusDuration;
    status.tickInterval = attack.statusTickInterval;
    status.tickDamage = attack.statusTickDamage;
    status.vfxPreset = attack.statusVfx;
    return status;
}
}

EnemyPrismSlime::~EnemyPrismSlime() = default;

void EnemyPrismSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_PrismSlime");
    SetEnemyType("PrismSlime");
    ReloadAttackProfile();
    SetMaterialType(kPrismCrystalMaterialType);
    SetColor({ 0.34f, 0.22f, 0.62f, 1.0f });
    SetMetallic(0.84f);
    SetRoughness(0.12f);
    SetEnableEnvMap(true);
    SetEnvIntensity(1.0f);
    SetEmissive(0.92f);
    defaultColor_ = GetColor();

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncCollisionRadius();

    InitializeFaceParts();
    UpdatePhaseAppearance();
}

void EnemyPrismSlime::Update(float deltaTime) {
    if (UpdateInactiveState(deltaTime)) {
        UpdateFaceParts(deltaTime);
        return;
    }

    UpdatePrismSpikeVisuals(deltaTime);
    UpdateCrystalLanceVisuals(deltaTime);
    UpdateSummonPortalVisuals(deltaTime);
    EnsureBaseScale();
    idleTimer_ += deltaTime;
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    impactPulseTimer_ = (std::max)(0.0f, impactPulseTimer_ - deltaTime);
    UpdateElementPhase();

    Vector3 direction = lockedDirection_;
    float distance = 9999.0f;
    if (target_) {
        const Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    }

    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    UpdateBehavior(deltaTime, direction, distance, velocity);
    SetVelocity(velocity);

    ApplySlimeAnimation(deltaTime);
    SyncCollisionRadius();
    BaseEnemy::Update(deltaTime);
    UpdateFaceParts(deltaTime);
}

void EnemyPrismSlime::Draw(
    ID3D12Resource* pointLightResource,
    ID3D12Resource* spotLightResource) {
    BaseEnemy::Draw(pointLightResource, spotLightResource);
    for (const PrismSpikeVisual& spike : prismSpikeVisuals_) {
        if (spike.object) {
            // シーン管理外の内部描画物なので、現在の描画カメラを明示的に反映します。
            spike.object->RefreshRenderCameraData();
            spike.object->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const CrystalLanceVisual& lance : crystalLanceVisuals_) {
        if (lance.object) {
            lance.object->RefreshRenderCameraData();
            lance.object->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const SummonPortalVisual& portal : summonPortalVisuals_) {
        if (portal.effect && portal.effect->GetIsVisible()) {
            portal.effect->RefreshRenderCameraData();
            portal.effect->Draw(pointLightResource, spotLightResource);
        }
    }
    if (!GetIsVisible()) {
        return;
    }

    if (coreFramePart_) {
        coreFramePart_->Draw(pointLightResource, spotLightResource);
    }
    if (corePart_) {
        corePart_->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& eye : eyeParts_) {
        if (eye) {
            eye->Draw(pointLightResource, spotLightResource);
        }
    }
}

std::unique_ptr<Object3d> EnemyPrismSlime::Clone() const {
    auto clone = std::make_unique<EnemyPrismSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyPrismSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    debugPreviewAttackId_ = attackId;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackCooldown_ = 0.0f;
    attackTimer_ = 0.0f;
    actionTimer_ = 0.0f;
    actionIndex_ = 0;
    ClearPrismAttackVisuals();
    HideAttackTelegraph();
}

const char* EnemyPrismSlime::GetDebugAttackPhaseName() const {
    if (attackState_ == AttackState::Idle) {
        return "プリズム・待機／移動";
    }
    if (attackState_ == AttackState::Recovery) {
        return "プリズム・反発／回復";
    }

    switch (currentAttack_) {
    case AttackKind::PrismSpikes:
        return attackState_ == AttackState::Windup ? "プリズム棘・地面予兆" : "プリズム棘・連続隆起";
    case AttackKind::CrystalLanceVolley:
        return attackState_ == AttackState::Windup ? "浮遊晶槍・照準" : "浮遊晶槍・時間差斉射";
    case AttackKind::FireFan:
        return attackState_ == AttackState::Windup ? "炎・吸気" : "炎・扇状連射";
    case AttackKind::ThunderChain:
        return attackState_ == AttackState::Windup ? "雷・予告" : "雷・連続落雷";
    case AttackKind::WindWave:
        return attackState_ == AttackState::Windup ? "風・圧縮" : "風・押し流し";
    case AttackKind::SlimeSummon:
        return attackState_ == AttackState::Windup ? "召喚陣・出現予告" : "召喚陣・スライム転送";
    default:
        return "プリズム・攻撃";
    }
}

void EnemyPrismSlime::ApplyManagedScale(const Vector3& scale) {
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
    SyncCollisionRadius();
}

bool EnemyPrismSlime::UpdateInactiveState(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        ClearPrismAttackVisuals();
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (isCarried_) {
        ClearPrismAttackVisuals();
        HideAttackTelegraph();
        return true;
    }
    if (IsThrowRecovering()) {
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

void EnemyPrismSlime::EnsureBaseScale() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyPrismSlime::UpdateElementPhase() {
    ElementPhase nextPhase = ElementPhase::Neutral;
    if (param_.has_value() && param_->maxHp > 0.001f) {
        const float ratio = std::clamp(param_->hp / param_->maxHp, 0.0f, 1.0f);
        if (ratio <= 0.25f) {
            nextPhase = ElementPhase::Wind;
        } else if (ratio <= 0.50f) {
            nextPhase = ElementPhase::Thunder;
        } else if (ratio <= 0.75f) {
            nextPhase = ElementPhase::Fire;
        }
    }

    if (const AttackKind previewAttack = ResolveDebugAttack(); previewAttack != AttackKind::None) {
        switch (previewAttack) {
        case AttackKind::FireFan: nextPhase = ElementPhase::Fire; break;
        case AttackKind::ThunderChain: nextPhase = ElementPhase::Thunder; break;
        case AttackKind::WindWave: nextPhase = ElementPhase::Wind; break;
        default: nextPhase = ElementPhase::Neutral; break;
        }
    }

    if (nextPhase != elementPhase_) {
        elementPhase_ = nextPhase;
        UpdatePhaseAppearance();
        impactPulseTimer_ = 0.32f;
        EmitPreset(kPrismPhasePulsePreset, GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.48f, 0.0f });
    }
}

void EnemyPrismSlime::UpdateBehavior(
    float deltaTime,
    const Vector3& direction,
    float distance,
    Vector3& velocity) {
    if (attackState_ != AttackState::Idle) {
        UpdateAttack(deltaTime, direction, distance);
        return;
    }

    if (!target_ || !param_.has_value()) {
        velocity = CalculateWanderVelocity(deltaTime, 0.55f, 0.52f);
        UpdateFacing(velocity);
        return;
    }

    UpdateFacing(direction);
    if (UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        return;
    }

    const AttackKind debugAttack = ResolveDebugAttack();
    const AttackKind plannedAttack = debugAttack != AttackKind::None ? debugAttack : ResolveAutomaticAttack();
    const EnemyAttackDefinition& plannedDefinition = GetAttackDefinition(GetAttackId(plannedAttack));
    const bool isInAttackRange =
        distance >= plannedDefinition.minRange && distance <= plannedDefinition.maxRange;
    if (distance <= detectionRange_ && attackCooldown_ <= 0.0f && isInAttackRange) {
        StartAttack(plannedAttack, direction, distance);
        return;
    }

    const float moveSpeed = (std::max)(0.75f, param_->speed);
    const float approachDistance = (std::min)(9.0f, (std::max)(2.0f, plannedDefinition.maxRange * 0.86f));
    if (distance > approachDistance) {
        velocity.x = direction.x * moveSpeed;
        velocity.z = direction.z * moveSpeed;
    } else if (distance < 4.2f) {
        velocity.x = -direction.x * moveSpeed * 0.62f;
        velocity.z = -direction.z * moveSpeed * 0.62f;
    }
}

void EnemyPrismSlime::StartAttack(AttackKind kind, const Vector3& direction, float distance) {
    if (kind == AttackKind::None) {
        return;
    }

    currentAttack_ = kind;
    attackState_ = AttackState::Windup;
    lockedDirection_ = NormalizePlanar(direction);
    lockedTargetDistance_ = distance;
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackStateDuration_ = (std::max)(0.08f, attack.windupDuration);
    attackTimer_ = attackStateDuration_;
    effectTimer_ = 0.0f;
    actionTimer_ = 0.0f;
    actionIndex_ = 0;
    warningTriggered_ = false;
    damageApplied_ = false;

    if (kind == AttackKind::PrismSpikes) {
        PreparePrismSpikePoints();
    } else if (kind == AttackKind::CrystalLanceVolley) {
        PrepareCrystalLances();
    } else if (kind == AttackKind::ThunderChain) {
        PrepareThunderPoints();
    } else if (kind == AttackKind::SlimeSummon) {
        PrepareSummonPortals();
        if (debugPreviewAttackId_.empty()) {
            ++summonWavesUsed_;
        }
    }
    if (debugPreviewAttackId_.empty()) {
        ++automaticAttackSerial_;
    }
}

void EnemyPrismSlime::UpdateAttack(
    float deltaTime,
    const Vector3& direction,
    float distance) {
    if (attackState_ == AttackState::Windup) {
        if (attackTimer_ > GetCurrentAttackDefinition().warningLeadTime) {
            lockedDirection_ = NormalizePlanar(direction);
            lockedTargetDistance_ = distance;
        }
        UpdateFacing(lockedDirection_);
        UpdateWindup(deltaTime);
        return;
    }
    if (attackState_ == AttackState::Active) {
        UpdateFacing(lockedDirection_);
        UpdateActive(deltaTime, distance);
        return;
    }
    if (attackState_ == AttackState::Recovery) {
        attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
        HideAttackTelegraph();
        if (attackTimer_ <= 0.0f) {
            FinishAttack();
        }
    }
}

void EnemyPrismSlime::UpdateWindup(float deltaTime) {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    effectTimer_ -= deltaTime;
    const float progress = 1.0f - std::clamp(attackTimer_ / attackStateDuration_, 0.0f, 1.0f);
    const Vector3 center = GetTranslate();

    switch (currentAttack_) {
    case AttackKind::PrismSpikes: {
        ShowAttackTelegraphDecalCircle(
            prismSpellCenter_,
            attack.radius,
            progress,
            { 1.0f, 1.0f, 1.0f, 0.74f },
            kPrismSpellTelegraphTexture);
        if (effectTimer_ <= 0.0f) {
            const char* windupPreset = attack.windupVfx.empty()
                ? kPrismSpikeWarningPreset
                : attack.windupVfx.c_str();
            EmitPreset(windupPreset, prismSpellCenter_ + Vector3{ 0.0f, 0.08f, 0.0f });
            EmitPreset(kPrismChargePreset, GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.50f, 0.0f });
            effectTimer_ += 0.15f - progress * 0.035f;
        }
        break;
    }
    case AttackKind::CrystalLanceVolley: {
        const float predictedDistance = std::clamp(
            lockedTargetDistance_ + attack.radius,
            attack.minRange,
            attack.maxRange);
        ShowAttackTelegraphLaneFan(
            center,
            lockedDirection_,
            predictedDistance,
            attack.radius * 1.20f,
            kCrystalLanceCount,
            1.08f,
            -0.060f,
            progress,
            { 0.50f, 0.78f, 1.0f, 0.72f });
        if (effectTimer_ <= 0.0f) {
            EmitPreset(kPrismChargePreset,
                GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.66f, 0.0f });
            effectTimer_ += 0.10f - progress * 0.03f;
        }
        break;
    }
    case AttackKind::FireFan: {
        const float fanHalfAngle = static_cast<float>(kFireFanProjectileCount - 1) * 0.5f * 0.13f;
        const float fanEndWidth = attack.maxRange * std::tan(fanHalfAngle) * 2.0f + attack.radius * 2.0f;
        ShowAttackTelegraphCone(center, lockedDirection_, attack.maxRange,
            attack.radius * 1.35f, fanEndWidth, progress, { 1.0f, 0.27f, 0.055f, 0.80f });
        if (effectTimer_ <= 0.0f) {
            const Vector3 corePosition = GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.48f, 0.0f };
            EmitDirectedPreset(kFireCastPreset, corePosition, lockedDirection_, 0.72f + progress * 0.55f);
            effectTimer_ += 0.085f;
        }
        break;
    }
    case AttackKind::ThunderChain:
        ShowAttackTelegraphImpactAreas(
            thunderStrikePositions_.data(),
            thunderStrikePositions_.size(),
            attack.radius,
            progress,
            { 1.0f, 0.92f, 0.14f, 0.84f });
        if (effectTimer_ <= 0.0f) {
            EmitPreset(kThunderChargePreset, GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.58f, 0.0f });
            effectTimer_ += 0.065f - progress * 0.025f;
        }
        break;
    case AttackKind::WindWave:
        ShowAttackTelegraphCone(center, lockedDirection_, attack.maxRange,
            attack.radius * 0.46f, attack.radius * 2.0f,
            progress, { 0.45f, 1.0f, 0.82f, 0.80f });
        if (effectTimer_ <= 0.0f) {
            EmitDirectedPreset(kWindChargePreset,
                GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.42f, 0.0f },
                lockedDirection_ * -1.0f + Vector3{ 0.0f, 0.16f, 0.0f }, 0.85f + progress * 0.70f);
            effectTimer_ += 0.07f;
        }
        break;
    case AttackKind::SlimeSummon:
        if (effectTimer_ <= 0.0f) {
            EmitPreset(kPrismChargePreset,
                GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 0.52f, 0.0f });
            effectTimer_ += 0.12f - progress * 0.035f;
        }
        break;
    default:
        break;
    }

    if (!warningTriggered_ && attackTimer_ <= attack.warningLeadTime) {
        Vector4 cueColor = { 0.52f, 0.96f, 1.0f, 1.0f };
        if (currentAttack_ == AttackKind::FireFan) cueColor = { 1.0f, 0.28f, 0.05f, 1.0f };
        if (currentAttack_ == AttackKind::ThunderChain) cueColor = { 1.0f, 0.92f, 0.12f, 1.0f };
        if (currentAttack_ == AttackKind::WindWave) cueColor = { 0.58f, 1.0f, 0.86f, 1.0f };
        if (currentAttack_ == AttackKind::SlimeSummon) cueColor = { 0.96f, 0.58f, 1.0f, 1.0f };
        if (currentAttack_ == AttackKind::CrystalLanceVolley) cueColor = { 0.58f, 0.82f, 1.0f, 1.0f };
        TriggerAttackTelegraphCue(cueColor);
        warningTriggered_ = true;
    }

    if (attackTimer_ <= 0.0f) {
        attackState_ = AttackState::Active;
        attackStateDuration_ = (std::max)(0.08f, attack.activeDuration);
        attackTimer_ = attackStateDuration_;
        effectTimer_ = 0.0f;
        actionTimer_ = 0.0f;
        actionIndex_ = 0;
        HideAttackTelegraph();
    }
}

void EnemyPrismSlime::UpdateActive(float deltaTime, float targetDistance) {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    effectTimer_ -= deltaTime;
    actionTimer_ -= deltaTime;

    switch (currentAttack_) {
    case AttackKind::PrismSpikes:
        while (actionIndex_ < static_cast<int>(prismSpikePositions_.size()) && actionTimer_ <= 0.0f) {
            SpawnNextPrismSpike();
            ++actionIndex_;
            actionTimer_ += kPrismSpikeInterval;
        }
        break;
    case AttackKind::CrystalLanceVolley:
        while (actionIndex_ < static_cast<int>(crystalLanceVisuals_.size()) && actionTimer_ <= 0.0f) {
            LaunchNextCrystalLance();
            ++actionIndex_;
            actionTimer_ += kCrystalLanceInterval;
        }
        break;
    case AttackKind::FireFan:
        while (actionIndex_ < kFireFanProjectileCount && actionTimer_ <= 0.0f) {
            FireNextFanProjectile();
            ++actionIndex_;
            actionTimer_ += kFireFanInterval;
        }
        break;
    case AttackKind::ThunderChain:
        while (actionIndex_ < static_cast<int>(thunderStrikePositions_.size()) && actionTimer_ <= 0.0f) {
            StrikeNextThunderPoint();
            ++actionIndex_;
            actionTimer_ += kThunderStrikeInterval;
        }
        if (actionIndex_ < static_cast<int>(thunderStrikePositions_.size())) {
            const std::size_t remainingCount = thunderStrikePositions_.size() -
                static_cast<std::size_t>(actionIndex_);
            const float strikeProgress = 1.0f - std::clamp(
                actionTimer_ / kThunderStrikeInterval,
                0.0f,
                1.0f);
            ShowAttackTelegraphImpactAreas(
                thunderStrikePositions_.data() + actionIndex_,
                remainingCount,
                attack.radius,
                strikeProgress,
                { 1.0f, 0.92f, 0.14f, 0.78f });
        }
        break;
    case AttackKind::WindWave:
        if (effectTimer_ <= 0.0f) {
            const float progress = 1.0f - std::clamp(attackTimer_ / attackStateDuration_, 0.0f, 1.0f);
            const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };
            const float wave = std::sin(progress * kPi * 8.0f) * 0.42f;
            const Vector3 origin = GetWorldPosition() + lockedDirection_ * (1.0f + progress * attack.maxRange * 0.78f) +
                side * wave + Vector3{ 0.0f, baseScale_.y * 0.34f, 0.0f };
            EmitDirectedPreset(kWindStreamPreset, origin, lockedDirection_, 1.35f + progress * 0.9f);
            effectTimer_ += 0.042f;
        }
        if (actionTimer_ <= 0.0f) {
            ApplyWindWave(targetDistance);
            actionTimer_ += kWindPushInterval;
        }
        break;
    case AttackKind::SlimeSummon:
        while (actionIndex_ < static_cast<int>(summonPortalVisuals_.size()) && actionTimer_ <= 0.0f) {
            SpawnNextSummonedSlime();
            ++actionIndex_;
            actionTimer_ += kSummonInterval;
        }
        break;
    default:
        break;
    }

    if (attackTimer_ <= 0.0f) {
        BeginRecovery();
    }
}

void EnemyPrismSlime::BeginRecovery() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackState_ = AttackState::Recovery;
    attackStateDuration_ = (std::max)(0.08f, attack.recoveryDuration);
    attackTimer_ = attackStateDuration_;
    attackCooldown_ = (std::max)(0.2f, attack.cooldown);
    impactPulseTimer_ = 0.26f;
    HideAttackTelegraph();
}

void EnemyPrismSlime::FinishAttack() {
    if (currentAttack_ == AttackKind::SlimeSummon) {
        summonPortalVisuals_.clear();
    }
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackTimer_ = 0.0f;
    actionTimer_ = 0.0f;
    actionIndex_ = 0;
    warningTriggered_ = false;
    damageApplied_ = false;
}

EnemyPrismSlime::AttackKind EnemyPrismSlime::ResolveAutomaticAttack() const {
    if (summonWavesUsed_ < kMaxSummonWaves && automaticAttackSerial_ % 3 == 1) {
        return AttackKind::SlimeSummon;
    }
    if (automaticAttackSerial_ % 4 == 2) {
        return AttackKind::CrystalLanceVolley;
    }
    switch (elementPhase_) {
    case ElementPhase::Fire: return AttackKind::FireFan;
    case ElementPhase::Thunder: return AttackKind::ThunderChain;
    case ElementPhase::Wind: return AttackKind::WindWave;
    default: return AttackKind::PrismSpikes;
    }
}

EnemyPrismSlime::AttackKind EnemyPrismSlime::ResolveDebugAttack() const {
    if (debugPreviewAttackId_ == kCrystalSpikesAttackId) return AttackKind::PrismSpikes;
    if (debugPreviewAttackId_ == kCrystalLanceVolleyAttackId) return AttackKind::CrystalLanceVolley;
    if (debugPreviewAttackId_ == kFireFanAttackId) return AttackKind::FireFan;
    if (debugPreviewAttackId_ == kThunderChainAttackId) return AttackKind::ThunderChain;
    if (debugPreviewAttackId_ == kWindWaveAttackId) return AttackKind::WindWave;
    if (debugPreviewAttackId_ == kSlimeSummonAttackId) return AttackKind::SlimeSummon;
    return AttackKind::None;
}

const EnemyAttackDefinition& EnemyPrismSlime::GetCurrentAttackDefinition() const {
    return GetAttackDefinition(GetAttackId(currentAttack_));
}

const char* EnemyPrismSlime::GetAttackId(AttackKind kind) const {
    switch (kind) {
    case AttackKind::CrystalLanceVolley: return kCrystalLanceVolleyAttackId;
    case AttackKind::FireFan: return kFireFanAttackId;
    case AttackKind::ThunderChain: return kThunderChainAttackId;
    case AttackKind::WindWave: return kWindWaveAttackId;
    case AttackKind::SlimeSummon: return kSlimeSummonAttackId;
    default: return kCrystalSpikesAttackId;
    }
}

void EnemyPrismSlime::PreparePrismSpikePoints() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const Vector3 casterPosition = GetWorldPosition();
    Vector3 requestedCenter = casterPosition + lockedDirection_ *
        std::clamp(lockedTargetDistance_, 0.0f, (std::max)(attack.maxRange, 0.1f));
    if (target_) {
        requestedCenter = target_->GetWorldPosition();
        Vector3 fromCaster = requestedCenter - casterPosition;
        fromCaster.y = 0.0f;
        const float targetDistance = std::sqrt(fromCaster.x * fromCaster.x + fromCaster.z * fromCaster.z);
        if (targetDistance > attack.maxRange && targetDistance > 0.001f) {
            requestedCenter = casterPosition + fromCaster * (attack.maxRange / targetDistance);
        }
    }
    prismSpellCenter_ = FindGroundPoint(requestedCenter);

    constexpr std::array<float, 6> angleOffsets = {
        0.0f, kPi / 3.0f, -kPi / 3.0f, kPi * 2.0f / 3.0f, -kPi * 2.0f / 3.0f, kPi,
    };
    constexpr std::array<float, 6> radiusRates = {
        0.58f, 0.66f, 0.72f, 0.78f, 0.70f, 0.62f,
    };

    for (std::size_t index = 0; index < angleOffsets.size(); ++index) {
        const Vector3 radialDirection = RotatePlanar(lockedDirection_, angleOffsets[index]);
        const float distance = (std::max)(attack.radius, 1.0f) * radiusRates[index];
        prismSpikePositions_[index] = FindGroundPoint(
            prismSpellCenter_ + radialDirection * distance);
    }
    // 最後に出す主棘は、詠唱地点の中心へ配置します。
    prismSpikePositions_.back() = prismSpellCenter_;
}

std::unique_ptr<Object3d> EnemyPrismSlime::CreatePrismSpikeObject(int index) const {
    if (!common_) {
        return nullptr;
    }

    auto spike = std::make_unique<Object3d>();
    spike->Initialize(common_);
    spike->SetName("PrismSpike_" + std::to_string(index));
    spike->SetClassName("EnemyVisualPart");
    spike->SetModel("Effects/prism_crystal_spike");
    spike->SetColliderType(ColliderType::kNone);
    spike->SetCollisionAttribute(0);
    spike->SetCollisionMask(0);
    spike->SetMaterialType(kPrismCrystalMaterialType);

    constexpr std::array<Vector4, 3> kCrystalTints = {
        Vector4{ 0.34f, 0.20f, 0.74f, 1.0f },
        Vector4{ 0.20f, 0.43f, 0.78f, 1.0f },
        Vector4{ 0.66f, 0.22f, 0.70f, 1.0f },
    };
    spike->SetColor(kCrystalTints[static_cast<std::size_t>(index) % kCrystalTints.size()]);
    spike->SetMetallic(0.94f);
    spike->SetRoughness(0.11f);
    spike->SetEnableEnvMap(true);
    spike->SetEnvIntensity(1.12f);
    spike->SetEmissive(0.96f);
    spike->SetBlendMode(BlendMode::kNone);
    return spike;
}

void EnemyPrismSlime::SpawnNextPrismSpike() {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(prismSpikePositions_.size())) {
        return;
    }

    PrismSpikeVisual visual;
    visual.object = CreatePrismSpikeObject(actionIndex_);
    if (!visual.object) {
        return;
    }

    const bool isCenterSpike = actionIndex_ == static_cast<int>(prismSpikePositions_.size()) - 1;
    const float heightVariation = isCenterSpike
        ? 1.58f
        : 0.88f + static_cast<float>((actionIndex_ * 5) % 4) * 0.055f;
    const float widthVariation = isCenterSpike
        ? 1.18f
        : 0.62f + static_cast<float>((actionIndex_ * 3) % 3) * 0.065f;
    visual.groundPosition = prismSpikePositions_[actionIndex_];
    visual.fullScale = { widthVariation, 1.22f * heightVariation, widthVariation };
    visual.object->SetTranslate(visual.groundPosition + Vector3{ 0.0f, -0.24f, 0.0f });
    visual.object->SetRotation({
        0.0f,
        std::atan2(lockedDirection_.x, lockedDirection_.z) + static_cast<float>(actionIndex_ - 2) * 0.13f,
        0.0f,
    });
    visual.object->SetScale({ visual.fullScale.x * 0.42f, 0.015f, visual.fullScale.z * 0.42f });
    visual.object->Update(0.0f);
    visual.object->UpdateWorldMatrix(true);

    const float groundEffectScale = isCenterSpike ? 1.16f : 0.74f;
    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAt(
            kPrismSpikeGroundEffect,
            visual.groundPosition + Vector3{ 0.0f, 0.045f, 0.0f },
            { 0.0f, static_cast<float>(actionIndex_) * 0.37f, 0.0f },
            { groundEffectScale, 1.0f, groundEffectScale });
    }

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const char* activePreset = attack.activeVfx.empty()
        ? kPrismSpikeBurstPreset
        : attack.activeVfx.c_str();
    EmitDirectedPreset(activePreset,
        visual.groundPosition + Vector3{ 0.0f, 0.12f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, isCenterSpike ? 1.04f : 0.68f);
    if (actionIndex_ == 0) {
        ApplyPrismSpellDamage();
    }
    prismSpikeVisuals_.push_back(std::move(visual));
}

void EnemyPrismSlime::UpdatePrismSpikeVisuals(float deltaTime) {
    auto spike = prismSpikeVisuals_.begin();
    while (spike != prismSpikeVisuals_.end()) {
        spike->age += (std::max)(deltaTime, 0.0f);

        Vector3 scale = spike->fullScale;
        Vector3 position = spike->groundPosition;
        if (spike->age < kPrismSpikeRiseDuration) {
            const float progress = SmoothStep01(spike->age / kPrismSpikeRiseDuration);
            const float overshoot = std::sin(progress * kPi) * 0.12f;
            scale.x *= std::lerp(0.42f, 1.0f, progress);
            scale.y *= (std::max)(0.015f, progress + overshoot);
            scale.z *= std::lerp(0.42f, 1.0f, progress);
            position.y -= (1.0f - progress) * 0.24f;
        } else if (spike->age < kPrismSpikeRiseDuration + kPrismSpikeHoldDuration) {
            const float shimmer = std::sin((spike->age - kPrismSpikeRiseDuration) * 13.0f) * 0.012f;
            scale.x *= 1.0f + shimmer;
            scale.z *= 1.0f - shimmer;
        } else {
            const float fractureTime = spike->age - kPrismSpikeRiseDuration - kPrismSpikeHoldDuration;
            const float fracture = SmoothStep01(fractureTime / kPrismSpikeFractureDuration);
            const float tension = std::sin(fracture * kPi);
            scale.x *= 1.0f + tension * 0.075f;
            scale.y *= 1.0f + tension * 0.035f;
            scale.z *= 1.0f + tension * 0.075f;
            position.y += tension * 0.035f;
        }

        spike->object->SetTranslate(position);
        spike->object->SetScale(scale);
        spike->object->Update(deltaTime);
        // このObject3dはSceneの所有物ではないため、通常の一括GPU更新対象に含まれません。
        spike->object->UpdateWorldMatrix(true);

        if (spike->age >= kPrismSpikeLifetime) {
            SpawnPrismSpikeShatter(*spike);
            spike = prismSpikeVisuals_.erase(spike);
        } else {
            ++spike;
        }
    }
}

void EnemyPrismSlime::SpawnPrismSpikeShatter(const PrismSpikeVisual& spike) {
    const bool isMainSpike = spike.fullScale.x > 1.0f;
    const Vector3 burstPosition = spike.groundPosition + Vector3{
        0.0f,
        spike.fullScale.y * 1.18f,
        0.0f,
    };

    if (DebrisEffectManager* debris = DebrisEffectManager::GetInstance()) {
        debris->SpawnOnGround(
            kPrismSpikeShatterDebrisPreset,
            burstPosition,
            spike.groundPosition.y + 0.02f);
        if (isMainSpike) {
            debris->SpawnOnGround(
                kPrismSpikeShatterDebrisPreset,
                burstPosition + Vector3{ 0.0f, spike.fullScale.y * 0.82f, 0.0f },
                spike.groundPosition.y + 0.02f);
        }
    }

    EmitDirectedPreset(
        kPrismSpikeBurstPreset,
        burstPosition,
        { 0.0f, 1.0f, 0.0f },
        isMainSpike ? 1.34f : 0.92f);

    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        const float groundFlashScale = isMainSpike ? 1.28f : 0.78f;
        effects->SpawnEffectAt(
            kPrismSpikeGroundEffect,
            spike.groundPosition + Vector3{ 0.0f, 0.05f, 0.0f },
            { 0.0f, spike.age * 0.71f, 0.0f },
            { groundFlashScale, 1.0f, groundFlashScale });
    }
}

void EnemyPrismSlime::PrepareCrystalLances() {
    crystalLanceVisuals_.clear();
    crystalLanceVisuals_.reserve(kCrystalLanceCount);
    crystalVolleyDamageApplied_ = false;

    for (int index = 0; index < kCrystalLanceCount; ++index) {
        CrystalLanceVisual lance;
        lance.object = CreatePrismSpikeObject(index + 10);
        lance.slotIndex = index;
        if (!lance.object) {
            continue;
        }

        lance.object->SetName("PrismCrystalLance_" + std::to_string(index));
        lance.object->SetScale({ 0.01f, 0.01f, 0.01f });
        lance.object->SetTranslate(GetWorldPosition());
        lance.object->Update(0.0f);
        lance.object->UpdateWorldMatrix(true);
        crystalLanceVisuals_.push_back(std::move(lance));
    }
}

void EnemyPrismSlime::LaunchNextCrystalLance() {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(crystalLanceVisuals_.size())) {
        return;
    }

    CrystalLanceVisual& lance = crystalLanceVisuals_[static_cast<std::size_t>(actionIndex_)];
    if (!lance.object || lance.launched) {
        return;
    }

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 aimPosition = lance.position + lockedDirection_ * (std::max)(attack.maxRange, 1.0f);
    if (target_) {
        aimPosition = target_->GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f };
    }

    Vector3 launchDirection = aimPosition - lance.position;
    if (Math::Length(launchDirection) <= 0.001f) {
        launchDirection = lockedDirection_;
    } else {
        launchDirection = Math::Normalize(launchDirection);
    }

    lance.velocity = launchDirection * (std::max)(12.0f, attack.maxSpeed);
    lance.launched = true;
    lance.age = 0.0f;
    lance.trailTimer = 0.0f;
    lance.object->SetRotation(MakeUpAxisAimRotation(launchDirection));
    lance.object->SetScale({ 0.42f, 0.72f, 0.42f });
    const char* trailPreset = attack.activeVfx.empty()
        ? kPrismLanceTrailPreset
        : attack.activeVfx.c_str();
    EmitDirectedPreset(trailPreset, lance.position, launchDirection * -1.0f, 1.25f);
}

void EnemyPrismSlime::UpdateCrystalLanceVisuals(float deltaTime) {
    if (crystalLanceVisuals_.empty()) {
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kCrystalLanceVolleyAttackId);
    float revealProgress = 1.0f;
    if (currentAttack_ == AttackKind::CrystalLanceVolley && attackState_ == AttackState::Windup) {
        revealProgress = 1.0f - std::clamp(
            attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f);
    }
    const float reveal = SmoothStep01(revealProgress);
    const Vector3 side = { lockedDirection_.z, 0.0f, -lockedDirection_.x };

    auto lance = crystalLanceVisuals_.begin();
    while (lance != crystalLanceVisuals_.end()) {
        if (!lance->object) {
            lance = crystalLanceVisuals_.erase(lance);
            continue;
        }

        if (!lance->launched) {
            const float centeredSlot = static_cast<float>(lance->slotIndex) -
                static_cast<float>(kCrystalLanceCount - 1) * 0.5f;
            const float arch = 1.0f - std::abs(centeredSlot) * 0.17f;
            const float bob = std::sin(idleTimer_ * 4.6f + static_cast<float>(lance->slotIndex) * 0.9f) * 0.10f;
            lance->position = GetWorldPosition() +
                side * centeredSlot * 1.08f - lockedDirection_ * (0.58f + std::abs(centeredSlot) * 0.08f) +
                Vector3{ 0.0f, baseScale_.y * 0.68f + 1.28f + arch * 0.55f + bob, 0.0f };

            Vector3 aimPosition = lance->position + lockedDirection_ * (std::max)(attack.maxRange, 1.0f);
            if (target_) {
                aimPosition = target_->GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f };
            }
            Vector3 aimDirection = aimPosition - lance->position;
            if (Math::Length(aimDirection) <= 0.001f) {
                aimDirection = lockedDirection_;
            }

            const float pulse = std::sin(idleTimer_ * 7.2f + static_cast<float>(lance->slotIndex)) * 0.018f;
            lance->object->SetTranslate(lance->position);
            lance->object->SetRotation(MakeUpAxisAimRotation(aimDirection));
            lance->object->SetScale({
                (0.38f + pulse) * reveal,
                (0.67f - pulse) * reveal,
                (0.38f + pulse) * reveal,
            });
            lance->object->Update(deltaTime);
            lance->object->UpdateWorldMatrix(true);
            ++lance;
            continue;
        }

        lance->age += (std::max)(deltaTime, 0.0f);
        lance->trailTimer -= deltaTime;
        const Vector3 previousPosition = lance->position;
        lance->position += lance->velocity * deltaTime;

        if (lance->trailTimer <= 0.0f) {
            const char* trailPreset = attack.activeVfx.empty()
                ? kPrismLanceTrailPreset
                : attack.activeVfx.c_str();
            EmitDirectedPreset(
                trailPreset,
                lance->position,
                lance->velocity * -1.0f,
                1.0f);
            lance->trailTimer += kCrystalLanceTrailInterval;
        }

        bool hitTarget = false;
        if (target_) {
            const Vector3 targetCenter = target_->GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f };
            const float hitRadius = (std::max)(0.90f, target_->GetCollisionRadius() + 0.34f);
            hitTarget = DistanceSquaredToSegment(targetCenter, previousPosition, lance->position) <=
                hitRadius * hitRadius;
        }

        if (hitTarget && !crystalVolleyDamageApplied_) {
            const Vector3 knockbackDirection = NormalizePlanar(lance->velocity);
            DamageEvent event;
            event.target = target_;
            event.attacker = this;
            event.damageAmount = attack.damage;
            event.damageType = DamageType::Physical;
            event.knockbackVelocity = {
                knockbackDirection.x * 5.5f,
                4.2f,
                knockbackDirection.z * 5.5f,
            };
            EventManager::GetInstance()->Dispatch(event);
            crystalVolleyDamageApplied_ = true;
        }

        const bool expired = lance->age >= (std::max)(0.3f, attack.lifetime);
        if (hitTarget || expired) {
            SpawnCrystalLanceShatter(*lance);
            lance = crystalLanceVisuals_.erase(lance);
            continue;
        }

        lance->object->SetTranslate(lance->position);
        lance->object->SetRotation(MakeUpAxisAimRotation(lance->velocity));
        lance->object->Update(deltaTime);
        lance->object->UpdateWorldMatrix(true);
        ++lance;
    }
}

void EnemyPrismSlime::SpawnCrystalLanceShatter(const CrystalLanceVisual& lance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kCrystalLanceVolleyAttackId);
    const Vector3 groundPosition = FindGroundPoint(lance.position);
    if (DebrisEffectManager* debris = DebrisEffectManager::GetInstance()) {
        debris->SpawnOnGround(
            kPrismSpikeShatterDebrisPreset,
            lance.position,
            groundPosition.y + 0.02f);
    }
    const char* impactPreset = attack.impactVfx.empty()
        ? kPrismSpikeBurstPreset
        : attack.impactVfx.c_str();
    EmitDirectedPreset(
        impactPreset,
        lance.position,
        lance.velocity * -1.0f,
        1.08f);
}

void EnemyPrismSlime::ClearPrismAttackVisuals() {
    prismSpikeVisuals_.clear();
    crystalLanceVisuals_.clear();
    summonPortalVisuals_.clear();
}

void EnemyPrismSlime::ApplyPrismSpellDamage() {
    if (!target_ || damageApplied_) {
        return;
    }

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 difference = target_->GetWorldPosition() - prismSpellCenter_;
    difference.y = 0.0f;
    const float distance = std::sqrt(difference.x * difference.x + difference.z * difference.z);
    if (distance > attack.radius) {
        return;
    }

    const Vector3 direction = distance > 0.001f ? NormalizePlanar(difference) : lockedDirection_;
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = attack.damage;
    event.damageType = DamageType::Physical;
    event.knockbackVelocity = {
        direction.x * 5.5f,
        10.5f,
        direction.z * 5.5f,
    };
    EventManager::GetInstance()->Dispatch(event);
    damageApplied_ = true;
}

void EnemyPrismSlime::FireNextFanProjectile() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const float centerIndex = static_cast<float>(kFireFanProjectileCount - 1) * 0.5f;
    const float fanAngle = (static_cast<float>(actionIndex_) - centerIndex) * 0.13f;
    Vector3 direction = RotatePlanar(lockedDirection_, fanAngle);

    Vector3 spawnPosition = GetWorldPosition() + direction * (baseScale_.x * 0.48f);
    spawnPosition.y += baseScale_.y * 0.48f + std::abs(static_cast<float>(actionIndex_) - centerIndex) * 0.10f;
    const float speed = std::clamp(
        lockedTargetDistance_ * 1.55f + 4.0f,
        (std::max)(11.0f, attack.minSpeed),
        (std::max)(22.0f, attack.maxSpeed));
    const float lifetime = attack.lifetime > 0.0f ? attack.lifetime : 2.8f;

    BulletManager::GetInstance()->Fire(
        spawnPosition,
        direction * speed + Vector3{ 0.0f, 0.45f, 0.0f },
        kEnemyAttack,
        kPlayer | kAllSolid,
        "Primitives/sphere",
        (std::max)(0.42f, attack.radius),
        lifetime,
        MakePrismFireVisual(),
        attack.damage,
        MakeBurningStatus(attack),
        DamageType::Fire);
    EmitDirectedPreset(kFireCastPreset, spawnPosition, direction, 1.15f);
}

void EnemyPrismSlime::StrikeNextThunderPoint() {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(thunderStrikePositions_.size())) {
        return;
    }
    const Vector3 groundPosition = thunderStrikePositions_[actionIndex_];
    const float yaw = std::atan2(lockedDirection_.x, lockedDirection_.z);
    constexpr float strikeHeight = 10.5f;
    Vector3 strikeCenter = groundPosition;
    strikeCenter.y += strikeHeight * 0.5f;

    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAt(kThunderBoltEffect, strikeCenter, { kHalfPi, yaw, 0.0f }, { 1.15f, 1.18f, 1.15f });
        effects->SpawnEffectAt(kThunderBoltEffect, strikeCenter, { kHalfPi, yaw + kHalfPi, 0.0f }, { 0.76f, 1.18f, 0.90f });
        effects->SpawnEffectAt(kThunderCoreEffect, strikeCenter, { 0.0f, 0.0f, 0.0f }, { 1.18f, 1.18f, 1.18f });
        effects->SpawnEffectAt(kThunderImpactEffect, groundPosition + Vector3{ 0.0f, 0.055f, 0.0f },
            { 0.0f, yaw, 0.0f }, { 1.35f, 1.0f, 1.35f });
        effects->SpawnEffectAt(kThunderScorchEffect, groundPosition + Vector3{ 0.0f, 0.04f, 0.0f }, { 0.0f, yaw, 0.0f });
    }
    EmitPreset(kThunderImpactPreset, groundPosition + Vector3{ 0.0f, 0.22f, 0.0f });

    if (!target_ || damageApplied_) {
        return;
    }
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 difference = target_->GetWorldPosition() - groundPosition;
    const float verticalDistance = difference.y;
    difference.y = 0.0f;
    if (Math::Length(difference) > attack.radius || verticalDistance < -0.6f || verticalDistance > strikeHeight) {
        return;
    }
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = attack.damage;
    event.damageType = DamageType::Electric;
    event.knockbackVelocity = { lockedDirection_.x * 8.0f, 7.5f, lockedDirection_.z * 8.0f };
    EventManager::GetInstance()->Dispatch(event);
    damageApplied_ = true;
}

void EnemyPrismSlime::ApplyWindWave(float targetDistance) {
    if (!target_) {
        return;
    }
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    if (targetDistance > attack.maxRange + 0.9f) {
        return;
    }

    const Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
    const Vector3 targetDirection = NormalizePlanar(toTarget);
    if (Math::Dot(lockedDirection_, targetDirection) < 0.22f) {
        return;
    }

    const float falloff = 1.0f - std::clamp(targetDistance / (attack.maxRange + 0.9f), 0.0f, 1.0f);
    if (Character* character = dynamic_cast<Character*>(target_)) {
        character->ApplyExternalImpulse({
            lockedDirection_.x * (25.0f + falloff * 10.0f),
            6.2f + falloff * 3.0f,
            lockedDirection_.z * (25.0f + falloff * 10.0f),
        }, 0.25f);
    }

    EmitDirectedPreset(kWindImpactPreset, target_->GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f }, lockedDirection_, 1.25f);
    if (!damageApplied_ && attackTimer_ <= attackStateDuration_ * 0.62f) {
        DamageEvent event;
        event.target = target_;
        event.attacker = this;
        event.damageAmount = attack.damage;
        event.damageType = DamageType::Physical;
        event.knockbackVelocity = {
            lockedDirection_.x * (19.0f + falloff * 7.0f),
            7.0f + falloff * 2.0f,
            lockedDirection_.z * (19.0f + falloff * 7.0f),
        };
        EventManager::GetInstance()->Dispatch(event);
        damageApplied_ = true;
    }
}

void EnemyPrismSlime::PrepareThunderPoints() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kThunderChainAttackId);
    const Vector3 origin = GetWorldPosition();
    const float spacing = (std::max)(2.0f, attack.maxRange / 6.4f);
    for (std::size_t index = 0; index < thunderStrikePositions_.size(); ++index) {
        const float distance = 3.2f + spacing * static_cast<float>(index);
        thunderStrikePositions_[index] = FindGroundPoint(origin + lockedDirection_ * distance);
        if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
            effects->SpawnEffectAt(kThunderWarningEffect,
                thunderStrikePositions_[index] + Vector3{ 0.0f, 0.045f, 0.0f },
                { 0.0f, std::atan2(lockedDirection_.x, lockedDirection_.z), 0.0f },
                { attack.radius, 1.0f, attack.radius });
        }
    }
}

void EnemyPrismSlime::PrepareSummonPortals() {
    summonPortalVisuals_.clear();
    summonPortalVisuals_.reserve(kSummonPortalCount);

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const Vector3 center = GetWorldPosition();
    const float placementRadius = (std::max)(3.6f, attack.radius);
    std::uint32_t randomState = 0x9e3779b9u ^ ((summonRandomSerial_ + 1u) * 0x85ebca6bu);
    const float startAngle = NextSummonRandom01(randomState) * kPi * 2.0f;

    for (int index = 0; index < kSummonPortalCount; ++index) {
        const float spreadAngle = (kPi * 2.0f * static_cast<float>(index)) /
            static_cast<float>(kSummonPortalCount);
        const float angleJitter = (NextSummonRandom01(randomState) - 0.5f) * 0.58f;
        const float distanceRate = 0.62f + NextSummonRandom01(randomState) * 0.32f;
        const float angle = startAngle + spreadAngle + angleJitter;

        Vector3 samplePosition = center + Vector3{
            std::sin(angle) * placementRadius * distanceRate,
            0.0f,
            std::cos(angle) * placementRadius * distanceRate,
        };

        SummonPortalVisual portal;
        portal.groundPosition = FindGroundPoint(samplePosition);
        portal.effect = CreateSummonPortalEffect();

        const int kindIndex = (std::min)(3, static_cast<int>(NextSummonRandom01(randomState) * 4.0f));
        portal.slimeKind = static_cast<SummonSlimeKind>(kindIndex);
        switch (portal.slimeKind) {
        case SummonSlimeKind::Fire:
            portal.color = { 1.0f, 0.20f, 0.035f, 0.90f };
            break;
        case SummonSlimeKind::Bomber:
            // 完全な黒で紋様が消えない範囲に抑えた、黒紫の召喚色です。
            portal.color = { 0.11f, 0.075f, 0.15f, 0.96f };
            break;
        case SummonSlimeKind::Pink:
            portal.color = { 1.0f, 0.24f, 0.68f, 0.90f };
            break;
        case SummonSlimeKind::Thunder:
            portal.color = { 1.0f, 0.86f, 0.055f, 0.94f };
            break;
        }

        if (portal.effect) {
            portal.effect->SetTranslate(portal.groundPosition + Vector3{ 0.0f, 0.065f, 0.0f });
            portal.effect->SetScale({ 0.48f, 1.0f, 0.48f });
            portal.effect->SetColor({ portal.color.x, portal.color.y, portal.color.z, 0.36f });
            portal.effect->UpdateLocalMatrix();
            portal.effect->UpdateWorldMatrix();
        }
        summonPortalVisuals_.push_back(std::move(portal));
    }

    ++summonRandomSerial_;
}

std::unique_ptr<EffectObject3d> EnemyPrismSlime::CreateSummonPortalEffect() const {
    if (!common_) {
        return nullptr;
    }

    auto effect = std::make_unique<EffectObject3d>();
    effect->Initialize(common_);
    effect->SetName("PrismSlimeSummonPortal");
    effect->SetProceduralType(7);
    effect->editPlaneSize_ = { 2.0f, 2.0f };
    effect->editMeshSegments_ = 1;
    effect->UpdateProceduralMesh();
    effect->SetTexture(kPrismSpellTelegraphTexture);
    effect->SetBlendMode(BlendMode::kNormal);
    effect->SetIntensity(1.38f);
    effect->SetScrollSpeed({ 0.0f, 0.0f });
    effect->SetEnableReveal(false);
    effect->SetEnableDistortion(false);
    effect->SetEnableNoiseTexture(false);
    effect->SetEnableColorRamp(false);
    effect->SetEdgeFadeStrength(0.0f);
    effect->SetAlphaReference(0.004f);
    effect->SetStartScale({ 1.0f, 1.0f, 1.0f });
    effect->SetEndScale({ 1.0f, 1.0f, 1.0f });
    effect->SetStartColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    effect->SetEndColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    effect->Play(600.0f);
    return effect;
}

void EnemyPrismSlime::UpdateSummonPortalVisuals(float deltaTime) {
    if (summonPortalVisuals_.empty()) {
        return;
    }

    float revealProgress = 1.0f;
    if (currentAttack_ == AttackKind::SlimeSummon && attackState_ == AttackState::Windup) {
        revealProgress = 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f);
    }
    const float easedReveal = SmoothStep01(revealProgress);

    for (std::size_t index = 0; index < summonPortalVisuals_.size(); ++index) {
        SummonPortalVisual& portal = summonPortalVisuals_[index];
        if (!portal.effect) {
            continue;
        }

        portal.age += deltaTime;
        portal.effect->Update(deltaTime);
        float scale = 1.36f * (0.42f + easedReveal * 0.58f);
        float alpha = portal.color.w * (0.34f + easedReveal * 0.66f);
        if (portal.spawned) {
            portal.spawnedAge += deltaTime;
            const float fade = std::clamp(portal.spawnedAge / kSummonPortalFadeDuration, 0.0f, 1.0f);
            scale *= 1.0f + fade * 0.36f;
            alpha *= (1.0f - fade) * (1.0f - fade);
            if (fade >= 1.0f) {
                portal.effect->SetIsVisible(false);
                continue;
            }
        } else {
            const float pulse = std::sin(portal.age * 7.5f + static_cast<float>(index) * 1.7f);
            scale *= 1.0f + pulse * (0.025f + easedReveal * 0.025f);
            alpha *= 0.92f + pulse * 0.08f;
        }

        portal.effect->SetIsVisible(true);
        portal.effect->SetTranslate(portal.groundPosition + Vector3{ 0.0f, 0.065f, 0.0f });
        portal.effect->SetRotation({ 0.0f, portal.age * (0.38f + static_cast<float>(index) * 0.045f), 0.0f });
        portal.effect->SetScale({ scale, 1.0f, scale });
        portal.effect->SetColor({ portal.color.x, portal.color.y, portal.color.z, alpha });
        portal.effect->UpdateLocalMatrix();
        portal.effect->UpdateWorldMatrix();
    }
}

void EnemyPrismSlime::SpawnNextSummonedSlime() {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(summonPortalVisuals_.size())) {
        return;
    }

    SummonPortalVisual& portal = summonPortalVisuals_[static_cast<std::size_t>(actionIndex_)];
    if (portal.spawned) {
        return;
    }

    const char* enemyType = "Slime";
    const char* nameSuffix = "Pink";
    const char* spawnPreset = kPrismPhasePulsePreset;
    switch (portal.slimeKind) {
    case SummonSlimeKind::Fire:
        enemyType = "FireSlime";
        nameSuffix = "Fire";
        spawnPreset = kFireCastPreset;
        break;
    case SummonSlimeKind::Bomber:
        enemyType = "Bomber";
        nameSuffix = "Bomb";
        spawnPreset = kPrismSpikeWarningPreset;
        break;
    case SummonSlimeKind::Pink:
        break;
    case SummonSlimeKind::Thunder:
        enemyType = "ThunderSlime";
        nameSuffix = "Thunder";
        spawnPreset = kThunderChargePreset;
        break;
    }

    std::unique_ptr<BaseEnemy> enemy = EnemyFactory::GetInstance()->CreateEnemy(enemyType, common_);
    if (!enemy) {
        return;
    }

    enemy->SetName(std::string("PrismSummon_") + nameSuffix + "_" +
        std::to_string(summonRandomSerial_) + "_" + std::to_string(actionIndex_));
    enemy->SetTranslate(portal.groundPosition + Vector3{ 0.0f, 0.12f, 0.0f });
    enemy->SetVelocity({ 0.0f, 0.0f, 0.0f });
    enemy->SetGrounded(true);
    enemy->SetDetectionRange(24.0f);
    enemy->SetTarget(target_);

    portal.spawned = true;
    portal.spawnedAge = 0.0f;
    EmitPreset(spawnPreset, portal.groundPosition + Vector3{ 0.0f, 0.24f, 0.0f });
    SpawnSummonedSlime(std::move(enemy));
}

void EnemyPrismSlime::SpawnSummonedSlime(std::unique_ptr<BaseEnemy> enemy) {
    if (!enemy) {
        return;
    }
    if (spawnCallback_) {
        spawnCallback_(std::move(enemy));
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->GetCurrentScene()) {
        return;
    }
    enemy->SetTarget(sceneManager->GetCurrentScene()->GetPlayer());
    sceneManager->GetCurrentScene()->AddObject(std::move(enemy));
}

Vector3 EnemyPrismSlime::FindGroundPoint(const Vector3& samplePosition) const {
    Vector3 rayStart = samplePosition;
    rayStart.y += 11.0f;
    PhysicsQueryFilter filter;
    filter.mask = kAllGround;
    filter.ignoredObject = const_cast<EnemyPrismSlime*>(this);
    const RaycastHit hit = CollisionManager::GetInstance()->Raycast(
        rayStart, { 0.0f, -1.0f, 0.0f }, 24.0f, filter);
    if (hit.isHit) {
        return hit.hitPoint;
    }
    Vector3 fallback = samplePosition;
    fallback.y = GetTranslate().y;
    return fallback;
}

void EnemyPrismSlime::InitializeFaceParts() {
    coreFramePart_ = CreatePart(
        "PrismSlime_CoreFrame",
        "Characters/prism_slime_frame",
        { 0.0f, 0.51f, 0.505f },
        { 0.19f, 0.17f, 0.17f },
        { 0.58f, 0.34f, 0.90f, 1.0f },
        1.55f);
    corePart_ = CreatePart(
        "PrismSlime_Core",
        "Characters/prism_slime_core",
        { 0.0f, 0.51f, 0.518f },
        { 0.17f, 0.16f, 0.12f },
        { 0.94f, 0.42f, 0.88f, 1.0f },
        2.15f);
    eyeParts_[0] = CreatePart(
        "PrismSlime_EyeLeft",
        "Primitives/sphere",
        { -0.19f, 0.555f, 0.442f },
        { 0.055f, 0.078f, 0.040f },
        { 0.07f, 0.20f, 0.39f, 1.0f },
        1.12f);
    eyeParts_[1] = CreatePart(
        "PrismSlime_EyeRight",
        "Primitives/sphere",
        { 0.19f, 0.555f, 0.442f },
        { 0.055f, 0.078f, 0.040f },
        { 0.07f, 0.20f, 0.39f, 1.0f },
        1.12f);

    for (Object3d* prismPart : { coreFramePart_.get(), corePart_.get() }) {
        if (!prismPart) {
            continue;
        }
        prismPart->SetMaterialType(kPrismCrystalMaterialType);
        prismPart->SetMetallic(0.88f);
        prismPart->SetRoughness(0.10f);
        prismPart->SetEnableEnvMap(true);
        prismPart->SetEnvIntensity(1.35f);
    }
}

std::unique_ptr<Object3d> EnemyPrismSlime::CreatePart(
    const std::string& name,
    const std::string& modelName,
    const Vector3& localPosition,
    const Vector3& localScale,
    const Vector4& color,
    float emissive) {
    if (!common_) {
        return nullptr;
    }
    auto part = std::make_unique<Object3d>();
    part->Initialize(common_);
    part->SetName(name);
    part->SetClassName("EnemyVisualPart");
    part->SetModel(modelName);
    part->SetColliderType(ColliderType::kNone);
    part->SetCollisionAttribute(0);
    part->SetCollisionMask(0);
    part->SetColor(color);
    part->SetMetallic(0.08f);
    part->SetRoughness(0.18f);
    part->SetEmissive(emissive);
    part->SetTranslate(localPosition);
    part->SetScale(localScale);
    part->SetParent(this);
    return part;
}

void EnemyPrismSlime::UpdateFaceParts(float deltaTime) {
    const bool visible = GetIsVisible();
    const float pulse = 1.0f + std::sin(idleTimer_ * 4.2f) * 0.055f +
        (impactPulseTimer_ > 0.0f ? std::sin(impactPulseTimer_ * 46.0f) * 0.10f : 0.0f);

    if (corePart_) {
        corePart_->SetIsVisible(visible);
        corePart_->SetScale({ 0.17f * pulse, 0.16f * pulse, 0.12f * pulse });
        corePart_->SetRotation({ idleTimer_ * 0.28f, idleTimer_ * 0.44f, idleTimer_ * 0.62f });
        corePart_->Update(deltaTime);
    }
    if (coreFramePart_) {
        coreFramePart_->SetIsVisible(visible);
        const float framePulse = 1.0f + (pulse - 1.0f) * 0.62f;
        coreFramePart_->SetScale({ 0.19f * framePulse, 0.17f * framePulse, 0.17f });
        coreFramePart_->SetRotation({ 0.0f, 0.0f, std::sin(idleTimer_ * 2.1f) * 0.055f });
        coreFramePart_->Update(deltaTime);
    }

    const float eyeSquash = attackState_ == AttackState::Windup
        ? 1.0f - SmoothStep01(1.0f - attackTimer_ / (std::max)(0.01f, attackStateDuration_)) * 0.22f
        : 1.0f;
    for (auto& eye : eyeParts_) {
        if (!eye) {
            continue;
        }
        eye->SetIsVisible(visible);
        eye->SetScale({ 0.055f, 0.078f * eyeSquash, 0.040f });
        eye->Update(deltaTime);
    }
}

void EnemyPrismSlime::UpdatePhaseAppearance() {
    Vector4 coreColor = { 0.94f, 0.42f, 0.88f, 1.0f };
    Vector4 frameColor = { 0.58f, 0.34f, 0.90f, 1.0f };
    switch (elementPhase_) {
    case ElementPhase::Fire:
        coreColor = { 1.0f, 0.32f, 0.08f, 1.0f };
        frameColor = { 1.0f, 0.68f, 0.18f, 1.0f };
        break;
    case ElementPhase::Thunder:
        coreColor = { 1.0f, 0.94f, 0.16f, 1.0f };
        frameColor = { 0.72f, 0.94f, 1.0f, 1.0f };
        break;
    case ElementPhase::Wind:
        coreColor = { 0.35f, 1.0f, 0.72f, 1.0f };
        frameColor = { 0.66f, 1.0f, 0.88f, 1.0f };
        break;
    default:
        break;
    }
    if (corePart_) corePart_->SetColor(coreColor);
    if (coreFramePart_) coreFramePart_->SetColor(frameColor);
}

void EnemyPrismSlime::ApplySlimeAnimation(float deltaTime) {
    SlimeBounceAnimator::Params params;
    params.speedForFullBounce = 3.2f;
    params.idleAmplitude = 0.035f;
    params.moveAmplitude = 0.10f;
    params.hopFrequency = 4.4f;
    params.horizontalSquash = 0.10f;
    params.verticalStretch = 0.14f;
    params.airborneStretch = 0.18f;

    Vector3 targetScale = SlimeBounceAnimator::MakeScale(
        baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    Vector3 targetRotation = { 0.0f, GetRotation().y, 0.0f };

    if (attackState_ == AttackState::Windup) {
        const float progress = SmoothStep01(1.0f - attackTimer_ / (std::max)(0.01f, attackStateDuration_));
        const float tremble = std::sin(progress * kPi * 12.0f) * 0.018f * progress;
        targetScale.x = baseScale_.x * (1.0f + progress * 0.16f + tremble);
        targetScale.y = baseScale_.y * (1.0f - progress * 0.18f - std::abs(tremble));
        targetScale.z = baseScale_.z * (1.0f + progress * 0.10f - tremble);
        targetRotation.z = tremble * 1.4f;
    } else if (attackState_ == AttackState::Active) {
        const float progress = 1.0f - attackTimer_ / (std::max)(0.01f, attackStateDuration_);
        const float pulse = std::sin(progress * kPi * 5.0f);
        if (currentAttack_ == AttackKind::PrismSpikes) {
            targetScale = { baseScale_.x * (1.28f + pulse * 0.06f), baseScale_.y * 0.74f, baseScale_.z * (1.28f - pulse * 0.04f) };
        } else if (currentAttack_ == AttackKind::CrystalLanceVolley) {
            const float recoil = std::sin(progress * kPi * static_cast<float>(kCrystalLanceCount));
            targetScale = {
                baseScale_.x * (1.12f + std::abs(recoil) * 0.055f),
                baseScale_.y * (0.84f - recoil * 0.025f),
                baseScale_.z * (1.18f + recoil * 0.045f),
            };
            targetRotation.x = -0.075f + recoil * 0.025f;
        } else if (currentAttack_ == AttackKind::FireFan) {
            targetScale = { baseScale_.x * (1.08f + std::abs(pulse) * 0.05f), baseScale_.y * 0.88f, baseScale_.z * (1.22f - pulse * 0.06f) };
            targetRotation.x = -0.08f + pulse * 0.025f;
        } else if (currentAttack_ == AttackKind::ThunderChain) {
            const float jitter = std::sin(progress * kPi * 18.0f) * 0.045f;
            targetScale = { baseScale_.x * (1.14f + jitter), baseScale_.y * (0.82f - std::abs(jitter) * 0.4f), baseScale_.z * (1.14f - jitter) };
            targetRotation.z = jitter * 1.5f;
        } else if (currentAttack_ == AttackKind::WindWave) {
            targetScale = { baseScale_.x * (1.06f + pulse * 0.04f), baseScale_.y * 0.86f, baseScale_.z * (1.30f - pulse * 0.05f) };
            targetRotation.x = -0.10f;
        }
    } else if (attackState_ == AttackState::Recovery || impactPulseTimer_ > 0.0f) {
        const float remaining = attackState_ == AttackState::Recovery
            ? std::clamp(attackTimer_ / (std::max)(0.01f, attackStateDuration_), 0.0f, 1.0f)
            : std::clamp(impactPulseTimer_ / 0.32f, 0.0f, 1.0f);
        const float bounce = std::sin((1.0f - remaining) * kPi * 2.5f) * remaining;
        targetScale = {
            baseScale_.x * (1.0f + bounce * 0.12f),
            baseScale_.y * (1.0f - bounce * 0.18f),
            baseScale_.z * (1.0f + bounce * 0.12f),
        };
    }

    ApplyDamageReactionPose(targetScale, targetRotation);
    const float rate = 1.0f - std::exp(-deltaTime * 11.0f);
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

void EnemyPrismSlime::UpdateFacing(const Vector3& direction) {
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z);
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.10f));
}

void EnemyPrismSlime::EmitPreset(const char* presetName, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyPrismSlime::EmitDirectedPreset(
    const char* presetName,
    const Vector3& position,
    const Vector3& direction,
    float speedScale) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyPrismSlime::SyncCollisionRadius() {
    const Vector3 scale = GetScale();
    const float maximumScale = (std::max)({ 0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    SetCollisionRadius(kGroundCollisionWorldRadius / maximumScale);
}
