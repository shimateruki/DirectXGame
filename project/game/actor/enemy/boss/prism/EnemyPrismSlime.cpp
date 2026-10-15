#include "game/actor/combat/CombatGroundQuery.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.Internal.h"
#include "game/actor/enemy/boss/core/BossChantVisual.h"
#include "game/actor/enemy/animation/BossGroundHopVfx.h"

using namespace EnemyPrismSlimeImplementation;

namespace {
constexpr float kLocomotionHopInterval = 0.62f;
constexpr float kLocomotionAnticipationDuration = 0.24f;
constexpr float kLocomotionHopPowerScale = 0.52f;
constexpr float kLocomotionLandingDuration = 0.25f;
} // namespace

// プリズムボスの属性フェーズと攻撃状態を管理します。詠唱・移動・攻撃の優先順位を保って更新します。

EnemyPrismSlime::EnemyPrismSlime() = default;
EnemyPrismSlime::~EnemyPrismSlime() = default;

void EnemyPrismSlime::BindSceneSpawnContext(BaseScene* scene) {
    // 召喚した子も同じシーンへ登録できるよう、シーンが有効な間だけ生成コールバックを結ぶ。
    BaseEnemy::BindSceneSpawnContext(scene);
    if (!scene) {
        SetSpawnCallback(nullptr);
        return;
    }
    SetSpawnCallback([scene](std::unique_ptr<BaseEnemy> spawned) {
        if (!spawned) return;
        spawned->BindSceneSpawnContext(scene);
        scene->AddObject(std::move(spawned));
    });
}

void EnemyPrismSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 本体判定と、属性で色を変える顔・コアの独立描画部品を準備する。
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_PrismSlime");
    SetEnemyType("PrismSlime");
    ReloadAttackProfile();
    SetMaterialType(kPrismCrystalMaterialType);
    SetColor({0.34f, 0.22f, 0.62f, 1.0f});
    SetMetallic(0.84f);
    SetRoughness(0.12f);
    SetEnableEnvMap(true);
    SetEnvIntensity(1.0f);
    SetEmissive(0.92f);
    defaultColor_ = GetColor();

    SetCollisionAttribute(kPrismActiveCollisionAttribute);
    SetCollisionMask(kPrismActiveCollisionMask);
    SetColliderType(ColliderType::kSphere);
    SyncCollisionRadius();

    InitializeFaceParts();
    UpdatePhaseAppearance();
}

void EnemyPrismSlime::Update(float deltaTime) {
    // 遭遇演出と非行動状態を先に処理し、通常時だけ属性・移動・攻撃の更新へ進む。
    if (UpdateEncounterState(deltaTime)) {
        UpdateFaceParts(deltaTime);
        return;
    }

    if (UpdateInactiveState(deltaTime)) {
        UpdateFaceParts(deltaTime);
        return;
    }

    UpdatePrismSpikeVisuals(deltaTime);
    UpdateCrystalLanceVisuals(deltaTime);
    UpdateSummonPortalVisuals(deltaTime);
    EnsureBaseScale();
    idleTimer_ += deltaTime;
    locomotionLandingTimer_ = (std::max)(0.0f, locomotionLandingTimer_ - deltaTime);
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    impactPulseTimer_ = (std::max)(0.0f, impactPulseTimer_ - deltaTime);
    UpdateElementPhase();

    engine::Vector3 direction = lockedDirection_;
    float distance = 9999.0f;
    if (target_) {
        const engine::Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    }
    UpdateBossTargetEngagement(distance);

    const engine::Vector3 previousVelocity = GetVelocity();
    engine::Vector3 velocity = previousVelocity;
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    UpdateBehavior(deltaTime, direction, distance, velocity);
    const float locomotionIntentSpeed = SlimeBounceAnimator::PlanarSpeed(velocity);
    if (attackState_ == AttackState::Idle && target_ && param_.has_value()) {
        velocity = CombatMath::SmoothPlanarVelocity(previousVelocity, velocity, deltaTime, 7.5f);
        const engine::Vector3 candidate = GetTranslate() + engine::Vector3{velocity.x * 0.38f, 0.0f, velocity.z * 0.38f};
        if (isGrounded_ && !CombatGroundQuery::CanWalkTo(GetTranslate(), candidate, this, {4.0f, 12.0f}, 1.4f)) {
            velocity.x = 0.0f;
            velocity.z = 0.0f;
        }
    }
    if (attackState_ == AttackState::Idle) {
        if (locomotionAirborne_ && isGrounded_) {
            locomotionLandingTimer_ = kLocomotionLandingDuration;
            locomotionAirborne_ = false;
            BossGroundHopVfx::EmitPuff(
                CombatGroundQuery::FindGroundPoint(GetTranslate(), this, {4.0f, 12.0f}, GetTranslate().y) +
                    engine::Vector3{0.0f, 0.04f, 0.0f},
                {0.62f, 0.92f, 1.0f, 0.72f}, 2.5f, true);
        }
        if (SlimeBounceAnimator::StepBossGroundHop(locomotionHopTimer_, velocity, locomotionIntentSpeed,
                                                   isGrounded_ && param_.has_value(), deltaTime, kLocomotionHopInterval,
                                                   kLocomotionAnticipationDuration, 0.22f)) {
            velocity.y = (std::max)(velocity.y, std::clamp(param_->jumpPower * kLocomotionHopPowerScale, 9.0f, 14.5f));
            locomotionAirborne_ = true;
            BossGroundHopVfx::EmitPuff(
                CombatGroundQuery::FindGroundPoint(GetTranslate(), this, {4.0f, 12.0f}, GetTranslate().y) +
                    engine::Vector3{0.0f, 0.04f, 0.0f},
                {0.62f, 0.92f, 1.0f, 0.58f}, 2.2f, false);
        }
    } else {
        locomotionHopTimer_ = 0.0f;
        locomotionLandingTimer_ = 0.0f;
        locomotionAirborne_ = false;
    }
    SetVelocity(velocity);

    ApplySlimeAnimation(deltaTime);
    SyncCollisionRadius();
    BaseEnemy::Update(deltaTime);
    UpdateCrystalCircleVisual(deltaTime);
    UpdatePrismSpikeSectorVisual(deltaTime);
    UpdateWindSpellVisual(deltaTime);
    UpdateChantSigilVisual(deltaTime);
    UpdateFaceParts(deltaTime);
}

