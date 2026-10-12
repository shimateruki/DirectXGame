#define NOMINMAX
#include "game/actor/combat/CombatGroundQuery.h"
#include "game/actor/combat/CombatMath.h"
#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.Internal.h"
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

using namespace EnemyMagmaSlimeImplementation;

// マグマボスの攻撃状態を管理します。詠唱、攻撃、回復の段階で速度と当たり判定を切り替えます。

EnemyMagmaSlime::EnemyMagmaSlime() = default;
EnemyMagmaSlime::~EnemyMagmaSlime() = default;

void EnemyMagmaSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_MagmaSlime");
    SetEnemyType("MagmaSlime");
    ReloadAttackProfile();

    // 本体はOBJ内の溶岩・黒曜石・発光亀裂PBR材質をそのまま使います。
    SetMaterialType(0);
    SetColor({1.0f, 0.94f, 0.86f, 1.0f});
    SetEmissive(1.18f);
    SetMetallic(0.12f);
    SetRoughness(0.26f);
    SetEnableEnvMap(true);
    SetEnvIntensity(0.42f);
    defaultColor_ = GetColor();

    SetCollisionAttribute(CollisionCategories::Get("enemy"));
    SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
    SetColliderType(ColliderType::kSphere);
    SyncCollisionRadius();
}

void EnemyMagmaSlime::Update(float deltaTime) {
    // 設置済みのマグマ演出を先に更新し、遭遇・休眠・敗北中は新しい攻撃を始めない。
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
        const Vector3 candidate = GetTranslate() + Vector3{velocity.x * 0.42f, 0.0f, velocity.z * 0.42f};
        if (isGrounded_ && !CombatGroundQuery::CanWalkTo(GetTranslate(), candidate, this, {4.0f, 12.0f}, 1.2f)) {
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
                    Vector3{0.0f, 0.04f, 0.0f},
                {1.0f, 0.52f, 0.18f, 0.78f}, 2.6f, true);
        }
        if (SlimeBounceAnimator::StepBossGroundHop(locomotionHopTimer_, velocity, locomotionIntentSpeed,
                                                   isGrounded_ && param_.has_value(), deltaTime, kLocomotionHopInterval,
                                                   kLocomotionAnticipationDuration, 0.22f)) {
            velocity.y = (std::max)(velocity.y, std::clamp(param_->jumpPower * kLocomotionHopPowerScale, 9.0f, 15.0f));
            locomotionAirborne_ = true;
            BossGroundHopVfx::EmitPuff(
                CombatGroundQuery::FindGroundPoint(GetTranslate(), this, {4.0f, 12.0f}, GetTranslate().y) +
                    Vector3{0.0f, 0.04f, 0.0f},
                {1.0f, 0.52f, 0.18f, 0.62f}, 2.3f, false);
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

void EnemyMagmaSlime::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // シーン管理外の詠唱陣と火柱の補助表示も本体と同じ描画順で出す。
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
    // 遭遇制御下のスイッチは出現と休眠を切り替え、それ以外は通常の敵の動作に任せる。
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
    // 複製先へ配置倍率と標的を渡し、攻撃中の一時状態は持ち越さない。
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
    // エディターで調整した倍率をアニメーションの基準にし、当たり判定も同期する。
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
    SyncCollisionRadius();
}

bool EnemyMagmaSlime::HasOwnedSpecialMaterialVisuals() const {
    // 特殊マテリアルの別パスに送る一時エフェクトがあるときだけ描画処理を要求する。
    if (!GetIsVisible()) {
        return false;
    }
    return !magmaBlobs_.empty() || !magmaPools_.empty() || !magmaRings_.empty() || !magmaPillars_.empty() ||
           !magmaSurges_.empty() || !magmaFloodCells_.empty() || rushFlameVisuals_[0] != nullptr ||
           fireColumnVisuals_[0] != nullptr;
}

void EnemyMagmaSlime::DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // マグマ面・衝撃波・炎柱をそれぞれ対応する描画パスで処理する。
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
    // プレビューの技を変える際に、旧技の予告と残存エフェクトを消す。
    debugPreviewAttackId_ = attackId;
    currentAttack_ = AttackKind::None;
    attackState_ = AttackState::Idle;
    attackCooldown_ = 0.05f;
    attackTimer_ = 0.0f;
    HideAttackTelegraph();
    ClearTransientVisuals();
}

const char* EnemyMagmaSlime::GetDebugAttackPhaseName() const {
    // 編集画面には内部状態を予兆・攻撃・硬直・待機の短い表示名で返す。
    switch (attackState_) {
    case AttackState::Windup:
        return "予兆";
    case AttackState::Active:
        return "攻撃";
    case AttackState::Recovery:
        return "硬直";
    default:
        return "待機";
    }
}

