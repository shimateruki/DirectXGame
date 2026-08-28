#define NOMINMAX
#include "EnemyFalseKingSlime.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshRenderer.h"
#include "SceneManager.h"
#include "SlimeBounceAnimator.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
constexpr const char* kCrownLanceRainAttackId = "crown_lance_rain";
constexpr const char* kRoyalShockwaveAttackId = "royal_shockwave";
constexpr const char* kKingRushAttackId = "king_rush";
constexpr const char* kRoyalCrossAttackId = "royal_cross";
constexpr const char* kCrownDominionAttackId = "crown_dominion";

constexpr const char* kChargePreset = "false_king_charge";
constexpr const char* kLanceTrailPreset = "false_king_lance_trail";
constexpr const char* kLanceImpactPreset = "false_king_lance_impact";
constexpr const char* kRushWakePreset = "false_king_rush_wake";
constexpr const char* kShockwavePreset = "false_king_shockwave";
constexpr const char* kDominionPreset = "false_king_dominion";
constexpr const char* kAppearanceSequence = "false_king_appear_cue";
constexpr const char* kPhaseShiftSequence = "false_king_phase_shift_cue";
constexpr const char* kDominionSequence = "false_king_dominion_cue";

constexpr const char* kCrownLanceModel = "Effects/false_king_crown_lance";
constexpr const char* kRoyalWaveModel = "Effects/false_king_royal_wave";
constexpr const char* kRoyalBeamModel = "Effects/false_king_royal_beam";
constexpr const char* kRushWingsModel = "Effects/false_king_rush_wings";
constexpr const char* kDominionSigilModel = "Effects/false_king_dominion_sigil";

constexpr int kEncounterControlledActionMode = 1;
constexpr int kLaserMaterialType = 12;
constexpr int kShockwaveMaterialType = 14;
constexpr int kCrownMaterialType = 19;
constexpr float kDefaultAppearanceDuration = 1.45f;
constexpr float kAppearanceRise = 3.6f;
constexpr float kPi = 3.1415926535f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kModelYawOffset = kPi;
constexpr float kLanceInterval = 0.092f;
constexpr float kDominionLanceInterval = 0.58f;

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EaseOutCubic(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    const float inverse = 1.0f - value;
    return 1.0f - inverse * inverse * inverse;
}

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

float PlanarDistance(const Vector3& lhs, const Vector3& rhs) {
    const float x = lhs.x - rhs.x;
    const float z = lhs.z - rhs.z;
    return std::sqrt(x * x + z * z);
}

Vector3 LerpVector3(const Vector3& start, const Vector3& end, float rate) {
    return {
        Math::Lerp(start.x, end.x, rate),
        Math::Lerp(start.y, end.y, rate),
        Math::Lerp(start.z, end.z, rate),
    };
}
}

EnemyFalseKingSlime::~EnemyFalseKingSlime() = default;

void EnemyFalseKingSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_FalseKingSlime");
    SetEnemyType("FalseKingSlime");
    ReloadAttackProfile();

    SetMaterialType(25);
    // 金属値はOBJの各マテリアルへ持たせ、身体と王冠を同じ反射にしません。
    SetMetallic(0.0f);
    SetRoughness(0.24f);
    SetEnableEnvMap(true);
    SetEnvIntensity(0.68f);
    SetEmissive(1.0f);
    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kOBB);
    defaultColor_ = GetColor();
    SyncCollisionBounds();
}

void EnemyFalseKingSlime::Update(float deltaTime) {
    deltaTime = (std::max)(0.0f, deltaTime);
    if (UpdateEncounterState(deltaTime)) {
        // 登場演出中はCharacterの重力を止めつつ、変更したTransformだけ描画へ反映します。
        Object3d::Update(deltaTime);
        return;
    }

    UpdateCrownLances(deltaTime);
    UpdateRoyalWaves(deltaTime);
    UpdateRoyalBeams(deltaTime);

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
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);

    Vector3 targetDirection = lockedDirection_;
    float targetDistance = 9999.0f;
    if (target_) {
        targetDirection = NormalizePlanar(target_->GetWorldPosition() - GetWorldPosition());
        targetDistance = PlanarDistance(target_->GetWorldPosition(), GetWorldPosition());
    }

    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    UpdateBehavior(deltaTime, targetDirection, targetDistance, velocity);
    SetVelocity(velocity);
    ApplyBossAnimation(deltaTime);
    UpdateRoyalGlow(deltaTime);
    SyncCollisionBounds();
    BaseEnemy::Update(deltaTime);
}