void EnemyPrismSlime::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (debugPreviewChantOnly_) {
        if (chantVisual_) {
            chantVisual_->Draw(pointLightResource, spotLightResource);
        }
        return;
    }
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
    if (crystalCircle_ && crystalCircle_->GetIsVisible()) {
        crystalCircle_->RefreshRenderCameraData();
        crystalCircle_->Draw(pointLightResource, spotLightResource);
    }
    if (crystalCircleGlow_ && crystalCircleGlow_->GetIsVisible()) {
        crystalCircleGlow_->RefreshRenderCameraData();
        crystalCircleGlow_->Draw(pointLightResource, spotLightResource);
    }
    if (prismSpikeSector_ && prismSpikeSector_->GetIsVisible()) {
        prismSpikeSector_->RefreshRenderCameraData();
        prismSpikeSector_->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& sigil : prismSpikeSigil_) {
        if (sigil && sigil->GetIsVisible()) {
            sigil->RefreshRenderCameraData();
            sigil->Draw(pointLightResource, spotLightResource);
        }
    }
    auto drawWindEffect = [&](const std::unique_ptr<EffectObject3d>& effect) {
        if (effect && effect->GetIsVisible()) {
            effect->RefreshRenderCameraData();
            effect->Draw(pointLightResource, spotLightResource);
        }
    };
    for (const WindVortexVisual& vortex : windVortices_) {
        drawWindEffect(vortex.circle);
        drawWindEffect(vortex.circleGlow);
        for (const auto& effect : vortex.outer)
            drawWindEffect(effect);
        for (const auto& effect : vortex.inner)
            drawWindEffect(effect);
    }
    if (chantVisual_) {
        chantVisual_->Draw(pointLightResource, spotLightResource);
    }
    for (const SummonPortalVisual& portal : summonPortalVisuals_) {
        if (portal.effect && portal.effect->GetIsVisible()) {
            portal.effect->RefreshRenderCameraData();
            portal.effect->Draw(pointLightResource, spotLightResource);
        }
        if (portal.glow && portal.glow->GetIsVisible()) {
            portal.glow->RefreshRenderCameraData();
            portal.glow->Draw(pointLightResource, spotLightResource);
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

std::unique_ptr<engine::Object3d> EnemyPrismSlime::Clone() const {
    // 配置値と追跡対象を引き継ぎ、攻撃中の一時状態は新しい個体で初期化する。
    auto clone = std::make_unique<EnemyPrismSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyPrismSlime::OnSwitchEvent(bool active) {
    // 遭遇制御下ではスイッチを出現・休眠の要求として扱い、通常配置では基底の動作へ委ねる。
    if (!IsEncounterControlled()) {
        BaseEnemy::OnSwitchEvent(active);
        return;
    }

    encounterRequestedActive_ = active;
    if (!encounterInitializedForPlay_) {
        InitializeEncounterState();
    }

    if (active) {
        if (encounterState_ == EncounterState::Dormant) {
            BeginEncounterAppearance();
        }
    } else if (encounterState_ != EncounterState::Dormant) {
        ApplyDormantEncounterState();
    }
}

void EnemyPrismSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    // 技の切替時に旧技の予兆と補助描画物を消し、プレビューを待機状態からやり直す。
    debugPreviewAttackId_ = attackId;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackCooldown_ = 0.0f;
    attackTimer_ = 0.0f;
    actionTimer_ = 0.0f;
    actionIndex_ = 0;
    repositionTimer_ = 0.0f;
    repositionDuration_ = 0.0f;
    repositionJumpPending_ = false;
    ClearPrismAttackVisuals();
    HideAttackTelegraph();
}

const char* EnemyPrismSlime::GetDebugAttackPhaseName() const {
    // 攻撃種別と予備動作・実行状態を組み合わせ、プレビューで見える段階名に変える。
    if (attackState_ == AttackState::Idle) {
        return "プリズム・待機／移動";
    }
    if (attackState_ == AttackState::Recovery) {
        return "プリズム・反発／回復";
    }
    if (attackState_ == AttackState::Reposition) {
        return "プリズム・跳躍移動";
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
        return attackState_ == AttackState::Windup ? "風・魔法陣を構築" : "風・竜巻";
    case AttackKind::SlimeSummon:
        return attackState_ == AttackState::Windup ? "召喚陣・出現予告" : "召喚陣・スライム転送";
    default:
        return "プリズム・攻撃";
    }
}

void EnemyPrismSlime::ApplyManagedScale(const engine::Vector3& scale) {
    // シーン編集で変更した倍率をアニメーションの基準へ反映し、判定半径も更新する。
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
    SyncCollisionRadius();
}

bool EnemyPrismSlime::UpdateInactiveState(float deltaTime) {
    // 敗北・運搬・投げ回復中は予兆を消し、通常AIの攻撃更新へ入れない。
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
    // 攻撃で身体を伸縮させても配置時のサイズへ戻せるよう、基準倍率を一度だけ保存する。
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

// HP 段階から属性を選びます。攻撃プレビュー中は選択技の属性を優先します。
void EnemyPrismSlime::UpdateElementPhase() {
    // HP段階またはデバッグで選んだ技から属性を決め、表示と次の攻撃選択を同期する。
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
        case AttackKind::FireFan:
            nextPhase = ElementPhase::Fire;
            break;
        case AttackKind::ThunderChain:
            nextPhase = ElementPhase::Thunder;
            break;
        case AttackKind::WindWave:
            nextPhase = ElementPhase::Wind;
            break;
        default:
            nextPhase = ElementPhase::Neutral;
            break;
        }
    }

    if (nextPhase != elementPhase_) {
        elementPhase_ = nextPhase;
        UpdatePhaseAppearance();
        impactPulseTimer_ = 0.32f;
        EmitPreset(kPrismPhasePulsePreset, GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 0.48f, 0.0f});
    }
}

// 再配置・攻撃中の更新を優先し、待機中は次の技の射程へ移動します。
void EnemyPrismSlime::UpdateBehavior(float deltaTime, const engine::Vector3& direction, float distance, engine::Vector3& velocity) {
    if (attackState_ == AttackState::Reposition) {
        UpdateReposition(deltaTime, velocity);
        return;
    }
    if (attackState_ != AttackState::Idle) {
        UpdateAttack(deltaTime, direction, distance);
        return;
    }

    if (!target_ || !param_.has_value()) {
        const float verticalVelocity = velocity.y;
        velocity = CalculateWanderVelocity(deltaTime, 0.55f, 0.52f);
        velocity.y = verticalVelocity;
        UpdateFacing(velocity);
        return;
    }

    UpdateFacing(direction);
    if (UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        return;
    }

    const AttackKind debugAttack = ResolveDebugAttack();
    const AttackKind plannedAttack = debugAttack != AttackKind::None ? debugAttack : ResolveAutomaticAttack(distance);
    const EnemyAttackDefinition& plannedDefinition = plannedAttack == AttackKind::FireFan
        ? SelectFireFanDefinition(distance) : GetAttackDefinition(GetAttackId(plannedAttack));
    const bool isInAttackRange = distance >= plannedDefinition.minRange && distance <= plannedDefinition.maxRange;
    if ((!locomotionAirborne_ || isGrounded_) && IsBossTargetEngaged() && attackCooldown_ <= 0.0f && isInAttackRange) {
        StartAttack(plannedAttack, direction, distance);
        return;
    }

    // 射程外では待機用の歩速を使わず、次の技が届く位置まで詰めます。
    const float moveSpeed = distance > plannedDefinition.maxRange ? (std::max)(5.5f, param_->speed * 3.0f)
                                                                  : (std::max)(0.75f, param_->speed);
    const float approachDistance = (std::min)(9.0f, (std::max)(2.0f, plannedDefinition.maxRange * 0.86f));
    const engine::Vector3 spacing = CombatMath::MakePacedSpacingVelocity(direction, distance, 4.2f, approachDistance, moveSpeed,
                                                                 0.62f, 0.58f, idleTimer_, 4.6f);
    velocity.x = spacing.x;
    velocity.z = spacing.z;
}