void EnemyMagmaSlime::EnsureBaseScale() {
    // 攻撃時の伸縮から復帰できるよう、配置倍率を初回だけ保存する。
    if (hasBaseScale_) {
        return;
    }
    baseScale_ = GetScale();
    hasBaseScale_ = true;
}

// 攻撃中の状態機械を優先し、待機中は選択した技の射程へ移動します。
void EnemyMagmaSlime::UpdateBehavior(float deltaTime, const Vector3& targetDirection, float targetDistance,
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
    const AttackKind plannedAttack =
        debugAttack != AttackKind::None ? debugAttack : ResolveAutomaticAttack(targetDistance);
    const EnemyAttackDefinition& planned = GetAttackDefinition(GetAttackId(plannedAttack));
    const bool inRange = targetDistance >= planned.minRange && targetDistance <= planned.maxRange;
    if ((!locomotionAirborne_ || isGrounded_) && IsBossTargetEngaged() && attackCooldown_ <= 0.0f && inRange) {
        StartAttack(plannedAttack, targetDirection);
        return;
    }

    // 射程外では次の技を待ち続けず、攻撃できる距離まで素早く近づきます。
    const float moveSpeed = targetDistance > planned.maxRange ? (std::max)(5.5f, param_->speed * 3.0f)
                                                              : (std::max)(0.70f, param_->speed * 0.82f);
    const float preferredDistance = (std::clamp)(planned.recommendedTargetDistance, 4.5f, 11.0f);
    const Vector3 spacing =
        CombatMath::MakePacedSpacingVelocity(targetDirection, targetDistance, preferredDistance - 1.2f,
                                             preferredDistance + 1.4f, moveSpeed, 0.48f, 0.36f, idleTimer_, 6.2f);
    velocity.x = spacing.x;
    velocity.z = spacing.z;
}