void EnemyFalseKingSlime::Draw(
    ID3D12Resource* pointLightResource,
    ID3D12Resource* spotLightResource) {
    BaseEnemy::Draw(pointLightResource, spotLightResource);

    // 特殊マテリアルが使えないEditor描画でも攻撃形状を失わないよう、
    // 加算合成の発光コアを通常パスにも描画します。
    for (const CrownLance& lance : crownLances_) {
        if (lance.visual && lance.visual->GetIsVisible()) {
            lance.visual->RefreshRenderCameraData();
            lance.visual->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const RoyalWave& wave : royalWaves_) {
        if (wave.visual && wave.visual->GetIsVisible()) {
            wave.visual->RefreshRenderCameraData();
            wave.visual->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.visual && beam.visual->GetIsVisible()) {
            beam.visual->RefreshRenderCameraData();
            beam.visual->Draw(pointLightResource, spotLightResource);
        }
    }
    if (rushWingsVisual_ && rushWingsVisual_->GetIsVisible()) {
        rushWingsVisual_->Draw(pointLightResource, spotLightResource);
    }
    if (dominionSigilVisual_ && dominionSigilVisual_->GetIsVisible()) {
        dominionSigilVisual_->Draw(pointLightResource, spotLightResource);
    }
}

void EnemyFalseKingSlime::DrawForCamera(
    Camera* camera,
    ID3D12Resource* pointLightResource,
    ID3D12Resource* spotLightResource,
    int previewBufferIndex) {
    Object3d::DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);

    // 演出カメラPreviewはSceneの特殊マテリアルパスを共有しないため、
    // 攻撃の発光コアをPreview用WVPで直接描きます。
    for (const CrownLance& lance : crownLances_) {
        if (lance.visual && lance.visual->GetIsVisible()) {
            lance.visual->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const RoyalWave& wave : royalWaves_) {
        if (wave.visual && wave.visual->GetIsVisible()) {
            wave.visual->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.visual && beam.visual->GetIsVisible()) {
            beam.visual->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    if (rushWingsVisual_ && rushWingsVisual_->GetIsVisible()) {
        rushWingsVisual_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
    if (dominionSigilVisual_ && dominionSigilVisual_->GetIsVisible()) {
        dominionSigilVisual_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
}

void EnemyFalseKingSlime::OnSwitchEvent(bool active) {
    if (!IsEncounterControlled()) {
        BaseEnemy::OnSwitchEvent(active);
        return;
    }

    activationRequested_ = active;
    if (!initializedForPlay_) {
        InitializeEncounterState();
    }
    if (active && encounterState_ == EncounterState::Dormant) {
        BeginAppearance();
    } else if (!active && encounterState_ != EncounterState::Dormant) {
        ApplyDormantState();
    }
}

std::unique_ptr<Object3d> EnemyFalseKingSlime::Clone() const {
    auto clone = std::make_unique<EnemyFalseKingSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    clone->baseScale_ = baseScale_;
    clone->hasBaseScale_ = true;
    return clone;
}

void EnemyFalseKingSlime::ApplyManagedScale(const Vector3& scale) {
    authoredScale_ = scale;
    baseScale_ = scale;
    hasBaseScale_ = true;
    authoredStateCaptured_ = true;
    SetScale(scale);
    SyncCollisionBounds();
}

bool EnemyFalseKingSlime::HasOwnedSpecialMaterialVisuals() const {
    if (!GetIsVisible()) {
        return false;
    }
    for (const CrownLance& lance : crownLances_) {
        if (lance.aura && lance.aura->GetIsVisible()) return true;
    }
    for (const RoyalWave& wave : royalWaves_) {
        if (wave.glow && wave.glow->GetIsVisible()) return true;
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.glow && beam.glow->GetIsVisible()) return true;
    }
    return false;
}

void EnemyFalseKingSlime::DrawOwnedSpecialMaterialVisuals(
    uint32_t depthSrvHandle,
    uint32_t grabSrvHandle) {
    for (const CrownLance& lance : crownLances_) {
        if (lance.aura && lance.aura->GetIsVisible()) {
            lance.aura->RefreshRenderCameraData();
            lance.aura->DrawCrownUnlock(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const RoyalWave& wave : royalWaves_) {
        if (wave.glow && wave.glow->GetIsVisible()) {
            wave.glow->RefreshRenderCameraData();
            wave.glow->DrawShockwave(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.glow && beam.glow->GetIsVisible()) {
            beam.glow->RefreshRenderCameraData();
            beam.glow->DrawLaser(depthSrvHandle, grabSrvHandle);
        }
    }
}

void EnemyFalseKingSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    debugPreviewAttackId_ = attackId;
    if (attackId == kCrownDominionAttackId) {
        battlePhase_ = 3;
    } else if (attackId == kRoyalCrossAttackId) {
        battlePhase_ = (std::max)(battlePhase_, 2);
    }
    currentAttack_ = AttackKind::None;
    attackState_ = AttackState::Idle;
    attackCooldown_ = 0.05f;
    attackTimer_ = 0.0f;
    HideAttackTelegraph();
    ClearTransientVisuals();
}

const char* EnemyFalseKingSlime::GetDebugAttackPhaseName() const {
    switch (attackState_) {
    case AttackState::Windup: return "予兆";
    case AttackState::Active: return "攻撃";
    case AttackState::Recovery: return "硬直";
    case AttackState::PhaseShift: return "形態移行";
    default: return "待機";
    }
}

const char* EnemyFalseKingSlime::GetDebugAttackBodyName() const {
    AttackKind kind = currentAttack_;
    if (kind == AttackKind::None) {
        kind = ResolveDebugAttack();
    }
    switch (kind) {
    case AttackKind::CrownLanceRain: return "Crown Lance Mesh";
    case AttackKind::RoyalShockwave: return "Spiked Royal Wave Mesh";
    case AttackKind::KingRush: return "Rush Wings + Boss Body";
    case AttackKind::RoyalCross: return "Sweeping Royal Blade Mesh";
    case AttackKind::CrownDominion: return "Dominion Sigil + Blades + Lances";
    default: return "None";
    }
}

size_t EnemyFalseKingSlime::GetDebugVisibleAttackVisualCount() const {
    size_t visibleCount = 0;
    for (const CrownLance& lance : crownLances_) {
        if (lance.visual && lance.visual->GetIsVisible()) {
            ++visibleCount;
        }
    }
    for (const RoyalWave& wave : royalWaves_) {
        if (wave.visual && wave.visual->GetIsVisible()) {
            ++visibleCount;
        }
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.visual && beam.visual->GetIsVisible()) {
            ++visibleCount;
        }
    }
    if (rushWingsVisual_ && rushWingsVisual_->GetIsVisible()) {
        ++visibleCount;
    }
    if (dominionSigilVisual_ && dominionSigilVisual_->GetIsVisible()) {
        ++visibleCount;
    }
    return visibleCount;
}

void EnemyFalseKingSlime::EnsureBaseScale() {
    if (hasBaseScale_) {
        return;
    }
    baseScale_ = GetScale();
    authoredScale_ = baseScale_;
    hasBaseScale_ = true;
}

void EnemyFalseKingSlime::UpdateBehavior(
    float deltaTime,
    const Vector3& targetDirection,
    float targetDistance,
    Vector3& velocity) {
    if (attackState_ == AttackState::PhaseShift) {
        UpdatePhaseShift(deltaTime, velocity);
        return;
    }
    if (attackState_ != AttackState::Idle) {
        UpdateAttack(deltaTime, targetDirection, targetDistance, velocity);
        return;
    }

    const int desiredPhase = ResolveDesiredBattlePhase();
    if (desiredPhase > battlePhase_ && debugPreviewAttackId_.empty()) {
        BeginPhaseShift(desiredPhase);
        return;
    }

    if (!target_ || !param_.has_value()) {
        velocity = CalculateWanderVelocity(deltaTime, 0.34f, 0.28f);
        UpdateFacing(velocity, 0.06f);
        return;
    }

    UpdateFacing(targetDirection);
    // 攻撃プレビューでは発見リアクションを挟まず、選択した攻撃の予兆へ直行します。
    if (debugPreviewAttackId_.empty() &&
        UpdateNoticeReaction(deltaTime, targetDistance, detectionRange_, targetDirection)) {
        return;
    }

    const AttackKind debugAttack = ResolveDebugAttack();
    const AttackKind plannedAttack = debugAttack != AttackKind::None
        ? debugAttack
        : ResolveAutomaticAttack(targetDistance);
    const EnemyAttackDefinition& planned = GetAttackDefinition(GetAttackId(plannedAttack));
    const bool inRange = targetDistance >= planned.minRange && targetDistance <= planned.maxRange;
    if (targetDistance <= detectionRange_ && attackCooldown_ <= 0.0f && inRange) {
        StartAttack(plannedAttack, targetDirection);
        return;
    }

    // 攻撃範囲外の時だけゆっくり間合いを詰め、広い闘技場を使わせます。
    const float moveSpeed = (std::max)(0.85f, param_->speed * (battlePhase_ >= 3 ? 1.15f : 0.90f));
    const float preferredDistance = (std::clamp)(planned.recommendedTargetDistance, 7.0f, 15.0f);
    if (targetDistance > preferredDistance + 2.0f) {
        velocity.x = targetDirection.x * moveSpeed;
        velocity.z = targetDirection.z * moveSpeed;
    } else if (targetDistance < preferredDistance - 3.0f) {
        velocity.x = -targetDirection.x * moveSpeed * 0.48f;
        velocity.z = -targetDirection.z * moveSpeed * 0.48f;
    }
}

void EnemyFalseKingSlime::StartAttack(AttackKind kind, const Vector3& targetDirection) {
    if (kind == AttackKind::None) {
        return;
    }

    currentAttack_ = kind;
    attackState_ = AttackState::Windup;
    lockedDirection_ = NormalizePlanar(targetDirection);
    lockedTargetPosition_ = target_
        ? FindGroundPoint(target_->GetWorldPosition())
        : FindGroundPoint(GetTranslate() + lockedDirection_ * 9.0f);
    attackStartPosition_ = FindGroundPoint(GetTranslate());
    attackEndPosition_ = attackStartPosition_;
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackStateDuration_ = (std::max)(0.18f, attack.windupDuration);
    attackTimer_ = attackStateDuration_;
    actionTimer_ = 0.0f;
    effectTimer_ = 0.0f;
    actionIndex_ = 0;
    warningTriggered_ = false;
    rushDamageApplied_ = false;

    if (kind == AttackKind::CrownLanceRain) {
        PrepareLanceTargets(false);
    } else if (kind == AttackKind::CrownDominion) {
        PrepareLanceTargets(true);
    }
    if (debugPreviewAttackId_.empty()) {
        ++automaticAttackSerial_;
    }
}

void EnemyFalseKingSlime::UpdateAttack(
    float deltaTime,
    const Vector3& targetDirection,
    float,
    Vector3& velocity) {
    if (attackState_ == AttackState::Windup) {
        UpdateWindup(deltaTime, targetDirection);
        return;
    }
    if (attackState_ == AttackState::Active) {
        UpdateActive(deltaTime, targetDirection, velocity);
        return;
    }
    if (attackState_ == AttackState::Recovery) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
        HideAttackTelegraph();
        if (attackTimer_ <= 0.0f) {
            FinishAttack();
        }
    }
}

void EnemyFalseKingSlime::UpdateWindup(float deltaTime, const Vector3& targetDirection) {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    effectTimer_ -= deltaTime;
    const float progress = 1.0f - std::clamp(
        attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
    const Vector4 warningColor = battlePhase_ >= 3
        ? Vector4{ 0.90f, 0.18f, 1.0f, 0.86f }
        : Vector4{ 1.0f, 0.72f, 0.12f, 0.86f };

    if (attackTimer_ > attack.warningLeadTime && target_) {
        lockedDirection_ = NormalizePlanar(targetDirection);
        lockedTargetPosition_ = FindGroundPoint(target_->GetWorldPosition());
        if (currentAttack_ == AttackKind::CrownLanceRain) {
            PrepareLanceTargets(false);
        }
    }
    UpdateFacing(lockedDirection_, 0.18f);

    switch (currentAttack_) {
    case AttackKind::CrownLanceRain: {
        const size_t count = static_cast<size_t>(battlePhase_ == 1 ? 10 : (battlePhase_ == 2 ? 14 : 18));
        ShowAttackTelegraphImpactAreas(lanceTargets_.data(), count, attack.radius, progress, warningColor);
        break;
    }
    case AttackKind::RoyalShockwave:
        ShowAttackTelegraphCircle(FindGroundPoint(GetTranslate()), attack.maxRange, progress, warningColor);
        break;
    case AttackKind::KingRush:
        ShowAttackTelegraphLine(
            GetTranslate(), lockedDirection_, attack.maxRange, attack.radius * 2.0f, progress, warningColor);
        break;
    case AttackKind::RoyalCross:
        ShowAttackTelegraphLaneFan(
            GetTranslate(), lockedDirection_, attack.maxRange, attack.radius * 2.0f,
            battlePhase_ >= 3 ? 6 : 4, attack.radius * 3.0f, kPi * 0.5f, progress, warningColor);
        break;
    case AttackKind::CrownDominion:
        ShowAttackTelegraphCircle(FindGroundPoint(GetTranslate()), attack.maxRange, progress, warningColor);
        break;
    default:
        break;
    }

    if (effectTimer_ <= 0.0f) {
        EmitDirectedPreset(
            currentAttack_ == AttackKind::CrownDominion ? kDominionPreset : kChargePreset,
            GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 1.5f, 0.0f },
            { -lockedDirection_.x * 0.12f, 1.0f, -lockedDirection_.z * 0.12f },
            0.92f + progress * 0.78f);
        effectTimer_ += (std::max)(0.12f, 0.24f - progress * 0.08f);
    }
    if (!warningTriggered_ && attackTimer_ <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue(warningColor);
        warningTriggered_ = true;
    }
    if (attackTimer_ <= 0.0f) {
        BeginActive();
    }
}

void EnemyFalseKingSlime::BeginActive() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackState_ = AttackState::Active;
    attackStateDuration_ = (std::max)(0.20f, attack.activeDuration);
    attackTimer_ = attackStateDuration_;
    actionTimer_ = 0.0f;
    effectTimer_ = 0.0f;
    actionIndex_ = 0;
    HideAttackTelegraph();

    switch (currentAttack_) {
    case AttackKind::RoyalShockwave:
        SpawnRoyalWaves();
        break;
    case AttackKind::KingRush:
        BeginRush();
        break;
    case AttackKind::RoyalCross:
        SpawnRoyalBeams(false);
        break;
    case AttackKind::CrownDominion:
        SpawnRoyalBeams(true);
        dominionSigilVisual_ = CreateRoyalMesh(
            "FalseKing_DominionSigil",
            kDominionSigilModel,
            { 1.0f, 1.0f, 1.0f, 1.0f },
            2.15f);
        if (dominionSigilVisual_) {
            dominionSigilVisual_->SetIsVisible(false);
            dominionSigilVisual_->UpdateWorldMatrix(true);
        }
        dominionLanceTimer_ = 0.32f;
        VFXSequencer::PlayOneShot(kDominionSequence, FindGroundPoint(GetTranslate()), { 2.4f, 2.4f, 2.4f });
        break;
    default:
        break;
    }
}

void EnemyFalseKingSlime::UpdateActive(
    float deltaTime,
    const Vector3& targetDirection,
    Vector3& velocity) {
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    actionTimer_ -= deltaTime;
    effectTimer_ -= deltaTime;

    switch (currentAttack_) {
    case AttackKind::CrownLanceRain: {
        const int count = battlePhase_ == 1 ? 10 : (battlePhase_ == 2 ? 14 : 18);
        while (actionIndex_ < count && actionTimer_ <= 0.0f) {
            SpawnNextCrownLance(false);
            ++actionIndex_;
            actionTimer_ += kLanceInterval;
        }
        break;
    }
    case AttackKind::RoyalShockwave:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        break;
    case AttackKind::KingRush:
        UpdateRush(deltaTime, targetDirection, velocity);
        break;
    case AttackKind::RoyalCross:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        break;
    case AttackKind::CrownDominion:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateDominion(deltaTime);
        break;
    default:
        break;
    }

    if (attackTimer_ <= 0.0f) {
        BeginRecovery();
    }
}

void EnemyFalseKingSlime::BeginRecovery() {
    attackState_ = AttackState::Recovery;
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackStateDuration_ = (std::max)(0.18f, attack.recoveryDuration);
    attackTimer_ = attackStateDuration_;
    HideAttackTelegraph();
    SetVelocity({ 0.0f, GetVelocity().y, 0.0f });
    rushWingsVisual_.reset();
    dominionSigilVisual_.reset();
}

void EnemyFalseKingSlime::FinishAttack() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackCooldown_ = (std::max)(0.35f, attack.cooldown * (battlePhase_ == 1 ? 1.0f : (battlePhase_ == 2 ? 0.88f : 0.78f)));
    currentAttack_ = AttackKind::None;
    attackState_ = AttackState::Idle;
    attackTimer_ = 0.0f;
    actionIndex_ = 0;
    SetTranslate(FindGroundPoint(GetTranslate()));
    SetGrounded(true);
}

EnemyFalseKingSlime::AttackKind EnemyFalseKingSlime::ResolveAutomaticAttack(float targetDistance) const {
    if (battlePhase_ <= 1) {
        if (targetDistance > 16.0f && automaticAttackSerial_ % 3 == 2) {
            return AttackKind::KingRush;
        }
        constexpr std::array<AttackKind, 3> pattern = {
            AttackKind::CrownLanceRain,
            AttackKind::RoyalShockwave,
            AttackKind::KingRush,
        };
        return pattern[static_cast<size_t>(automaticAttackSerial_ % static_cast<int>(pattern.size()))];
    }
    if (battlePhase_ == 2) {
        constexpr std::array<AttackKind, 4> pattern = {
            AttackKind::RoyalCross,
            AttackKind::CrownLanceRain,
            AttackKind::KingRush,
            AttackKind::RoyalShockwave,
        };
        return pattern[static_cast<size_t>(automaticAttackSerial_ % static_cast<int>(pattern.size()))];
    }
    constexpr std::array<AttackKind, 5> pattern = {
        AttackKind::CrownDominion,
        AttackKind::KingRush,
        AttackKind::CrownLanceRain,
        AttackKind::RoyalCross,
        AttackKind::RoyalShockwave,
    };
    return pattern[static_cast<size_t>(automaticAttackSerial_ % static_cast<int>(pattern.size()))];
}

EnemyFalseKingSlime::AttackKind EnemyFalseKingSlime::ResolveDebugAttack() const {
    if (debugPreviewAttackId_ == kCrownLanceRainAttackId) return AttackKind::CrownLanceRain;
    if (debugPreviewAttackId_ == kRoyalShockwaveAttackId) return AttackKind::RoyalShockwave;
    if (debugPreviewAttackId_ == kKingRushAttackId) return AttackKind::KingRush;
    if (debugPreviewAttackId_ == kRoyalCrossAttackId) return AttackKind::RoyalCross;
    if (debugPreviewAttackId_ == kCrownDominionAttackId) return AttackKind::CrownDominion;
    return AttackKind::None;
}

const char* EnemyFalseKingSlime::GetAttackId(AttackKind kind) const {
    switch (kind) {
    case AttackKind::CrownLanceRain: return kCrownLanceRainAttackId;
    case AttackKind::RoyalShockwave: return kRoyalShockwaveAttackId;
    case AttackKind::KingRush: return kKingRushAttackId;
    case AttackKind::RoyalCross: return kRoyalCrossAttackId;
    case AttackKind::CrownDominion: return kCrownDominionAttackId;
    default: return kCrownLanceRainAttackId;
    }
}

const EnemyAttackDefinition& EnemyFalseKingSlime::GetCurrentAttackDefinition() const {
    return GetAttackDefinition(GetAttackId(currentAttack_));
}

int EnemyFalseKingSlime::ResolveDesiredBattlePhase() const {
    const float maximumHp = GetEncounterMaximumHp();
    const float hpRate = maximumHp > 0.0f ? GetEncounterCurrentHp() / maximumHp : 1.0f;
    if (hpRate <= 0.34f) return 3;
    if (hpRate <= 0.67f) return 2;
    return 1;
}

void EnemyFalseKingSlime::BeginPhaseShift(int nextPhase) {
    pendingBattlePhase_ = (std::clamp)(nextPhase, 1, 3);
    attackState_ = AttackState::PhaseShift;
    currentAttack_ = AttackKind::None;
    phaseTransitionTimer_ = 0.0f;
    attackTimer_ = 0.0f;
    ClearTransientVisuals();
    HideAttackTelegraph();
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    VFXSequencer::PlayOneShot(
        kPhaseShiftSequence,
        FindGroundPoint(GetTranslate()) + Vector3{ 0.0f, 0.15f, 0.0f },
        { 2.1f + static_cast<float>(pendingBattlePhase_) * 0.25f,
          2.1f + static_cast<float>(pendingBattlePhase_) * 0.25f,
          2.1f + static_cast<float>(pendingBattlePhase_) * 0.25f });
}

void EnemyFalseKingSlime::UpdatePhaseShift(float deltaTime, Vector3& velocity) {
    phaseTransitionTimer_ += deltaTime;
    velocity = { 0.0f, 0.0f, 0.0f };
    const float progress = GetPhaseTransitionProgress();
    effectTimer_ -= deltaTime;
    if (effectTimer_ <= 0.0f) {
        const float angle = idleTimer_ * (4.0f + static_cast<float>(pendingBattlePhase_));
        const Vector3 direction = { std::sin(angle) * 0.45f, 1.0f, std::cos(angle) * 0.45f };
        EmitDirectedPreset(kDominionPreset, GetWorldPosition() + Vector3{ 0.0f, 1.4f, 0.0f }, direction, 1.0f + progress * 0.9f);
        effectTimer_ += 0.14f;
    }
    if (phaseTransitionTimer_ >= phaseTransitionDuration_) {
        battlePhase_ = pendingBattlePhase_;
        attackState_ = AttackState::Idle;
        phaseTransitionTimer_ = phaseTransitionDuration_;
        attackCooldown_ = 0.72f;
        SetTranslate(FindGroundPoint(GetTranslate()));
        SetGrounded(true);
    }
}

void EnemyFalseKingSlime::PrepareLanceTargets(bool dominionPattern) {
    const Vector3 center = dominionPattern ? FindGroundPoint(GetTranslate()) : lockedTargetPosition_;
    const float phaseOffset = std::fmod(
        idleTimer_ * 1.37f + static_cast<float>(automaticAttackSerial_) * 0.83f,
        kTwoPi);
    for (size_t index = 0; index < lanceTargets_.size(); ++index) {
        if (!dominionPattern && index == 0) {
            lanceTargets_[index] = lockedTargetPosition_;
            continue;
        }
        const int ring = static_cast<int>(index) / 6;
        const int spoke = static_cast<int>(index) % 6;
        const float radius = dominionPattern
            ? 7.0f + static_cast<float>(ring) * 7.3f
            : 2.7f + static_cast<float>(ring) * 3.4f + static_cast<float>(spoke % 2) * 0.65f;
        const float angle = phaseOffset + static_cast<float>(spoke) * kPi / 3.0f + static_cast<float>(ring) * kPi / 7.0f;
        lanceTargets_[index] = FindGroundPoint(center + Vector3{ std::sin(angle) * radius, 0.0f, std::cos(angle) * radius });
    }
}

void EnemyFalseKingSlime::SpawnNextCrownLance(bool dominionPattern) {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(lanceTargets_.size())) {
        return;
    }
    SpawnCrownLance(lanceTargets_[static_cast<size_t>(actionIndex_)], dominionPattern ? 0.10f : 0.0f);
}

void EnemyFalseKingSlime::SpawnCrownLance(const Vector3& groundPosition, float delay) {
    CrownLance lance;
    lance.visual = CreateRoyalMesh(
        "FalseKing_CrownLance_" + std::to_string(crownLances_.size()),
        kCrownLanceModel,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        battlePhase_ >= 3 ? 1.95f : 1.65f);
    lance.aura = CreateRoyalVisual(
        "FalseKing_CrownLanceAura_" + std::to_string(crownLances_.size()),
        "Primitives/sphere",
        kCrownMaterialType,
        { 1.0f, 0.34f, 0.92f, 0.72f },
        2.6f);
    if (!lance.visual) {
        return;
    }
    lance.groundPosition = FindGroundPoint(groundPosition);
    lance.delay = delay;
    lance.flightDuration = battlePhase_ >= 3 ? 0.58f : 0.72f;
    lance.visual->SetTranslate(lance.groundPosition + Vector3{ 0.0f, 16.0f, 0.0f });
    lance.visual->SetScale({ 1.05f, 2.0f, 1.05f });
    lance.visual->SetIsVisible(delay <= 0.0f);
    lance.visual->UpdateWorldMatrix(true);
    if (lance.aura) {
        lance.aura->SetTranslate(lance.groundPosition + Vector3{ 0.0f, 0.10f, 0.0f });
        lance.aura->SetScale({ 1.55f, 0.12f, 1.55f });
        lance.aura->SetIsVisible(delay <= 0.0f);
        lance.aura->UpdateWorldMatrix(true);
    }
    crownLances_.push_back(std::move(lance));
}

void EnemyFalseKingSlime::UpdateCrownLances(float deltaTime) {
    auto lance = crownLances_.begin();
    while (lance != crownLances_.end()) {
        if (!lance->visual) {
            lance = crownLances_.erase(lance);
            continue;
        }
        lance->age += deltaTime;
        const float localAge = lance->age - lance->delay;
        if (localAge < 0.0f) {
            ++lance;
            continue;
        }

        const float progress = std::clamp(localAge / (std::max)(lance->flightDuration, 0.01f), 0.0f, 1.0f);
        const float height = Math::Lerp(16.0f, 4.15f, SmoothStep01(progress));
        const float glow = 0.82f + std::sin(localAge * 22.0f) * 0.18f;
        lance->visual->SetIsVisible(progress < 1.0f);
        lance->visual->SetTranslate(lance->groundPosition + Vector3{ 0.0f, height, 0.0f });
        lance->visual->SetRotationY(localAge * 3.8f);
        lance->visual->SetScale({ 1.02f * glow, 2.0f + (1.0f - progress) * 0.24f, 1.02f * glow });
        lance->visual->Update(deltaTime);
        lance->visual->UpdateWorldMatrix(true);

        if (lance->aura) {
            const float auraScale = 1.1f + progress * 1.4f;
            lance->aura->SetIsVisible(progress < 1.0f);
            lance->aura->SetScale({ auraScale, 0.10f, auraScale });
            lance->aura->SetColor({ 1.0f, 0.28f + progress * 0.38f, 0.92f, (1.0f - progress * 0.35f) * 0.70f });
            lance->aura->Update(deltaTime);
            lance->aura->UpdateWorldMatrix(true);
        }

        lance->particleTimer -= deltaTime;
        if (lance->particleTimer <= 0.0f && progress < 0.96f) {
            EmitDirectedPreset(
                kLanceTrailPreset,
                lance->groundPosition + Vector3{ 0.0f, height, 0.0f },
                { 0.0f, -1.0f, 0.0f },
                1.0f + progress * 0.55f);
            lance->particleTimer += 0.12f;
        }
        if (progress >= 1.0f && !lance->impactPlayed) {
            lance->impactPlayed = true;
            EmitDirectedPreset(kLanceImpactPreset, lance->groundPosition, { 0.0f, 1.0f, 0.0f }, 1.35f);
        }
        if (progress >= 1.0f && !lance->damageApplied) {
            const EnemyAttackDefinition& attack = currentAttack_ == AttackKind::CrownDominion
                ? GetAttackDefinition(kCrownDominionAttackId)
                : GetAttackDefinition(kCrownLanceRainAttackId);
            Vector3 knockbackDirection = target_
                ? NormalizePlanar(target_->GetWorldPosition() - lance->groundPosition)
                : lockedDirection_;
            DamageTargetAt(
                lance->groundPosition,
                attack.radius + (target_ ? target_->GetCollisionRadius() : 0.0f),
                3.8f,
                attack.damage,
                DamageType::Explosion,
                { knockbackDirection.x * 7.0f, 7.5f, knockbackDirection.z * 7.0f });
            lance->damageApplied = true;
        }
        if (localAge >= lance->flightDuration + 0.34f) {
            lance = crownLances_.erase(lance);
            continue;
        }
        ++lance;
    }
}

void EnemyFalseKingSlime::SpawnRoyalWaves() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kRoyalShockwaveAttackId);
    const int count = battlePhase_ == 1 ? 4 : (battlePhase_ == 2 ? 5 : 6);
    for (int index = 0; index < count; ++index) {
        RoyalWave wave;
        wave.visual = CreateRoyalMesh(
            "FalseKing_RoyalWave_" + std::to_string(index),
            kRoyalWaveModel,
            { 1.0f, 1.0f, 1.0f, 1.0f },
            battlePhase_ >= 3 ? 1.85f : 1.55f);
        wave.glow = CreateRoyalVisual(
            "FalseKing_RoyalWaveGlow_" + std::to_string(index),
            "Primitives/cylinder",
            kShockwaveMaterialType,
            battlePhase_ >= 3 ? Vector4{ 0.84f, 0.20f, 1.0f, 0.42f } : Vector4{ 1.0f, 0.66f, 0.12f, 0.42f },
            1.8f);
        if (!wave.visual) {
            continue;
        }
        wave.center = FindGroundPoint(GetTranslate()) + Vector3{ 0.0f, 0.20f, 0.0f };
        wave.delay = static_cast<float>(index) * (battlePhase_ >= 3 ? 0.27f : 0.34f);
        wave.lifetime = 1.02f + static_cast<float>(index) * 0.035f;
        wave.startRadius = 1.15f + static_cast<float>(index) * 0.16f;
        wave.endRadius = attack.maxRange * (0.82f + static_cast<float>(index) * 0.035f);
        wave.previousRadius = wave.startRadius;
        wave.visual->SetTranslate(wave.center);
        wave.visual->SetScale({ wave.startRadius, 1.0f, wave.startRadius });
        wave.visual->SetIsVisible(index == 0);
        wave.visual->UpdateWorldMatrix(true);
        if (wave.glow) {
            wave.glow->SetTranslate(wave.center - Vector3{ 0.0f, 0.12f, 0.0f });
            wave.glow->SetScale({ wave.startRadius, 0.06f, wave.startRadius });
            wave.glow->SetIsVisible(index == 0);
            wave.glow->UpdateWorldMatrix(true);
        }
        royalWaves_.push_back(std::move(wave));
    }
    EmitDirectedPreset(kShockwavePreset, FindGroundPoint(GetTranslate()), { 0.0f, 1.0f, 0.0f }, 1.55f);
}