// 攻撃開始時の方向と距離を保存し、予兆から実行へ進む基準にします。
void EnemyPrismSlime::StartAttack(AttackKind kind, const engine::Vector3& direction, float distance) {
    if (kind == AttackKind::None) {
        return;
    }
    if (kind == AttackKind::FireFan && debugPreviewAttackId_.empty() && attackProfile_.selectionMode != "priority" &&
        !attackProfile_.SelectSequence(distance, 100000.0f, {}, 0.0f)) {
        // 全候補が重み0・射程外なら旧火球へ戻さず、外側の行動サイクルを次へ進めます。
        ++attackCycleIndex_;
        attackCooldown_ = 0.1f;
        return;
    }

    currentAttack_ = kind;
    attackState_ = AttackState::Windup;
    lockedDirection_ = NormalizePlanar(direction);
    lockedTargetDistance_ = distance;
    attackSequence_.Cancel();
    if (kind == AttackKind::FireFan) selectedFireFan_ = SelectFireFanDefinition(distance);
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
    } else if (kind == AttackKind::WindWave) {
        const float distanceToCircle = std::clamp(lockedTargetDistance_, 3.2f, (std::max)(3.2f, attack.maxRange));
        windSpellOrigin_ = FindGroundPoint(GetWorldPosition() + lockedDirection_ * distanceToCircle);
        windTornadoAge_ = 0.0f;
        const engine::Vector3 right = {lockedDirection_.z, 0.0f, -lockedDirection_.x};
        for (std::size_t index = 0; index < windVortices_.size(); ++index) {
            WindVortexVisual& vortex = windVortices_[index];
            const float side = index == 0 ? 0.0f : (index == 1 ? -attack.radius * 2.1f : attack.radius * 2.1f);
            const float forward = index == 0 ? 0.0f : (index == 1 ? -attack.radius * 0.85f : attack.radius * 1.05f);
            engine::Vector3 sample = windSpellOrigin_ + right * side + lockedDirection_ * forward;
            if (IsEncounterControlled()) {
                const float margin = attack.radius * 0.72f;
                sample.x = std::clamp(sample.x, encounterBasePosition_.x - kRepositionArenaHalfExtentX + margin,
                                      encounterBasePosition_.x + kRepositionArenaHalfExtentX - margin);
                sample.z = std::clamp(sample.z, encounterBasePosition_.z - kRepositionArenaHalfExtentZ + margin,
                                      encounterBasePosition_.z + kRepositionArenaHalfExtentZ - margin);
            }
            vortex.origin = FindGroundPoint(sample);
            vortex.position = vortex.origin;
            vortex.direction = {};
            vortex.driftTimer = 0.0f;
            vortex.circle = CreateSpellEffect(kWindChantSigilEffect);
            vortex.circleGlow = CreateSpellEffect(kWindChantSigilEffect);
            if (vortex.circleGlow)
                vortex.circleGlow->SetBlendMode(BlendMode::kAdd);
            for (auto& effect : vortex.outer)
                effect = CreateSpellEffect(kPrismWindTornadoOuterEffect);
            for (auto& effect : vortex.inner)
                effect = CreateSpellEffect(kPrismWindTornadoInnerEffect);
        }
    } else if (kind == AttackKind::SlimeSummon) {
        PrepareSummonPortals();
        if (debugPreviewAttackId_.empty()) {
            ++summonWavesUsed_;
        }
    }
    BossChantStyle chantStyle;
    chantStyle.sigilEffectPath = kPrismChantSigilEffect;
    chantStyle.motePreset = kPrismSpellMotesPreset;
    chantStyle.glintPreset = "prism_chant_glints";
    chantStyle.releasePreset = kPrismLanceReleasePreset;
    chantStyle.accentColor = {0.40f, 0.75f, 1.0f, 1.0f};
    chantStyle.radius = std::clamp(baseScale_.x * 1.55f, 2.2f, 3.0f);
    // 詠唱陣は攻撃を放つ間も残し、終了直前にだけ薄くします。
    const float releaseDuration = kind == AttackKind::FireFan && attack.sequence.enabled
        ? (std::max)(attack.activeDuration, attack.sequence.duration) : attack.activeDuration;
    chantStyle.fadeStart = (std::max)(0.0f, releaseDuration - 0.24f);
    if (kind == AttackKind::PrismSpikes) {
        chantStyle.releasePreset = kPrismSpikeBurstPreset;
    } else if (kind == AttackKind::SlimeSummon) {
        chantStyle.releasePreset = kPrismPhasePulsePreset;
    } else if (kind == AttackKind::FireFan) {
        chantStyle.glintPreset.clear();
        chantStyle.sigilEffectPath = kFireChantSigilEffect;
        chantStyle.motePreset = kFireChantMotesPreset;
        chantStyle.releasePreset = kFireCastPreset;
        chantStyle.accentColor = {1.0f, 0.35f, 0.08f, 1.0f};
    } else if (kind == AttackKind::ThunderChain) {
        chantStyle.glintPreset.clear();
        chantStyle.sigilEffectPath = kThunderChantSigilEffect;
        chantStyle.motePreset = kThunderChantMotesPreset;
        chantStyle.releasePreset = "thunder_slime_discharge";
        chantStyle.accentColor = {1.0f, 0.90f, 0.28f, 1.0f};
    } else if (kind == AttackKind::WindWave) {
        chantStyle.glintPreset.clear();
        chantStyle.sigilEffectPath = kWindChantSigilEffect;
        chantStyle.motePreset = kWindChantMotesPreset;
        chantStyle.releasePreset = kWindImpactPreset;
        chantStyle.accentColor = {0.45f, 1.0f, 0.78f, 1.0f};
    }
    chantVisual_ = std::make_unique<BossChantVisual>();
    if (!chantVisual_->Start(common_, std::move(chantStyle))) {
        chantVisual_.reset();
    }
    if (debugPreviewAttackId_.empty()) {
        lastAutomaticAttack_ = kind;
        ++attackCycleIndex_;
        ++automaticAttackSerial_;
    }
}