// 照準と攻撃タイマーを確定し、設置技の予告位置と詠唱演出を準備します。
void EnemyMagmaSlime::StartAttack(AttackKind kind, const Vector3& targetDirection) {
    if (kind == AttackKind::None) {
        return;
    }

    currentAttack_ = kind;
    attackState_ = AttackState::Windup;
    lockedDirection_ = NormalizePlanar(targetDirection);
    lockedTargetPosition_ = target_ ? FindGroundPoint(target_->GetWorldPosition())
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
        mortarPatternPhase_ =
            std::fmod(idleTimer_ * 1.731f + static_cast<float>(automaticAttackSerial_) * 0.917f, kPi * 2.0f);
        PrepareMortarTargets();
    } else if (kind == AttackKind::EruptionField) {
        PrepareEruptionTargets();
    }
    if (kind == AttackKind::FireColumn) {
        PrepareFireColumnTargets();
        CreateFireColumnSigils();
    }
    // 突進とプレス以外の術式には、共通の術者中心の詠唱演出を使います。
    if (kind == AttackKind::MagmaMortar || kind == AttackKind::EruptionField || kind == AttackKind::LavaSpiral ||
        kind == AttackKind::FireColumn || kind == AttackKind::MagmaFlood) {
        BossChantStyle style;
        style.sigilEffectPath = kind == AttackKind::FireColumn ? kFireChantEffect : kMagmaChantEffect;
        style.motePreset = kFireChantMotesPreset;
        style.releasePreset = kind == AttackKind::MagmaFlood ? kFloodSplashPreset : "";
        style.accentColor =
            kind == AttackKind::FireColumn ? Vector4{1.0f, 0.42f, 0.08f, 1.0f} : Vector4{0.95f, 0.20f, 0.035f, 1.0f};
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

void EnemyMagmaSlime::UpdateAttack(float deltaTime, const Vector3& targetDirection, float targetDistance,
                                   Vector3& velocity) {
    // 警告の終盤で照準を固定し、実行・硬直中は各段階の処理だけを進める。
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
    // 技ごとの危険範囲を同じ進捗率で示し、発生直前に警告を一度強める。
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    effectTimer_ -= deltaTime;
    const float progress = 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
    const Vector4 warningColor = {1.0f, 0.19f, 0.015f, 0.84f};

    switch (currentAttack_) {
    case AttackKind::MagmaMortar:
        ShowAttackTelegraphImpactAreas(mortarTargets_.data(), mortarTargets_.size(), attack.radius, progress,
                                       warningColor);
        break;
    case AttackKind::LavaRush:
        ShowAttackTelegraphLine(GetTranslate(), lockedDirection_, attack.maxRange, attack.radius * 2.0f, progress,
                                warningColor);
        break;
    case AttackKind::VolcanicSlam:
        ShowAttackTelegraphCircle(lockedTargetPosition_, attack.radius, progress, warningColor);
        break;
    case AttackKind::EruptionField:
        ShowAttackTelegraphImpactAreas(eruptionTargets_.data(), eruptionTargets_.size(), attack.radius, progress,
                                       warningColor);
        break;
    case AttackKind::LavaSpiral:
        ShowAttackTelegraphCircle(GetTranslate(), attack.maxRange, progress, warningColor);
        break;
    case AttackKind::FireColumn:
        break;
    case AttackKind::MagmaFlood:
        ShowAttackTelegraphLine(GetTranslate(), lockedDirection_, attack.maxRange, attack.radius * 2.0f, progress,
                                warningColor);
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
        EmitDirectedPreset(preset, GetWorldPosition() + Vector3{0.0f, baseScale_.y * 0.55f, 0.0f},
                           {-lockedDirection_.x * 0.18f, 1.0f, -lockedDirection_.z * 0.18f}, 0.82f + progress * 0.72f);
        effectTimer_ += (std::max)(0.055f, 0.14f - progress * 0.075f);
    }

    if (!warningTriggered_ && attackTimer_ <= attack.warningLeadTime) {
        if (currentAttack_ != AttackKind::FireColumn) {
            TriggerAttackTelegraphCue({1.0f, 0.18f, 0.015f, 1.0f});
        }
        warningTriggered_ = true;
    }
    if (attackTimer_ <= 0.0f) {
        BeginActive();
    }
}

void EnemyMagmaSlime::BeginActive() {
    // 技定義の有効時間を始め、種類に応じた設置・突進・プレスの初回処理を起動する。
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

void EnemyMagmaSlime::UpdateActive(float deltaTime, float targetDistance, Vector3& velocity) {
    // 連続発射の間隔と移動技の速度を進め、実行時間が尽きたら硬直へ移す。
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
    // 技定義の硬直と再使用待ちを設定し、火柱の一時描画物を片付ける。
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackState_ = AttackState::Recovery;
    attackStateDuration_ = (std::max)(0.10f, attack.recoveryDuration);
    attackTimer_ = attackStateDuration_;
    attackCooldown_ = (std::max)(0.22f, attack.cooldown);
    landingPulseTimer_ = 0.34f;
    SetGrounded(true);
    HideAttackTelegraph();
    for (auto& flame : fireColumnVisuals_)
        flame.reset();
    for (auto& sigil : fireColumnSigils_)
        sigil.reset();
    for (auto& link : fireColumnLinks_)
        link.reset();
}

void EnemyMagmaSlime::FinishAttack() {
    // 次の技へ前回の警告・着弾フラグを持ち越さないよう、攻撃状態を初期化する。
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
    // 全技の巡回順を混ぜ、直前の技が次巡の先頭に続かないようにする。
    attackCycle_ = {
        AttackKind::MagmaMortar, AttackKind::LavaRush,   AttackKind::VolcanicSlam, AttackKind::EruptionField,
        AttackKind::LavaSpiral,  AttackKind::FireColumn, AttackKind::MagmaFlood,
    };
    std::shuffle(attackCycle_.begin(), attackCycle_.end(), attackRandom_);
    if (attackCycle_[0] == lastAutomaticAttack_) {
        std::swap(attackCycle_[0], attackCycle_[1]);
    }
    attackCycleIndex_ = 0;
}

EnemyMagmaSlime::AttackKind EnemyMagmaSlime::ResolveAutomaticAttack(float targetDistance) {
    // 現在の距離で届く技を巡回順から探し、使用可能な候補を先頭へ繰り上げる。
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
    // JSONの攻撃IDをプレビュー用の内部種別へ対応付ける。
    if (debugPreviewAttackId_ == kMagmaMortarAttackId)
        return AttackKind::MagmaMortar;
    if (debugPreviewAttackId_ == kLavaRushAttackId)
        return AttackKind::LavaRush;
    if (debugPreviewAttackId_ == kVolcanicSlamAttackId)
        return AttackKind::VolcanicSlam;
    if (debugPreviewAttackId_ == kEruptionFieldAttackId)
        return AttackKind::EruptionField;
    if (debugPreviewAttackId_ == kLavaSpiralAttackId)
        return AttackKind::LavaSpiral;
    if (debugPreviewAttackId_ == kFireColumnAttackId)
        return AttackKind::FireColumn;
    if (debugPreviewAttackId_ == kMagmaFloodAttackId)
        return AttackKind::MagmaFlood;
    return AttackKind::None;
}

const char* EnemyMagmaSlime::GetAttackId(AttackKind kind) const {
    // 内部の攻撃種別をプロファイル参照用のIDに変える。
    switch (kind) {
    case AttackKind::MagmaMortar:
        return kMagmaMortarAttackId;
    case AttackKind::LavaRush:
        return kLavaRushAttackId;
    case AttackKind::VolcanicSlam:
        return kVolcanicSlamAttackId;
    case AttackKind::EruptionField:
        return kEruptionFieldAttackId;
    case AttackKind::LavaSpiral:
        return kLavaSpiralAttackId;
    case AttackKind::FireColumn:
        return kFireColumnAttackId;
    case AttackKind::MagmaFlood:
        return kMagmaFloodAttackId;
    default:
        return "";
    }
}

const EnemyAttackDefinition& EnemyMagmaSlime::GetCurrentAttackDefinition() const {
    // 実行中の技が参照する攻撃時間・射程・ダメージの定義を返す。
    return GetAttackDefinition(GetAttackId(currentAttack_));
}

std::unique_ptr<Object3d> EnemyMagmaSlime::CreateMagmaVisual(const std::string& name, const std::string& model,
                                                             int materialType, const Vector4& color,
                                                             float emissive) const {
    // 一時演出は敵本体の衝突対象にせず、種類に応じてマグマ・炎・衝撃波の材質を設定する。
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
    // 予告と着弾が地形から浮かないよう、指定地点の下にある接地面を探す。
    return CombatGroundQuery::FindGroundPoint(samplePosition, this, {12.0f, 28.0f}, GetTranslate().y);
}

void EnemyMagmaSlime::DamageTargetAt(const Vector3& center, float radius, float damage, const Vector3& knockback,
                                     float burnDuration, float burnTickInterval, float burnTickDamage) {
    // 平面半径と高さ差の両方を満たした対象へ、火傷を含む一回の被弾イベントを送る。
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
    event.statusEffect = MakeBurningStatus(burnDuration, burnTickInterval, burnTickDamage, "status_burning_flame");
    EventManager::GetInstance()->Dispatch(event);
}

void EnemyMagmaSlime::ApplySlimeAnimation(float deltaTime) {
    // 移動の弾みと技ごとの変形を合成し、配置時の倍率を基準に戻す。
    SlimeBounceAnimator::Params params;
    params.speedForFullBounce = 2.8f;
    params.idleAmplitude = 0.045f;
    params.moveAmplitude = 0.16f;
    params.hopFrequency = 4.9f;
    params.horizontalSquash = 0.19f;
    params.verticalStretch = 0.24f;
    params.airborneStretch = 0.34f;

    Vector3 targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    Vector3 targetRotation = {0.0f, GetRotation().y, 0.0f};

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
        SlimeBounceAnimator::ApplyTravelPose(targetScale, targetRotation, baseScale_, GetVelocity(),
                                             GetRotation().y - kMagmaSlimeModelYawOffset, 3.4f, 0.065f, 0.045f);
        SlimeBounceAnimator::ApplyGroundHopPose(targetScale, baseScale_, isGrounded_, locomotionHopTimer_,
                                                kLocomotionHopInterval, kLocomotionAnticipationDuration,
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
    // 水平方向が有効なときだけ向きを補間し、停止時の姿勢は保持する。
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z) + kMagmaSlimeModelYawOffset;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.10f));
}