void EnemyFalseKingSlime::UpdateRoyalWaves(float deltaTime) {
    auto wave = royalWaves_.begin();
    while (wave != royalWaves_.end()) {
        if (!wave->visual) {
            wave = royalWaves_.erase(wave);
            continue;
        }
        wave->age += deltaTime;
        const float localAge = wave->age - wave->delay;
        if (localAge < 0.0f) {
            ++wave;
            continue;
        }
        const float progress = std::clamp(localAge / (std::max)(wave->lifetime, 0.01f), 0.0f, 1.0f);
        const float radius = Math::Lerp(wave->startRadius, wave->endRadius, EaseOutCubic(progress));
        wave->visual->SetIsVisible(progress < 1.0f);
        wave->visual->SetScale({ radius, 1.0f + (1.0f - progress) * 0.18f, radius });
        wave->visual->SetRotationY(wave->age * (battlePhase_ >= 3 ? 1.4f : 0.86f));
        wave->visual->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        wave->visual->Update(deltaTime);
        wave->visual->UpdateWorldMatrix(true);

        if (wave->glow) {
            wave->glow->SetIsVisible(progress < 1.0f);
            wave->glow->SetTranslate(wave->center - Vector3{ 0.0f, 0.12f, 0.0f });
            wave->glow->SetScale({ radius, 0.10f, radius });
            wave->glow->SetRotationY(wave->age * (battlePhase_ >= 3 ? 1.4f : 0.86f));
            wave->glow->SetColor({
                battlePhase_ >= 3 ? 0.82f : 1.0f,
                battlePhase_ >= 3 ? 0.18f : 0.56f,
                battlePhase_ >= 3 ? 1.0f : 0.10f,
                (1.0f - progress) * 0.42f });
            wave->glow->Update(deltaTime);
            wave->glow->UpdateWorldMatrix(true);
        }

        if (!wave->hitTarget && target_) {
            const float distance = PlanarDistance(target_->GetWorldPosition(), wave->center);
            const float padding = target_->GetCollisionRadius() + 0.82f;
            // 波の上を飛び越えられるよう、接地付近だけを当たりにします。
            if (distance >= wave->previousRadius - padding && distance <= radius + padding &&
                std::abs(target_->GetWorldPosition().y - wave->center.y) <= 2.25f) {
                const EnemyAttackDefinition& attack = GetAttackDefinition(kRoyalShockwaveAttackId);
                const Vector3 direction = NormalizePlanar(target_->GetWorldPosition() - wave->center);
                DamageTargetAt(
                    target_->GetWorldPosition(),
                    padding,
                    2.25f,
                    attack.damage,
                    DamageType::Explosion,
                    { direction.x * 8.0f, 5.4f, direction.z * 8.0f });
                wave->hitTarget = true;
            }
        }
        wave->previousRadius = radius;
        if (progress >= 1.0f) {
            wave = royalWaves_.erase(wave);
            continue;
        }
        ++wave;
    }
}