int EnemyPrismSlime::GetDebugChantVisibleLayerCount() const {
    // 詠唱の重ね描きがどこまで進んだかをプレビューへ返す。
    if (attackState_ != AttackState::Windup && attackState_ != AttackState::Active) {
        return -2;
    }
    return chantVisual_ ? chantVisual_->GetVisibleLayerCount() : -1;
}

void EnemyPrismSlime::UpdateAttack(float deltaTime, const engine::Vector3& direction, float distance) {
    if (attackState_ == AttackState::Windup) {
        // 着弾位置を先に決める攻撃は、表示・判定・発生位置の向きを固定します。
        const bool hasFixedGroundTarget = currentAttack_ == AttackKind::PrismSpikes ||
                                          currentAttack_ == AttackKind::ThunderChain ||
                                          currentAttack_ == AttackKind::WindWave;
        if (!hasFixedGroundTarget && attackTimer_ > GetCurrentAttackDefinition().warningLeadTime) {
            lockedDirection_ = NormalizePlanar(direction);
            lockedTargetDistance_ = distance;
        }
        UpdateFacing(lockedDirection_);
        UpdateWindup(deltaTime);
        return;
    }
    if (attackState_ == AttackState::Active) {
        UpdateFacing(lockedDirection_);
        UpdateActive(deltaTime);
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
    const engine::Vector3 center = GetTranslate();

    switch (currentAttack_) {
    case AttackKind::PrismSpikes: {
        if (effectTimer_ <= 0.0f) {
            const char* windupPreset = attack.windupVfx.empty() ? kPrismSpikeWarningPreset : attack.windupVfx.c_str();
            EmitPreset(windupPreset, prismSpellCenter_ + engine::Vector3{0.0f, 0.08f, 0.0f});
            const float orbit = idleTimer_ * (2.2f + progress * 2.6f);
            EmitPreset(kPrismSpellMotesPreset,
                       prismSpellCenter_ + engine::Vector3{std::cos(orbit) * 1.65f, 0.12f, std::sin(orbit) * 1.65f});
            EmitPreset(kPrismChargePreset, GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 0.50f, 0.0f});
            effectTimer_ += 0.15f - progress * 0.035f;
        }
        break;
    }
    case AttackKind::CrystalLanceVolley: {
        const float predictedDistance =
            std::clamp(lockedTargetDistance_ + attack.radius, attack.minRange, attack.maxRange);
        ShowAttackTelegraphLaneFan(center, lockedDirection_, predictedDistance, attack.radius * 1.20f,
                                   kCrystalLanceCount, 1.08f, -0.060f, progress, {0.50f, 0.78f, 1.0f, 0.72f});
        if (effectTimer_ <= 0.0f) {
            EmitPreset(kPrismChargePreset, GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 0.66f, 0.0f});
            effectTimer_ += 0.10f - progress * 0.03f;
        }
        break;
    }
    case AttackKind::FireFan: {
        if (effectTimer_ <= 0.0f) {
            const engine::Vector3 corePosition = GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 0.48f, 0.0f};
            EmitDirectedPreset(kFireCastPreset, corePosition, lockedDirection_, 0.72f + progress * 0.55f);
            effectTimer_ += 0.085f;
        }
        break;
    }
    case AttackKind::ThunderChain:
        if (effectTimer_ <= 0.0f) {
            EmitPreset(kThunderChargePreset, GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 0.58f, 0.0f});
            effectTimer_ += 0.065f - progress * 0.025f;
        }
        break;
    case AttackKind::WindWave:
        if (effectTimer_ <= 0.0f) {
            EmitDirectedPreset(kWindChargePreset, GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 0.42f, 0.0f},
                               lockedDirection_ * -1.0f + engine::Vector3{0.0f, 0.16f, 0.0f}, 0.85f + progress * 0.70f);
            effectTimer_ += 0.07f;
        }
        break;
    case AttackKind::SlimeSummon:
        if (effectTimer_ <= 0.0f) {
            EmitPreset(kPrismChargePreset, GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 0.52f, 0.0f});
            effectTimer_ += 0.12f - progress * 0.035f;
        }
        break;
    default:
        break;
    }

    if (!warningTriggered_ && attackTimer_ <= attack.warningLeadTime) {
        engine::Vector4 cueColor = {0.52f, 0.96f, 1.0f, 1.0f};
        if (currentAttack_ == AttackKind::FireFan)
            cueColor = {1.0f, 0.28f, 0.05f, 1.0f};
        if (currentAttack_ == AttackKind::ThunderChain)
            cueColor = {1.0f, 0.92f, 0.12f, 1.0f};
        if (currentAttack_ == AttackKind::WindWave)
            cueColor = {0.58f, 1.0f, 0.86f, 1.0f};
        if (currentAttack_ == AttackKind::SlimeSummon)
            cueColor = {0.96f, 0.58f, 1.0f, 1.0f};
        if (currentAttack_ == AttackKind::CrystalLanceVolley)
            cueColor = {0.58f, 0.82f, 1.0f, 1.0f};
        TriggerAttackTelegraphCue(cueColor);
        warningTriggered_ = true;
    }

    if (attackTimer_ <= 0.0f) {
        attackState_ = AttackState::Active;
        attackStateDuration_ = (std::max)(0.08f, attack.activeDuration);
        if (currentAttack_ == AttackKind::FireFan && attack.sequence.enabled) {
            // この接続先では詠唱完了を時刻0とします。予備動作の長さを変えても発射間隔は変わりません。
            attackStateDuration_ = (std::max)(attackStateDuration_, attack.sequence.duration);
            attackSequence_.Start(attack, MakeSequenceContext());
        }
        attackTimer_ = attackStateDuration_;
        effectTimer_ = 0.0f;
        actionTimer_ = 0.0f;
        actionIndex_ = 0;
        if (currentAttack_ == AttackKind::WindWave) {
            windTornadoAge_ = 0.0f;
            for (std::size_t index = 0; index < windVortices_.size(); ++index) {
                WindVortexVisual& vortex = windVortices_[index];
                vortex.position = vortex.origin;
                vortex.driftTimer = 0.0f;
                EmitPreset(kWindImpactPreset, vortex.origin + engine::Vector3{0.0f, 0.12f, 0.0f});
                if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
                    const float radius = attack.radius * (index == 0 ? 1.0f : 0.72f);
                    effects->SpawnEffectAt(kWindRingEffect, vortex.origin + engine::Vector3{0.0f, 0.06f, 0.0f},
                                           {0.0f, 0.0f, 0.0f}, {radius, 1.0f, radius});
                }
            }
        }
        HideAttackTelegraph();
    }
}