void EnemyMagmaSlime::UpdateBodyHeat(float deltaTime) {
    // 詠唱や着地の段階に応じて本体の熱・発光を変化させる。
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
        EmitDirectedPreset(kCoreEmberPreset, GetWorldPosition() + Vector3{0.0f, baseScale_.y * 0.58f, 0.0f},
                           {0.0f, 1.0f, 0.0f}, attacking ? 1.22f : 0.78f);
        ambientEmberTimer_ += attacking ? 0.085f : 0.19f;
    }
}

void EnemyMagmaSlime::EmitPreset(const char* presetName, const Vector3& position) const {
    // GPU粒子が利用できる場合だけ指定プリセットを発生させる。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyMagmaSlime::EmitDirectedPreset(const char* presetName, const Vector3& position, const Vector3& direction,
                                         float speedScale) const {
    // 発射方向と速度倍率を持つ粒子だけを、GPU粒子管理器が有効な場合に放出する。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyMagmaSlime::SyncCollisionRadius() {
    // 見た目の伸縮を物理判定へ伝播させず、配置時のワールド半径を保つ。
    const Vector3 scale = GetScale();
    const float maximumScale = (std::max)({0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    SetCollisionRadius(kGroundCollisionWorldRadius / maximumScale);
}

void EnemyMagmaSlime::ClearTransientVisuals() {
    // 敗北や技切替の後に設置物・火柱・詠唱陣が残らないよう、一時演出を消す。
    ClearRushFlameVisuals();
    for (auto& flame : fireColumnVisuals_)
        flame.reset();
    for (auto& sigil : fireColumnSigils_)
        sigil.reset();
    for (auto& link : fireColumnLinks_)
        link.reset();
    chantVisual_.reset();
    magmaBlobs_.clear();
    magmaPools_.clear();
    magmaRings_.clear();
    magmaPillars_.clear();
    magmaSurges_.clear();
    magmaFloodCells_.clear();
}