void EnemyFalseKingSlime::BeginRush() {
    rushStep_ = 0;
    rushWingsVisual_ = CreateRoyalMesh(
        "FalseKing_RushWings",
        kRushWingsModel,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        battlePhase_ >= 3 ? 2.0f : 1.7f);
    if (rushWingsVisual_) {
        rushWingsVisual_->SetIsVisible(false);
        rushWingsVisual_->UpdateWorldMatrix(true);
    }
    BeginRushStep(rushStep_);
}

void EnemyFalseKingSlime::BeginRushStep(int step) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kKingRushAttackId);
    attackStartPosition_ = FindGroundPoint(GetTranslate());
    if (target_) {
        lockedTargetPosition_ = FindGroundPoint(target_->GetWorldPosition());
        lockedDirection_ = NormalizePlanar(lockedTargetPosition_ - attackStartPosition_);
    }
    const float distance = (std::min)(attack.maxRange, 13.0f + static_cast<float>(battlePhase_) * 2.6f);
    attackEndPosition_ = FindGroundPoint(attackStartPosition_ + lockedDirection_ * distance);
    rushStepTimer_ = 0.0f;
    rushStepDuration_ = battlePhase_ >= 3 ? 0.70f : 0.82f;
    rushDamageApplied_ = false;
    ShowAttackTelegraphLine(
        attackStartPosition_, lockedDirection_, distance, attack.radius * 2.0f, 0.85f,
        battlePhase_ >= 3 ? Vector4{ 0.88f, 0.18f, 1.0f, 0.82f } : Vector4{ 1.0f, 0.64f, 0.10f, 0.82f });
    (void)step;
}