void EnemyPrismSlime::UpdateActive(float deltaTime) {
    // 技ごとの発生タイミングを進め、実行時間が尽きたら共通の回復状態へ移す。
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
        if (attack.sequence.enabled) {
            attackSequence_.Update(deltaTime, MakeSequenceContext());
            break;
        }
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
        break;
    case AttackKind::WindWave:
        windTornadoAge_ += deltaTime;
        for (std::size_t index = 0; index < windVortices_.size(); ++index) {
            WindVortexVisual& vortex = windVortices_[index];
            if (windTornadoAge_ <= 0.16f + static_cast<float>(index) * 0.15f ||
                windTornadoAge_ >= attack.activeDuration - 0.18f)
                continue;
            vortex.driftTimer -= deltaTime;
            if (vortex.driftTimer <= 0.0f) {
                const float angle = NextSummonRandom01(windRandomState_) * kPi * 2.0f;
                vortex.direction = {std::sin(angle), 0.0f, std::cos(angle)};
                vortex.driftTimer += 0.26f;
            }
            engine::Vector3 candidate = vortex.position + vortex.direction * (deltaTime * 2.3f);
            engine::Vector3 fromOrigin = candidate - vortex.origin;
            fromOrigin.y = 0.0f;
            const float drift = engine::Math::Length(fromOrigin);
            if (drift > 1.35f)
                candidate = vortex.origin + fromOrigin * (1.35f / drift);
            vortex.position = FindGroundPoint(candidate);
        }
        if (effectTimer_ <= 0.0f) {
            for (std::size_t index = 0; index < windVortices_.size(); ++index) {
                if (windTornadoAge_ < 0.16f + static_cast<float>(index) * 0.15f)
                    continue;
                const float orbit = windTornadoAge_ * 12.0f + static_cast<float>(index) * 2.1f;
                const WindVortexVisual& vortex = windVortices_[index];
                const float radius = index == 0 ? 1.6f : 1.1f;
                const engine::Vector3 origin =
                    vortex.position + engine::Vector3{std::cos(orbit) * radius, 0.35f + std::fmod(windTornadoAge_ * 3.2f, 3.8f),
                                              std::sin(orbit) * radius};
                EmitDirectedPreset(kWindStreamPreset, origin, engine::Vector3{-std::sin(orbit), 0.75f, std::cos(orbit)},
                                   index == 0 ? 1.15f : 0.92f);
                EmitPreset(kWindDustPreset, vortex.position + engine::Vector3{0.0f, 0.12f, 0.0f});
            }
            effectTimer_ += 0.09f;
        }
        if (actionTimer_ <= 0.0f && windTornadoAge_ >= 0.16f) {
            ApplyWindTornado();
            actionTimer_ = kWindPushInterval;
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
    // 技定義の硬直・再使用待ちを設定し、攻撃判定の予告を隠す。
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackState_ = AttackState::Recovery;
    attackStateDuration_ = (std::max)(0.08f, attack.recoveryDuration);
    attackTimer_ = attackStateDuration_;
    attackCooldown_ = (std::max)(0.2f, attack.cooldown);
    impactPulseTimer_ = 0.26f;
    HideAttackTelegraph();
}

void EnemyPrismSlime::FinishAttack() {
    // 旧技のシーケンスと一時エフェクトを解放してから、次の攻撃位置への移動を始める。
    attackSequence_.Cancel();
    if (currentAttack_ == AttackKind::SlimeSummon) {
        summonPortalVisuals_.clear();
    }
    crystalCircle_.reset();
    crystalCircleGlow_.reset();
    prismSpikeSector_.reset();
    for (auto& sigil : prismSpikeSigil_)
        sigil.reset();
    for (WindVortexVisual& vortex : windVortices_) {
        vortex.circle.reset();
        vortex.circleGlow.reset();
        for (auto& effect : vortex.outer)
            effect.reset();
        for (auto& effect : vortex.inner)
            effect.reset();
    }
    chantVisual_.reset();
    currentAttack_ = AttackKind::None;
    attackTimer_ = 0.0f;
    actionTimer_ = 0.0f;
    actionIndex_ = 0;
    warningTriggered_ = false;
    damageApplied_ = false;
    BeginReposition();
}

void EnemyPrismSlime::BeginReposition() {
    // 攻撃後の移動先はアリーナ内に制限し、近すぎる抽選結果なら中心方向へ取り直す。
    attackState_ = AttackState::Reposition;

    const float angle = NextSummonRandom01(repositionRandomState_) * kPi * 2.0f;
    const float moveDistance =
        engine::Math::Lerp(kRepositionMinimumDistance, kRepositionMaximumDistance, NextSummonRandom01(repositionRandomState_));
    const engine::Vector3 current = GetTranslate();
    const engine::Vector3 arenaCenter = IsEncounterControlled() ? encounterBasePosition_ : current;
    engine::Vector3 candidate = {
        current.x + std::sin(angle) * moveDistance,
        current.y,
        current.z + std::cos(angle) * moveDistance,
    };
    candidate.x = std::clamp(candidate.x, arenaCenter.x - kRepositionArenaHalfExtentX,
                             arenaCenter.x + kRepositionArenaHalfExtentX);
    candidate.z = std::clamp(candidate.z, arenaCenter.z - kRepositionArenaHalfExtentZ,
                             arenaCenter.z + kRepositionArenaHalfExtentZ);

    engine::Vector3 displacement = candidate - current;
    displacement.y = 0.0f;
    float actualDistance = std::sqrt(displacement.x * displacement.x + displacement.z * displacement.z);
    if (actualDistance < 2.0f) {
        displacement = arenaCenter - current;
        displacement.y = 0.0f;
        const float centerDistance = std::sqrt(displacement.x * displacement.x + displacement.z * displacement.z);
        const engine::Vector3 fallbackDirection =
            centerDistance > 0.001f ? displacement * (1.0f / centerDistance) : engine::Vector3{1.0f, 0.0f, 0.0f};
        candidate = current + fallbackDirection * kRepositionMinimumDistance;
        candidate.x = std::clamp(candidate.x, arenaCenter.x - kRepositionArenaHalfExtentX,
                                 arenaCenter.x + kRepositionArenaHalfExtentX);
        candidate.z = std::clamp(candidate.z, arenaCenter.z - kRepositionArenaHalfExtentZ,
                                 arenaCenter.z + kRepositionArenaHalfExtentZ);
        displacement = candidate - current;
        displacement.y = 0.0f;
        actualDistance = std::sqrt(displacement.x * displacement.x + displacement.z * displacement.z);
    }

    repositionDestination_ = candidate;
    repositionDuration_ = std::clamp(actualDistance / kRepositionMoveSpeed, 0.72f, 1.38f);
    repositionTimer_ = repositionDuration_;
    attackStateDuration_ = repositionDuration_;
    repositionJumpPending_ = true;
}

void EnemyPrismSlime::UpdateReposition(float deltaTime, engine::Vector3& velocity) {
    // 再配置は最初だけ跳躍し、目標付近か制限時間で移動を終える。
    repositionTimer_ = (std::max)(0.0f, repositionTimer_ - deltaTime);
    engine::Vector3 toDestination = repositionDestination_ - GetTranslate();
    toDestination.y = 0.0f;
    const float distance = std::sqrt(toDestination.x * toDestination.x + toDestination.z * toDestination.z);
    if (repositionTimer_ <= 0.0f || distance <= 0.35f) {
        FinishReposition(velocity);
        return;
    }

    const engine::Vector3 moveDirection = toDestination * (1.0f / (std::max)(distance, 0.001f));
    const float progress = 1.0f - std::clamp(repositionTimer_ / (std::max)(repositionDuration_, 0.001f), 0.0f, 1.0f);
    const float stride = 0.82f + std::sin(progress * kPi) * 0.18f;
    velocity.x = moveDirection.x * kRepositionMoveSpeed * stride;
    velocity.z = moveDirection.z * kRepositionMoveSpeed * stride;
    if (repositionJumpPending_) {
        velocity.y = (std::max)(velocity.y, kRepositionJumpSpeed);
        SetGrounded(false);
        repositionJumpPending_ = false;
    }
    UpdateFacing(moveDirection);
}

void EnemyPrismSlime::FinishReposition(engine::Vector3& velocity) {
    // 移動の慣性を残さず待機へ戻し、次の技の射程調整へ渡す。
    attackState_ = AttackState::Idle;
    repositionTimer_ = 0.0f;
    repositionDuration_ = 0.0f;
    repositionJumpPending_ = false;
    velocity.x = 0.0f;
    velocity.z = 0.0f;
}

void EnemyPrismSlime::PrepareAttackCycle() {
    // 現在属性の技・晶槍・残回数がある召喚を混ぜ、直前の技が先頭に続かないようにする。
    AttackKind elementalAttack = AttackKind::PrismSpikes;
    switch (elementPhase_) {
    case ElementPhase::Fire:
        elementalAttack = AttackKind::FireFan;
        break;
    case ElementPhase::Thunder:
        elementalAttack = AttackKind::ThunderChain;
        break;
    case ElementPhase::Wind:
        elementalAttack = AttackKind::WindWave;
        break;
    default:
        break;
    }

    attackCycle_[0] = elementalAttack;
    attackCycle_[1] = AttackKind::CrystalLanceVolley;
    attackCycleCount_ = 2;
    if (summonWavesUsed_ < kMaxSummonWaves) {
        attackCycle_[attackCycleCount_++] = AttackKind::SlimeSummon;
    }
    std::shuffle(attackCycle_.begin(), attackCycle_.begin() + attackCycleCount_, attackRandom_);
    if (attackCycleCount_ > 1 && attackCycle_[0] == lastAutomaticAttack_) {
        std::swap(attackCycle_[0], attackCycle_[1]);
    }
    attackCycleIndex_ = 0;
    attackCyclePhase_ = elementPhase_;
}

EnemyPrismSlime::AttackKind EnemyPrismSlime::ResolveAutomaticAttack(float targetDistance) {
    // 周期が切れたら組み直し、現在距離の射程に合う技を先へ繰り上げる。
    if (attackCycleCount_ == 0 || attackCycleIndex_ >= attackCycleCount_ || attackCyclePhase_ != elementPhase_) {
        PrepareAttackCycle();
    }
    for (std::size_t index = attackCycleIndex_; index < attackCycleCount_; ++index) {
        if (attackCycleIndex_ == 0 && attackCycle_[index] == lastAutomaticAttack_) {
            continue;
        }
        const EnemyAttackDefinition& attack = attackCycle_[index] == AttackKind::FireFan
            ? SelectFireFanDefinition(targetDistance) : GetAttackDefinition(GetAttackId(attackCycle_[index]));
        if (targetDistance >= attack.minRange && targetDistance <= attack.maxRange) {
            std::swap(attackCycle_[attackCycleIndex_], attackCycle_[index]);
            break;
        }
    }
    return attackCycle_[attackCycleIndex_];
}

EnemyPrismSlime::AttackKind EnemyPrismSlime::ResolveDebugAttack() const {
    // 固定技IDとデータ定義シーケンスの両方をプレビュー可能な攻撃種別へ対応付ける。
    if (debugPreviewAttackId_ == kCrystalSpikesAttackId)
        return AttackKind::PrismSpikes;
    if (debugPreviewAttackId_ == kCrystalLanceVolleyAttackId)
        return AttackKind::CrystalLanceVolley;
    if (debugPreviewAttackId_ == kFireFanAttackId)
        return AttackKind::FireFan;
    if (debugPreviewAttackId_ == kThunderChainAttackId)
        return AttackKind::ThunderChain;
    if (debugPreviewAttackId_ == kWindWaveAttackId)
        return AttackKind::WindWave;
    if (debugPreviewAttackId_ == kSlimeSummonAttackId)
        return AttackKind::SlimeSummon;
    if (const auto* selected = attackProfile_.FindAttack(debugPreviewAttackId_);
        selected && selected->sequence.binding == "enemy" && (!selected->sequence.events.empty() || !selected->sequence.tracks.empty()))
        return AttackKind::FireFan;
    return AttackKind::None;
}

const EnemyAttackDefinition& EnemyPrismSlime::GetCurrentAttackDefinition() const {
    // 炎の技は射程で選択済みの定義を返し、他は固定IDの定義を使う。
    if (currentAttack_ == AttackKind::FireFan) return selectedFireFan_;
    return GetAttackDefinition(GetAttackId(currentAttack_));
}

const EnemyAttackDefinition& EnemyPrismSlime::SelectFireFanDefinition(float distance) const {
    // プレビュー指定を優先し、通常時は距離に合うシーケンス定義を選ぶ。
    if (const auto* selected = attackProfile_.FindAttack(debugPreviewAttackId_);
        selected && selected->sequence.binding == "enemy" &&
        (selected->id == kFireFanAttackId || (!selected->sequence.events.empty() || !selected->sequence.tracks.empty()))) return *selected;
    const auto* selected = attackProfile_.SelectSequence(distance, 100000.0f);
    return selected ? *selected : GetAttackDefinition(kFireFanAttackId);
}

AttackSequenceContext EnemyPrismSlime::MakeSequenceContext() {
    AttackSequenceContext context;
    context.owner = this;
    context.position = GetWorldPosition();
    context.forward = lockedDirection_;
    // 従来の発射口は前方に横幅の0.48倍、上方に高さの0.48倍です。
    context.ownerSize = {baseScale_.x, baseScale_.y, baseScale_.x};
    context.hasTarget = target_ != nullptr;
    if (target_) context.targetPosition = target_->GetWorldPosition();
    context.targetDistance = lockedTargetDistance_;
    context.grounded = isGrounded_;
    return context;
}

const char* EnemyPrismSlime::GetSequenceStateLabel() const {
    // データ定義の炎攻撃を実行中だけ、内部シーケンスの段階名を表示する。
    return currentAttack_ == AttackKind::FireFan && selectedFireFan_.sequence.enabled && attackState_ == AttackState::Active
        ? attackSequence_.GetStateLabel() : nullptr;
}

const char* EnemyPrismSlime::GetAttackId(AttackKind kind) const {
    // 内部の攻撃種別をJSON側の攻撃IDへ変換し、未指定なら通常の棘を使う。
    switch (kind) {
    case AttackKind::CrystalLanceVolley:
        return kCrystalLanceVolleyAttackId;
    case AttackKind::FireFan:
        return kFireFanAttackId;
    case AttackKind::ThunderChain:
        return kThunderChainAttackId;
    case AttackKind::WindWave:
        return kWindWaveAttackId;
    case AttackKind::SlimeSummon:
        return kSlimeSummonAttackId;
    default:
        return kCrystalSpikesAttackId;
    }
}

engine::Vector3 EnemyPrismSlime::FindGroundPoint(const engine::Vector3& samplePosition) const {
    // 予告や着弾を空中に置かないよう、周囲の地形面を探す。
    return CombatGroundQuery::FindGroundPoint(samplePosition, this, {11.0f, 24.0f}, GetTranslate().y);
}

void EnemyPrismSlime::InitializeFaceParts() {
    // コアと目は本体の子にし、攻撃判定を持たない見た目専用部品として作る。
    coreFramePart_ = CreatePart("PrismSlime_CoreFrame", "Characters/prism_slime_frame", {0.0f, 0.51f, 0.505f},
                                {0.19f, 0.17f, 0.17f}, {0.58f, 0.34f, 0.90f, 1.0f}, 1.55f);
    corePart_ = CreatePart("PrismSlime_Core", "Characters/prism_slime_core", {0.0f, 0.51f, 0.518f},
                           {0.17f, 0.16f, 0.12f}, {0.94f, 0.42f, 0.88f, 1.0f}, 2.15f);
    eyeParts_[0] = CreatePart("PrismSlime_EyeLeft", "Primitives/sphere", {-0.19f, 0.555f, 0.442f},
                              {0.055f, 0.078f, 0.040f}, {0.07f, 0.20f, 0.39f, 1.0f}, 1.12f);
    eyeParts_[1] = CreatePart("PrismSlime_EyeRight", "Primitives/sphere", {0.19f, 0.555f, 0.442f},
                              {0.055f, 0.078f, 0.040f}, {0.07f, 0.20f, 0.39f, 1.0f}, 1.12f);

    for (engine::Object3d* prismPart : {coreFramePart_.get(), corePart_.get()}) {
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

std::unique_ptr<engine::Object3d> EnemyPrismSlime::CreatePart(const std::string& name, const std::string& modelName,
                                                      const engine::Vector3& localPosition, const engine::Vector3& localScale,
                                                      const engine::Vector4& color, float emissive) {
    // 本体に追従する子オブジェクトを作り、衝突対象からは除外する。
    if (!common_) {
        return nullptr;
    }
    auto part = std::make_unique<engine::Object3d>();
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
    // コアの脈動と詠唱時の目の細まりを本体状態に合わせ、可視性も同期する。
    const bool visible = GetIsVisible();
    const float pulse = 1.0f + std::sin(idleTimer_ * 4.2f) * 0.055f +
                        (impactPulseTimer_ > 0.0f ? std::sin(impactPulseTimer_ * 46.0f) * 0.10f : 0.0f);

    if (corePart_) {
        corePart_->SetIsVisible(visible);
        corePart_->SetScale({0.17f * pulse, 0.16f * pulse, 0.12f * pulse});
        corePart_->SetRotation({idleTimer_ * 0.28f, idleTimer_ * 0.44f, idleTimer_ * 0.62f});
        corePart_->Update(deltaTime);
    }
    if (coreFramePart_) {
        coreFramePart_->SetIsVisible(visible);
        const float framePulse = 1.0f + (pulse - 1.0f) * 0.62f;
        coreFramePart_->SetScale({0.19f * framePulse, 0.17f * framePulse, 0.17f});
        coreFramePart_->SetRotation({0.0f, 0.0f, std::sin(idleTimer_ * 2.1f) * 0.055f});
        coreFramePart_->Update(deltaTime);
    }

    const float eyeSquash =
        attackState_ == AttackState::Windup
            ? 1.0f - SmoothStep01(1.0f - attackTimer_ / (std::max)(0.01f, attackStateDuration_)) * 0.22f
            : 1.0f;
    for (auto& eye : eyeParts_) {
        if (!eye) {
            continue;
        }
        eye->SetIsVisible(visible);
        eye->SetScale({0.055f, 0.078f * eyeSquash, 0.040f});
        eye->Update(deltaTime);
    }
}

void EnemyPrismSlime::UpdatePhaseAppearance() {
    // 属性フェーズの変化をコアと外枠の色で見分けられるようにする。
    engine::Vector4 coreColor = {0.94f, 0.42f, 0.88f, 1.0f};
    engine::Vector4 frameColor = {0.58f, 0.34f, 0.90f, 1.0f};
    switch (elementPhase_) {
    case ElementPhase::Fire:
        coreColor = {1.0f, 0.32f, 0.08f, 1.0f};
        frameColor = {1.0f, 0.68f, 0.18f, 1.0f};
        break;
    case ElementPhase::Thunder:
        coreColor = {1.0f, 0.94f, 0.16f, 1.0f};
        frameColor = {0.72f, 0.94f, 1.0f, 1.0f};
        break;
    case ElementPhase::Wind:
        coreColor = {0.35f, 1.0f, 0.72f, 1.0f};
        frameColor = {0.66f, 1.0f, 0.88f, 1.0f};
        break;
    default:
        break;
    }
    if (corePart_)
        corePart_->SetColor(coreColor);
    if (coreFramePart_)
        coreFramePart_->SetColor(frameColor);
}

void EnemyPrismSlime::ApplySlimeAnimation(float deltaTime) {
    SlimeBounceAnimator::Params params;
    params.speedForFullBounce = 3.2f;
    params.idleAmplitude = 0.035f;
    params.moveAmplitude = 0.16f;
    params.hopFrequency = 4.4f;
    params.horizontalSquash = 0.14f;
    params.verticalStretch = 0.20f;
    params.airborneStretch = 0.29f;

    engine::Vector3 targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    engine::Vector3 targetRotation = {0.0f, GetRotation().y, 0.0f};

    if (attackState_ == AttackState::Windup) {
        const float progress = SmoothStep01(1.0f - attackTimer_ / (std::max)(0.01f, attackStateDuration_));
        const float tremble = std::sin(progress * kPi * 12.0f) * 0.018f * progress;
        if (currentAttack_ == AttackKind::CrystalLanceVolley) {
            // 晶槍を組み上げる間は縮こまらず、詠唱で上へ伸びるシルエットにします。
            targetScale.x = baseScale_.x * (1.0f - progress * 0.08f + tremble * 0.4f);
            targetScale.y = baseScale_.y * (1.0f + progress * 0.16f - std::abs(tremble) * 0.3f);
            targetScale.z = baseScale_.z * (1.0f - progress * 0.06f - tremble * 0.3f);
            targetRotation.z = std::sin(idleTimer_ * 4.2f) * 0.025f * progress;
        } else {
            targetScale.x = baseScale_.x * (1.0f + progress * 0.16f + tremble);
            targetScale.y = baseScale_.y * (1.0f - progress * 0.18f - std::abs(tremble));
            targetScale.z = baseScale_.z * (1.0f + progress * 0.10f - tremble);
            targetRotation.z = tremble * 1.4f;
        }
    } else if (attackState_ == AttackState::Active) {
        const float progress = 1.0f - attackTimer_ / (std::max)(0.01f, attackStateDuration_);
        const float pulse = std::sin(progress * kPi * 5.0f);
        if (currentAttack_ == AttackKind::PrismSpikes) {
            targetScale = {baseScale_.x * (1.28f + pulse * 0.06f), baseScale_.y * 0.74f,
                           baseScale_.z * (1.28f - pulse * 0.04f)};
        } else if (currentAttack_ == AttackKind::CrystalLanceVolley) {
            const float recoil = std::sin(progress * kPi * static_cast<float>(kCrystalLanceCount));
            targetScale = {
                baseScale_.x * (1.12f + std::abs(recoil) * 0.055f),
                baseScale_.y * (0.84f - recoil * 0.025f),
                baseScale_.z * (1.18f + recoil * 0.045f),
            };
            targetRotation.x = -0.075f + recoil * 0.025f;
        } else if (currentAttack_ == AttackKind::FireFan) {
            targetScale = {baseScale_.x * (1.08f + std::abs(pulse) * 0.05f), baseScale_.y * 0.88f,
                           baseScale_.z * (1.22f - pulse * 0.06f)};
            targetRotation.x = -0.08f + pulse * 0.025f;
        } else if (currentAttack_ == AttackKind::ThunderChain) {
            const float jitter = std::sin(progress * kPi * 18.0f) * 0.045f;
            targetScale = {baseScale_.x * (1.14f + jitter), baseScale_.y * (0.82f - std::abs(jitter) * 0.4f),
                           baseScale_.z * (1.14f - jitter)};
            targetRotation.z = jitter * 1.5f;
        } else if (currentAttack_ == AttackKind::WindWave) {
            targetScale = {baseScale_.x * (1.06f + pulse * 0.04f), baseScale_.y * 0.86f,
                           baseScale_.z * (1.30f - pulse * 0.05f)};
            targetRotation.x = -0.10f;
        }
    } else if (attackState_ == AttackState::Reposition) {
        const float progress =
            1.0f - std::clamp(repositionTimer_ / (std::max)(repositionDuration_, 0.001f), 0.0f, 1.0f);
        const float hop = std::sin(progress * kPi);
        const float landingSquash = std::pow((std::max)(0.0f, (progress - 0.78f) / 0.22f), 2.0f);
        targetScale = {
            baseScale_.x * (1.0f - hop * 0.10f + landingSquash * 0.13f),
            baseScale_.y * (1.0f + hop * 0.20f - landingSquash * 0.18f),
            baseScale_.z * (1.0f - hop * 0.07f + landingSquash * 0.10f),
        };
        targetRotation.x = -0.055f * hop;
        targetRotation.z = std::sin(progress * kPi * 2.0f) * 0.025f;
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

    if (attackState_ == AttackState::Idle) {
        SlimeBounceAnimator::ApplyTravelPose(targetScale, targetRotation, baseScale_, GetVelocity(), GetRotation().y,
                                             4.0f, 0.055f, 0.075f);
        SlimeBounceAnimator::ApplyGroundHopPose(targetScale, baseScale_, isGrounded_, locomotionHopTimer_,
                                                kLocomotionHopInterval, kLocomotionAnticipationDuration,
                                                locomotionLandingTimer_, kLocomotionLandingDuration);
    }
    ApplyDamageReactionPose(targetScale, targetRotation);
    const float rate = 1.0f - std::exp(-deltaTime * 11.0f);
    const engine::Vector3 currentScale = GetScale();
    SetScale({
        engine::Math::Lerp(currentScale.x, targetScale.x, rate),
        engine::Math::Lerp(currentScale.y, targetScale.y, rate),
        engine::Math::Lerp(currentScale.z, targetScale.z, rate),
    });
    const engine::Vector3 currentRotation = GetRotation();
    SetRotation({
        engine::Math::Lerp(currentRotation.x, targetRotation.x, rate),
        currentRotation.y,
        engine::Math::Lerp(currentRotation.z, targetRotation.z, rate),
    });
}

void EnemyPrismSlime::UpdateFacing(const engine::Vector3& direction) {
    // 水平成分があるときだけ向きを補間し、停止時の姿勢を維持する。
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z);
    SetRotationY(engine::Math::LerpShortAngle(GetRotation().y, targetYaw, 0.10f));
}

void EnemyPrismSlime::EmitPreset(const char* presetName, const engine::Vector3& position) {
    // 名称で指定された粒子プリセットを指定位置へ発生させる。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyPrismSlime::EmitDirectedPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                                         float speedScale) {
    // 指向性のあるプリセットには発射方向と速度倍率を渡す。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyPrismSlime::SyncCollisionRadius() {
    // スライムの伸縮は見た目用です。物理形状まで伸縮すると接地点が上下して床へ沈むため、
    // コライダーは配置時の基準スケールへ固定します。
    const engine::Vector3 collisionScale = hasBaseScale_ ? baseScale_ : GetScale();
    if (Collider* collider = GetCollider()) {
        collider->SetScaleOverride(collisionScale);
    }
    const float maximumScale = (std::max)({
        0.001f,
        std::abs(collisionScale.x),
        std::abs(collisionScale.y),
        std::abs(collisionScale.z),
    });
    SetCollisionRadius(kGroundCollisionWorldRadius / maximumScale);
}