void EnemyFalseKingSlime::UpdateRush(
    float deltaTime,
    const Vector3& targetDirection,
    Vector3& velocity) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kKingRushAttackId);
    const int rushCount = battlePhase_ == 1 ? 2 : 3;
    if (rushStep_ >= rushCount) {
        velocity = { 0.0f, 0.0f, 0.0f };
        HideAttackTelegraph();
        if (rushWingsVisual_) {
            rushWingsVisual_->SetIsVisible(false);
        }
        return;
    }
    rushStepTimer_ += deltaTime;
    const float progress = std::clamp(rushStepTimer_ / (std::max)(rushStepDuration_, 0.01f), 0.0f, 1.0f);
    const float chargeEnd = 0.22f;
    if (progress < chargeEnd) {
        velocity = { 0.0f, 0.0f, 0.0f };
        lockedDirection_ = NormalizePlanar(targetDirection);
        const float distance = (std::min)(attack.maxRange, 13.0f + static_cast<float>(battlePhase_) * 2.6f);
        attackEndPosition_ = FindGroundPoint(attackStartPosition_ + lockedDirection_ * distance);
        UpdateFacing(lockedDirection_, 0.26f);
        const float telegraphProgress = SmoothStep01(progress / chargeEnd);
        ShowAttackTelegraphLine(
            attackStartPosition_, lockedDirection_, attack.maxRange, attack.radius * 2.0f,
            telegraphProgress,
            { 1.0f, battlePhase_ >= 3 ? 0.18f : 0.60f, battlePhase_ >= 3 ? 1.0f : 0.08f, 0.82f });
        if (rushWingsVisual_) {
            const float yaw = std::atan2(lockedDirection_.x, lockedDirection_.z);
            const float spread = 0.55f + telegraphProgress * 0.55f;
            rushWingsVisual_->SetIsVisible(telegraphProgress > 0.08f);
            rushWingsVisual_->SetTranslate(attackStartPosition_ + Vector3{ 0.0f, 0.78f, 0.0f });
            rushWingsVisual_->SetRotationY(yaw);
            rushWingsVisual_->SetScale({ spread, 0.82f + telegraphProgress * 0.18f, spread });
            rushWingsVisual_->Update(deltaTime);
            rushWingsVisual_->UpdateWorldMatrix(true);
        }
        return;
    }
    HideAttackTelegraph();
    const float dashProgress = SmoothStep01((progress - chargeEnd) / (1.0f - chargeEnd));
    const Vector3 position = LerpVector3(attackStartPosition_, attackEndPosition_, dashProgress);
    SetTranslate(position);
    velocity = { 0.0f, 0.0f, 0.0f };
    UpdateFacing(lockedDirection_, 0.45f);
    if (rushWingsVisual_) {
        const float yaw = std::atan2(lockedDirection_.x, lockedDirection_.z);
        const float wingPulse = 1.22f + std::sin(rushStepTimer_ * 28.0f) * 0.08f;
        rushWingsVisual_->SetIsVisible(true);
        rushWingsVisual_->SetTranslate(position + Vector3{ 0.0f, 0.78f, 0.0f });
        rushWingsVisual_->SetRotationY(yaw);
        rushWingsVisual_->SetScale({ wingPulse, 1.0f, wingPulse });
        rushWingsVisual_->Update(deltaTime);
        rushWingsVisual_->UpdateWorldMatrix(true);
    }
    effectTimer_ -= deltaTime;
    if (effectTimer_ <= 0.0f) {
        EmitDirectedPreset(
            kRushWakePreset,
            position + Vector3{ 0.0f, 0.42f, 0.0f },
            { -lockedDirection_.x, 0.30f, -lockedDirection_.z },
            1.2f + static_cast<float>(battlePhase_) * 0.18f);
        effectTimer_ += 0.11f;
    }
    if (!rushDamageApplied_ && target_ &&
        PlanarDistance(target_->GetWorldPosition(), position) <= attack.radius + target_->GetCollisionRadius()) {
        DamageTargetAt(
            position,
            attack.radius + target_->GetCollisionRadius(),
            3.4f,
            attack.damage,
            DamageType::Physical,
            { lockedDirection_.x * 13.0f, 6.2f, lockedDirection_.z * 13.0f });
        rushDamageApplied_ = true;
    }
    if (progress >= 1.0f) {
        EmitDirectedPreset(kLanceImpactPreset, attackEndPosition_, { -lockedDirection_.x, 0.7f, -lockedDirection_.z }, 1.1f);
        ++rushStep_;
        if (rushStep_ < rushCount) {
            BeginRushStep(rushStep_);
        } else if (rushWingsVisual_) {
            rushWingsVisual_->SetIsVisible(false);
        }
    }
}

void EnemyFalseKingSlime::SpawnRoyalBeams(bool dominionPattern) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(
        dominionPattern ? kCrownDominionAttackId : kRoyalCrossAttackId);
    const int beamCount = dominionPattern ? 6 : (battlePhase_ >= 3 ? 4 : 3);
    const float baseAngle = std::atan2(lockedDirection_.x, lockedDirection_.z);
    for (int index = 0; index < beamCount; ++index) {
        RoyalBeam beam;
        beam.visual = CreateRoyalMesh(
            "FalseKing_RoyalBeam_" + std::to_string(index),
            kRoyalBeamModel,
            { 1.0f, 1.0f, 1.0f, 1.0f },
            dominionPattern ? 2.1f : 1.75f);
        beam.glow = CreateRoyalVisual(
            "FalseKing_RoyalBeamGlow_" + std::to_string(index),
            "Primitives/cube",
            kLaserMaterialType,
            dominionPattern ? Vector4{ 0.82f, 0.16f, 1.0f, 0.38f } : Vector4{ 1.0f, 0.62f, 0.08f, 0.38f },
            dominionPattern ? 2.2f : 1.8f);
        if (!beam.visual) {
            continue;
        }
        beam.origin = FindGroundPoint(GetTranslate());
        beam.origin.y += dominionPattern ? 0.48f : 0.38f;
        beam.delay = static_cast<float>(index) * (dominionPattern ? 0.045f : 0.06f);
        beam.lifetime = attack.activeDuration;
        beam.angle = baseAngle + static_cast<float>(index) * kTwoPi / static_cast<float>(beamCount);
        beam.angularSpeed = (dominionPattern ? 0.56f : 0.78f) * (index % 2 == 0 ? 1.0f : -1.0f);
        beam.length = attack.maxRange;
        beam.width = attack.radius * (dominionPattern ? 0.90f : 1.0f);
        beam.damageTimer = 0.0f;
        beam.visual->SetIsVisible(false);
        beam.visual->UpdateWorldMatrix(true);
        if (beam.glow) {
            beam.glow->SetIsVisible(false);
            beam.glow->UpdateWorldMatrix(true);
        }
        royalBeams_.push_back(std::move(beam));
    }
}

void EnemyFalseKingSlime::UpdateRoyalBeams(float deltaTime) {
    auto beam = royalBeams_.begin();
    while (beam != royalBeams_.end()) {
        if (!beam->visual) {
            beam = royalBeams_.erase(beam);
            continue;
        }
        beam->age += deltaTime;
        const float localAge = beam->age - beam->delay;
        if (localAge < 0.0f) {
            ++beam;
            continue;
        }
        const float progress = std::clamp(localAge / (std::max)(beam->lifetime, 0.01f), 0.0f, 1.0f);
        const float envelope = (std::min)(
            SmoothStep01(progress / 0.12f),
            SmoothStep01((1.0f - progress) / 0.14f));
        beam->angle += beam->angularSpeed * deltaTime;
        const Vector3 direction = { std::sin(beam->angle), 0.0f, std::cos(beam->angle) };
        const Vector3 center = beam->origin + direction * (beam->length * 0.5f);
        const float pulse = 0.92f + std::sin(localAge * 18.0f) * 0.08f;
        beam->visual->SetIsVisible(envelope > 0.01f);
        beam->visual->SetTranslate(center);
        beam->visual->SetRotationY(beam->angle);
        beam->visual->SetScale({ beam->width * pulse, 0.85f + envelope * 0.15f, beam->length * 0.5f });
        beam->visual->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        beam->visual->Update(deltaTime);
        beam->visual->UpdateWorldMatrix(true);
        if (beam->glow) {
            beam->glow->SetIsVisible(envelope > 0.01f);
            beam->glow->SetTranslate(center + Vector3{ 0.0f, 0.08f, 0.0f });
            beam->glow->SetRotationY(beam->angle);
            beam->glow->SetScale({
                beam->width * 0.42f * pulse,
                0.20f + envelope * 0.10f,
                beam->length * 0.5f });
            beam->glow->SetColor({
                battlePhase_ >= 3 ? 0.82f : 1.0f,
                battlePhase_ >= 3 ? 0.16f : 0.56f,
                battlePhase_ >= 3 ? 1.0f : 0.08f,
                envelope * 0.38f });
            beam->glow->Update(deltaTime);
            beam->glow->UpdateWorldMatrix(true);
        }
        if (envelope > 0.65f) {
            DamageTargetOnBeam(*beam, deltaTime);
        }
        if (progress >= 1.0f) {
            beam = royalBeams_.erase(beam);
            continue;
        }
        ++beam;
    }
}

void EnemyFalseKingSlime::UpdateDominion(float deltaTime) {
    if (dominionSigilVisual_) {
        const float duration = (std::max)(attackStateDuration_, 0.01f);
        const float progress = 1.0f - std::clamp(attackTimer_ / duration, 0.0f, 1.0f);
        const float pulse = 2.45f + std::sin(progress * kPi * 10.0f) * 0.14f;
        dominionSigilVisual_->SetIsVisible(true);
        dominionSigilVisual_->SetTranslate(GetTranslate() + Vector3{ 0.0f, 4.8f, 0.0f });
        dominionSigilVisual_->SetRotationY(idleTimer_ * 1.7f);
        dominionSigilVisual_->SetScale({ pulse, 1.0f, pulse });
        dominionSigilVisual_->Update(deltaTime);
        dominionSigilVisual_->UpdateWorldMatrix(true);
    }
    dominionLanceTimer_ -= deltaTime;
    if (dominionLanceTimer_ > 0.0f) {
        return;
    }
    PrepareLanceTargets(true);
    // 一度に全面を埋めず、6本ずつの回転帯にして必ず抜け道を残します。
    const int bandOffset = (actionIndex_ * 5) % static_cast<int>(lanceTargets_.size());
    for (int index = 0; index < 6; ++index) {
        const int targetIndex = (bandOffset + index * 3) % static_cast<int>(lanceTargets_.size());
        SpawnCrownLance(lanceTargets_[static_cast<size_t>(targetIndex)], static_cast<float>(index) * 0.055f);
    }
    ++actionIndex_;
    dominionLanceTimer_ += kDominionLanceInterval;
}

std::unique_ptr<Object3d> EnemyFalseKingSlime::CreateRoyalVisual(
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
    // 通常描画の発光コアと特殊マテリアルの外周を重ねても暗くならないようにします。
    visual->SetBlendMode(BlendMode::kAdd);
    visual->SetSelectedLighting(0);
    visual->SetEnableLighting(false);
    visual->SetEnableEnvMap(false);
    visual->SetColor(color);
    visual->SetEmissive(emissive);
    visual->SetRoughness(0.18f);
    visual->SetMetallic(0.0f);

    if (MeshRenderer* renderer = visual->GetMeshRenderer()) {
        if (MeshRenderer::WaterParamForGPU* parameter = renderer->GetWaterParamData()) {
            const bool shockwave = materialType == kShockwaveMaterialType;
            const bool crown = materialType == kCrownMaterialType;
            parameter->waveSpeed = shockwave ? 2.0f : 3.1f;
            parameter->waveHeight = shockwave ? 0.18f : 0.34f;
            parameter->waveFrequency = shockwave ? 5.8f : 3.6f;
            parameter->flowSpeedX = crown ? 0.10f : 0.04f;
            parameter->flowSpeedY = crown ? 0.42f : 0.74f;
            parameter->effectType = shockwave ? 1.0f : 0.0f;
            parameter->effectScale = crown ? 1.18f : 0.88f;
            parameter->effectSoftness = crown ? 0.48f : 0.28f;
            parameter->effectIntensity = crown ? 1.72f : 1.62f;
            parameter->billboardScale = 1.0f;
        }
    }
    return visual;
}

std::unique_ptr<Object3d> EnemyFalseKingSlime::CreateRoyalMesh(
    const std::string& name,
    const std::string& model,
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
    visual->SetMaterialType(0);
    visual->SetBlendMode(BlendMode::kNone);
    visual->SetEnableLighting(true);
    visual->SetEnableEnvMap(true);
    visual->SetEnvIntensity(0.72f);
    visual->SetColor(color);
    visual->SetEmissive(emissive);
    visual->SetRoughness(0.22f);
    visual->SetMetallic(0.55f);
    return visual;
}

Vector3 EnemyFalseKingSlime::FindGroundPoint(const Vector3& samplePosition) const {
    Vector3 rayStart = samplePosition;
    rayStart.y += 16.0f;
    PhysicsQueryFilter filter;
    filter.mask = kAllGround;
    filter.ignoredObject = const_cast<EnemyFalseKingSlime*>(this);
    const RaycastHit hit = CollisionManager::GetInstance()->Raycast(
        rayStart, { 0.0f, -1.0f, 0.0f }, 34.0f, filter);
    if (hit.isHit) {
        return hit.hitPoint;
    }
    Vector3 fallback = samplePosition;
    fallback.y = authoredPosition_.y;
    return fallback;
}

void EnemyFalseKingSlime::DamageTargetAt(
    const Vector3& center,
    float radius,
    float verticalTolerance,
    float damage,
    DamageType damageType,
    const Vector3& knockback) {
    if (!target_ || target_->isDead || !target_->GetIsVisible()) {
        return;
    }
    const Vector3 targetPosition = target_->GetWorldPosition();
    if (std::abs(targetPosition.y - center.y) > verticalTolerance ||
        PlanarDistance(targetPosition, center) > radius) {
        return;
    }
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = damage;
    event.damageType = damageType;
    event.knockbackVelocity = knockback;
    EventManager::GetInstance()->Dispatch(event);
}

void EnemyFalseKingSlime::DamageTargetOnBeam(RoyalBeam& beam, float deltaTime) {
    if (!target_ || target_->isDead || !target_->GetIsVisible()) {
        return;
    }
    beam.damageTimer = (std::max)(0.0f, beam.damageTimer - deltaTime);
    if (beam.damageTimer > 0.0f) {
        return;
    }
    const Vector3 direction = { std::sin(beam.angle), 0.0f, std::cos(beam.angle) };
    const Vector3 toTarget = target_->GetWorldPosition() - beam.origin;
    const float projection = toTarget.x * direction.x + toTarget.z * direction.z;
    if (projection < 0.0f || projection > beam.length ||
        std::abs(target_->GetWorldPosition().y - beam.origin.y) > 3.0f) {
        return;
    }
    const float lateralX = toTarget.x - direction.x * projection;
    const float lateralZ = toTarget.z - direction.z * projection;
    const float lateralDistance = std::sqrt(lateralX * lateralX + lateralZ * lateralZ);
    if (lateralDistance > beam.width + target_->GetCollisionRadius()) {
        return;
    }
    const EnemyAttackDefinition& attack = GetAttackDefinition(
        currentAttack_ == AttackKind::CrownDominion ? kCrownDominionAttackId : kRoyalCrossAttackId);
    const Vector3 side = { direction.z, 0.0f, -direction.x };
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = attack.damage;
    event.damageType = DamageType::Electric;
    event.knockbackVelocity = { side.x * 7.0f, 3.8f, side.z * 7.0f };
    EventManager::GetInstance()->Dispatch(event);
    beam.damageTimer = 0.72f;
}

void EnemyFalseKingSlime::UpdateFacing(const Vector3& direction, float follow) {
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z) + kModelYawOffset;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, (std::clamp)(follow, 0.0f, 1.0f)));
}

void EnemyFalseKingSlime::ApplyBossAnimation(float deltaTime) {
    SlimeBounceAnimator::Params params;
    params.speedForFullBounce = 2.8f;
    params.idleAmplitude = 0.050f;
    params.moveAmplitude = 0.14f;
    params.hopFrequency = 3.2f + static_cast<float>(battlePhase_) * 0.45f;
    params.horizontalSquash = 0.13f;
    params.verticalStretch = 0.17f;
    params.airborneStretch = 0.22f;

    Vector3 targetScale = SlimeBounceAnimator::MakeScale(
        baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    Vector3 targetRotation = { authoredRotation_.x, GetRotation().y, authoredRotation_.z };
    if (attackState_ == AttackState::Windup) {
        const float progress = SmoothStep01(1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f));
        const float tremble = std::sin(progress * kPi * 18.0f) * progress * 0.045f;
        targetScale = {
            baseScale_.x * (1.0f + progress * 0.35f + tremble),
            baseScale_.y * (1.0f - progress * 0.32f),
            baseScale_.z * (1.0f + progress * 0.35f - tremble),
        };
        targetRotation.z += tremble * 1.7f;
    } else if (attackState_ == AttackState::Active) {
        const float progress = 1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f);
        if (currentAttack_ == AttackKind::KingRush) {
            const float pulse = std::sin(progress * kPi * 14.0f) * 0.04f;
            targetScale = {
                baseScale_.x * (1.20f + pulse),
                baseScale_.y * 0.70f,
                baseScale_.z * (1.48f - pulse),
            };
            targetRotation.x -= 0.13f;
        } else {
            const float castPulse = std::sin(progress * kPi * 10.0f);
            targetScale = {
                baseScale_.x * (1.18f + std::abs(castPulse) * 0.08f),
                baseScale_.y * (0.80f - castPulse * 0.07f),
                baseScale_.z * (1.18f + std::abs(castPulse) * 0.08f),
            };
            targetRotation.z += castPulse * 0.055f;
        }
    } else if (attackState_ == AttackState::PhaseShift) {
        const float progress = GetPhaseTransitionProgress();
        const float surge = std::sin(progress * kPi);
        const float tremble = std::sin(progress * kPi * 24.0f) * surge * 0.05f;
        targetScale = {
            baseScale_.x * (1.0f + surge * 0.34f + tremble),
            baseScale_.y * (1.0f + surge * 0.42f),
            baseScale_.z * (1.0f + surge * 0.34f - tremble),
        };
        targetRotation.z += tremble * 1.8f;
    } else if (attackState_ == AttackState::Recovery) {
        const float remaining = std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
        const float bounce = std::sin((1.0f - remaining) * kPi * 2.6f) * remaining;
        targetScale = {
            baseScale_.x * (1.0f + bounce * 0.20f),
            baseScale_.y * (1.0f - bounce * 0.27f),
            baseScale_.z * (1.0f + bounce * 0.20f),
        };
    }
    ApplyDamageReactionPose(targetScale, targetRotation);
    const float follow = 1.0f - std::exp(-deltaTime * 11.0f);
    const Vector3 currentScale = GetScale();
    SetScale({
        Math::Lerp(currentScale.x, targetScale.x, follow),
        Math::Lerp(currentScale.y, targetScale.y, follow),
        Math::Lerp(currentScale.z, targetScale.z, follow),
    });
    const Vector3 currentRotation = GetRotation();
    SetRotation({
        Math::Lerp(currentRotation.x, targetRotation.x, follow),
        currentRotation.y,
        Math::Lerp(currentRotation.z, targetRotation.z, follow),
    });
}

void EnemyFalseKingSlime::UpdateRoyalGlow(float deltaTime) {
    ambientSparkTimer_ -= deltaTime;
    const bool attacking = attackState_ == AttackState::Windup || attackState_ == AttackState::Active || attackState_ == AttackState::PhaseShift;
    const float pulse = 0.5f + std::sin(idleTimer_ * (attacking ? 8.2f : 3.4f)) * 0.5f;
    Vector4 phaseColor = authoredColor_;
    if (battlePhase_ == 2) {
        phaseColor = { 1.0f, 0.90f, 1.0f, authoredColor_.w };
    } else if (battlePhase_ >= 3) {
        phaseColor = { 0.92f, 0.78f, 1.0f, authoredColor_.w };
    }
    defaultColor_ = phaseColor;
    SetColor(phaseColor);
    SetEmissive(authoredEmissive_ + static_cast<float>(battlePhase_ - 1) * 0.34f + (attacking ? 0.52f : 0.0f) + pulse * 0.12f);
    if (!attacking) {
        ambientSparkTimer_ = 0.0f;
        return;
    }
    if (ambientSparkTimer_ <= 0.0f) {
        EmitDirectedPreset(
            battlePhase_ >= 3 ? kDominionPreset : kChargePreset,
            GetWorldPosition() + Vector3{ 0.0f, baseScale_.y * 1.28f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            1.12f);
        ambientSparkTimer_ += battlePhase_ >= 3 ? 0.14f : 0.18f;
    }
}

void EnemyFalseKingSlime::EmitPreset(const char* presetName, const Vector3& position) const {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyFalseKingSlime::EmitDirectedPreset(
    const char* presetName,
    const Vector3& position,
    const Vector3& direction,
    float speedScale) const {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyFalseKingSlime::ClearTransientVisuals() {
    crownLances_.clear();
    royalWaves_.clear();
    royalBeams_.clear();
    rushWingsVisual_.reset();
    dominionSigilVisual_.reset();
}

bool EnemyFalseKingSlime::UpdateEncounterState(float deltaTime) {
    if (!IsEncounterControlled()) {
        encounterState_ = EncounterState::Normal;
        initializedForPlay_ = false;
        return false;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->IsPlaying()) {
        ResetForEditor();
        return true;
    }

    if (!initializedForPlay_) {
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

    encounterTimer_ += deltaTime;
    idleTimer_ += deltaTime;
    const float progress = std::clamp(encounterTimer_ / GetAppearanceDuration(), 0.0f, 1.0f);
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    float horizontal = 1.0f;
    float vertical = 1.0f;
    float heightOffset = 0.0f;
    if (progress < 0.45f) {
        const float rate = SmoothStep01(progress / 0.45f);
        horizontal = Math::Lerp(0.44f, 1.24f, rate);
        vertical = Math::Lerp(0.16f, 0.68f, rate);
        heightOffset = (1.0f - rate) * kAppearanceRise;
    } else if (progress < 0.68f) {
        const float rate = SmoothStep01((progress - 0.45f) / 0.23f);
        horizontal = Math::Lerp(1.24f, 0.87f, rate);
        vertical = Math::Lerp(0.68f, 1.19f, rate);
        heightOffset = std::sin(rate * kPi) * 0.48f;
    } else if (progress < 0.88f) {
        const float rate = SmoothStep01((progress - 0.68f) / 0.20f);
        horizontal = Math::Lerp(0.87f, 1.08f, rate);
        vertical = Math::Lerp(1.19f, 0.90f, rate);
        heightOffset = std::sin(rate * kPi) * 0.16f;
    } else {
        const float rate = SmoothStep01((progress - 0.88f) / 0.12f);
        horizontal = Math::Lerp(1.08f, 1.0f, rate);
        vertical = Math::Lerp(0.90f, 1.0f, rate);
    }

    SetTranslate({ authoredPosition_.x, authoredPosition_.y + heightOffset, authoredPosition_.z });
    SetScale({ authoredScale_.x * horizontal, authoredScale_.y * vertical, authoredScale_.z * horizontal });
    SetRotation({
        authoredRotation_.x,
        authoredRotation_.y + std::sin(progress * kPi) * 0.12f,
        authoredRotation_.z + std::sin(progress * kPi * 2.0f) * 0.055f,
    });
    SetColor({
        authoredColor_.x,
        authoredColor_.y,
        authoredColor_.z,
        std::max(0.01f, SmoothStep01(std::min(progress / 0.30f, 1.0f))),
    });
    SetEmissive(1.0f + std::sin(std::min(progress / 0.72f, 1.0f) * kPi) * 2.2f);
    SyncCollisionBounds();
    if (progress >= 1.0f) {
        FinishAppearance();
    }
    return true;
}

void EnemyFalseKingSlime::CaptureAuthoredState(bool force) {
    if (authoredStateCaptured_ && !force) {
        return;
    }
    authoredPosition_ = GetTranslate();
    authoredScale_ = GetScale();
    authoredRotation_ = GetRotation();
    authoredColor_ = GetColor();
    authoredCollisionAttribute_ = GetCollisionAttribute();
    authoredCollisionMask_ = GetCollisionMask();
    authoredMaterialType_ = GetMaterialType();
    authoredEmissive_ = GetEmissive();
    if (authoredCollisionAttribute_ == 0) authoredCollisionAttribute_ = kEnemy;
    if (authoredCollisionMask_ == 0) authoredCollisionMask_ = kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet;
    baseScale_ = authoredScale_;
    hasBaseScale_ = true;
    authoredStateCaptured_ = true;
}

void EnemyFalseKingSlime::InitializeEncounterState() {
    initializedForPlay_ = true;
    CaptureAuthoredState(true);
    battlePhase_ = 1;
    pendingBattlePhase_ = 1;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackCooldown_ = 1.05f;
    const bool startActive = param_.has_value() && param_->startActive;
    if (startActive || activationRequested_) {
        BeginAppearance();
    } else {
        ApplyDormantState();
    }
}

void EnemyFalseKingSlime::ResetForEditor() {
    if (initializedForPlay_) {
        SetTranslate(authoredPosition_);
        SetScale(authoredScale_);
        SetRotation(authoredRotation_);
        SetColor(authoredColor_);
        SetMaterialType(authoredMaterialType_);
        SetEmissive(authoredEmissive_);
    }
    CaptureAuthoredState(true);
    initializedForPlay_ = false;
    activationRequested_ = false;
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    battlePhase_ = 1;
    pendingBattlePhase_ = 1;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetCollisionAttribute(authoredCollisionAttribute_);
    SetCollisionMask(authoredCollisionMask_);
    SetIsVisible(true);
    ClearTransientVisuals();
    HideAttackTelegraph();
    SyncCollisionBounds();
}

void EnemyFalseKingSlime::BeginAppearance() {
    encounterState_ = EncounterState::Appearing;
    encounterTimer_ = 0.0f;
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    Vector3 effectPosition = authoredPosition_;
    effectPosition.y += 0.2f;
    VFXSequencer::PlayOneShot(kAppearanceSequence, effectPosition, { 2.4f, 2.4f, 2.4f });
}

void EnemyFalseKingSlime::ApplyDormantState() {
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    SetTranslate(authoredPosition_);
    SetScale(authoredScale_);
    SetRotation(authoredRotation_);
    SetColor(authoredColor_);
    SetMaterialType(authoredMaterialType_);
    SetEmissive(authoredEmissive_);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearTransientVisuals();
    HideAttackTelegraph();
}

void EnemyFalseKingSlime::FinishAppearance() {
    encounterState_ = EncounterState::Active;
    encounterTimer_ = 0.0f;
    SetTranslate(authoredPosition_);
    SetScale(authoredScale_);
    SetRotation(authoredRotation_);
    SetColor(authoredColor_);
    SetMaterialType(authoredMaterialType_);
    SetEmissive(authoredEmissive_);
    SetCollisionAttribute(authoredCollisionAttribute_);
    SetCollisionMask(authoredCollisionMask_);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetIsVisible(true);
    attackCooldown_ = 0.82f;
    SyncCollisionBounds();
}

void EnemyFalseKingSlime::SyncCollisionBounds() {
    Object3d::ColliderConfig collider = GetColliderConfig();
    collider.type = ColliderType::kOBB;
    collider.center = { 0.0f, 1.48f, 0.0f };
    collider.size = { 2.82f, 1.46f, 2.28f };
    SetColliderConfig(collider);
}

bool EnemyFalseKingSlime::IsEncounterControlled() const {
    return param_.has_value() && param_->actionMode == kEncounterControlledActionMode;
}

float EnemyFalseKingSlime::GetAppearanceDuration() const {
    return param_.has_value()
        ? std::clamp(param_->shakeDuration, 0.55f, 3.5f)
        : kDefaultAppearanceDuration;
}

bool EnemyFalseKingSlime::IsEncounterHudActive() const {
    if (!IsEncounterControlled() || isDead) {
        return false;
    }
    return encounterState_ == EncounterState::Appearing || encounterState_ == EncounterState::Active;
}

float EnemyFalseKingSlime::GetEncounterCurrentHp() const {
    return param_.has_value() ? std::max(0.0f, param_->hp) : 0.0f;
}

float EnemyFalseKingSlime::GetEncounterMaximumHp() const {
    return param_.has_value() ? std::max(1.0f, param_->maxHp) : 1.0f;
}

float EnemyFalseKingSlime::GetEncounterAppearanceProgress() const {
    if (encounterState_ == EncounterState::Active) {
        return 1.0f;
    }
    if (encounterState_ != EncounterState::Appearing) {
        return 0.0f;
    }
    return std::clamp(encounterTimer_ / GetAppearanceDuration(), 0.0f, 1.0f);
}

float EnemyFalseKingSlime::GetPhaseTransitionProgress() const {
    if (attackState_ != AttackState::PhaseShift) {
        return battlePhase_ == pendingBattlePhase_ ? 1.0f : 0.0f;
    }
    return std::clamp(phaseTransitionTimer_ / (std::max)(phaseTransitionDuration_, 0.01f), 0.0f, 1.0f);
}

void EnemyFalseKingSlime::TriggerDebugDefeat() {
    if (!param_.has_value() || isDead || IsDefeatEffectPlaying()) {
        return;
    }
    param_->hp = 0.0f;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    ClearTransientVisuals();
    HideAttackTelegraph();
}
