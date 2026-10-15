#include "EnemyFireSlime.h"
#include "game/actor/enemy/animation/SlimeBounceAnimator.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "HitEffectDirector.h"
#include "MeshEffectManager.h"
#include "SceneManager.h"
#include "VFXSequencer.h"
#include <algorithm>
#include <cmath>

namespace {
// ブレス、火球、頭上炎の発生間隔をまとめる調整値
constexpr const char* kBreathAttackId = "flame_breath";
constexpr const char* kFireballAttackId = "fireball";
constexpr float kGroundCollisionWorldRadius = 0.82f;
constexpr float kThrownCollisionWorldRadius = 1.18f;
constexpr float kMoveHopInterval = 0.28f;
constexpr float kMoveHopPower = 4.7f;
constexpr float kFireSlimeModelYawOffset = 3.1415926535f;
constexpr float kHeadFlameHeight = 0.95f;
constexpr float kHeadFlameBaseScale = 0.78f;
constexpr int kHeadFlameMaterialType = 11;
constexpr float kHeadFlameEffectType = 2.0f;
constexpr float kBreathFlameEffectType = 5.0f;
constexpr int kBreathFlameVisualCount = 7;
constexpr const char* kBreathPreset = "fire_slime_breath";
constexpr const char* kBreathEmberPreset = "fire_slime_breath_embers";
constexpr const char* kCastPreset = "fire_slime_cast";
constexpr const char* kHeadFlamePreset = "fire_slime_head_flame";
constexpr const char* kHeadEmberPreset = "fire_slime_head_embers";

engine::Vector3 NormalizePlanar(engine::Vector3 value) {
    // 目標と重なって方向を失った場合も、攻撃とエフェクトに有効な前方を返す。
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {value.x / length, 0.0f, value.z / length};
}

// 火球の材質、軌跡、着弾エフェクトを指定倍率で構築します。
BulletVisualConfig MakeFireVisual(float scale) {
    // 火球の軌跡と着弾は弾の寿命に紐付け、単発発射でも残像を管理する。
    BulletVisualConfig visual;
    visual.materialType = 11;
    visual.blendMode = BlendMode::kNormal;
    visual.color = {1.0f, 0.34f, 0.07f, 0.96f};
    visual.emissive = 2.8f;
    visual.visualScale = scale;
    visual.effectType = 1.0f;
    visual.effectScale = 1.15f;
    visual.effectSoftness = 0.42f;
    visual.effectIntensity = 0.96f;
    visual.billboardScale = 0.68f;
    visual.trailPreset = "fireball_trail_flame";
    visual.trailSecondaryPreset = "fireball_trail_embers";
    visual.impactPreset = "fireball_impact_flame";
    visual.impactSecondaryPreset = "fireball_impact_sparks";
    visual.trailInterval = 0.030f;
    visual.trailSpeedScale = 1.0f;
    visual.trailReverseDirection = true;
    visual.impactDirected = false;
    return visual;
}

// 攻撃定義の燃焼時間・間隔・VFXを弾へ渡す状態異常設定に変換します。
StatusEffectApplication MakeStatusEffect(const EnemyAttackDefinition& attack) {
    // ダメージとは別に、攻撃 JSON で指定された燃焼の継続条件を弾へ渡す。
    StatusEffectApplication status;
    if (attack.statusEffectType == "burning") {
        status.type = StatusEffectType::Burning;
    }
    status.duration = attack.statusDuration;
    status.tickInterval = attack.statusTickInterval;
    status.tickDamage = attack.statusTickDamage;
    status.vfxPreset = attack.statusVfx;
    return status;
}

engine::Object3d* FindEnemyDamageTarget(engine::Object3d* object) {
    // 子メッシュとの接触も所有する敵本体への命中として扱う。
    for (engine::Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}
} // namespace

// 炎スライムの初期化
void EnemyFireSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_FireSlime");
    SetEnemyType("FireSlime");
    ReloadAttackProfile();
    // 本体のIdle/移動/攻撃姿勢はControllerのBody Clipから再生する。
    // 読み込み失敗時は従来の手続きアニメーションへ戻す。
    SetAnimatorController("enemy_fire_slime");
    SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    defaultColor_ = GetColor();

    SetCollisionAttribute(CollisionCategories::Get("enemy"));
    SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
}

// 近距離ブレスと中距離火球を切り替えるAI
void EnemyFireSlime::Update(float deltaTime) {
    // 無効・投擲中は通常 AI を進めず、解除すべき攻撃シーケンスを先に止める。
    if (UpdateInactiveState(deltaTime)) {
        fireballSequence_.Cancel();
        fireballWindupTimer_ = 0;
        return;
    }

    idleTimer_ += deltaTime;
    if (UpdateThrowRecoveryState(deltaTime)) {
        fireballSequence_.Cancel();
        fireballWindupTimer_ = 0;
        UpdateHeadFlame(deltaTime);
        return;
    }

    EnsureBaseScale();
    UpdateWildTimers(deltaTime);

    engine::Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    UpdateWildBehavior(deltaTime, velocity);
    ApplyGroundMovementAndAnimation(deltaTime, velocity);
    BaseEnemy::Update(deltaTime);
    UpdateHeadFlame(deltaTime);
}

bool EnemyFireSlime::UpdateInactiveState(float deltaTime) {
    // 死亡・運搬・撃破演出では炎と予告の寿命を通常攻撃から切り離す。
    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        breathTimer_ = 0.0f;
        fireballWindupTimer_ = 0.0f;
        attackTimer_ = 0.0f;
        breathWarningTriggered_ = false;
        fireballWarningTriggered_ = false;
        fireballAimLocked_ = false;
        RequestRemoveHeadFlameVisual();
        RequestRemoveBreathFlameVisuals();
        SetVelocity({0.0f, 0.0f, 0.0f});
        return true;
    }

    if (isCarried_) {
        HideAttackTelegraph();
        if (headFlameVisual_)
            headFlameVisual_->SetIsVisible(false);
        HideBreathFlameVisuals();
        idleTimer_ += deltaTime;
        return true;
    }

    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        RequestRemoveHeadFlameVisual();
        RequestRemoveBreathFlameVisuals();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

bool EnemyFireSlime::UpdateThrowRecoveryState(float deltaTime) {
    // 投げられた間は着地前後で衝突半径を切り替え、攻撃 AI を休止する。
    if (IsThrowRecovering()) {
        if (IsThrownPhysics()) {
            SyncThrownCollisionRadius();
        } else {
            SyncGroundCollisionRadius();
        }
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

void EnemyFireSlime::EnsureBaseScale() {
    // 初回に配置スケールを保存し、投擲や攻撃後の復元基準にする。
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyFireSlime::UpdateWildTimers(float deltaTime) {
    // 行動間隔と攻撃中の時間は負値を残さず、次の行動選択へ渡す。
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
}

void EnemyFireSlime::UpdateWildBehavior(float deltaTime, engine::Vector3& velocity) {
    if (!target_ && fireballSequence_.ControlsActor()) {
        // 対象を失っても時計を止めず、条件評価と終了・後片付けまで進めます。
        UpdateFireballWindup(deltaTime, velocity, {0, 0, 1}, 0);
        return;
    }
    if (target_ && param_.has_value()) {
        engine::Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        engine::Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        if (!fireballSequence_.ControlsActor()) UpdateFacing(direction);
        UpdateCombatBehavior(deltaTime, velocity, direction, distance);
    }
}

void EnemyFireSlime::UpdateCombatBehavior(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction,
                                          float distance) {
    // 編集済みシーケンスを優先し、使わない攻撃だけ従来の距離別行動へ戻す。
    const EnemyAttackDefinition& breathAttack = GetAttackDefinition(kBreathAttackId);
    const EnemyAttackDefinition& fireballAttack = GetAttackDefinition(kFireballAttackId);
    const EnemyAttackDefinition* sequenceAttack = SelectSequenceAttack(distance);
    if (breathTimer_ <= 0.0f && fireballWindupTimer_ <= 0.0f && !fireballSequence_.IsRunning() &&
        UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateFacing(direction);
        return;
    }

    if (fireballWindupTimer_ > 0.0f || fireballSequence_.IsRunning()) {
        UpdateFireballWindup(deltaTime, velocity, direction, distance);
    } else if (breathTimer_ > 0.0f) {
        UpdateBreath(deltaTime, direction, distance);
    } else if (sequenceAttack && attackCooldown_ <= 0.0f) {
        selectedFireball_ = *sequenceAttack;
        StartFireballWindup(direction, distance);
    } else if (distance >= breathAttack.minRange && distance <= breathAttack.maxRange && attackCooldown_ <= 0.0f) {
        StartBreath();
    } else if (distance <= (std::min)(detectionRange_, fireballAttack.maxRange) &&
               distance >= fireballAttack.minRange && attackCooldown_ <= 0.0f &&
               !fireballAttack.sequence.enabled && previewAttackId_.empty()) {
        selectedFireball_ = fireballAttack;
        StartFireballWindup(direction, distance);
    } else if (distance <= detectionRange_) {
        const float speed = (std::max)(0.0f, param_->speed);
        const float approach = distance > breathAttack.maxRange * 0.78f ? 1.0f : -0.55f;
        velocity.x = direction.x * speed * approach;
        velocity.z = direction.z * speed * approach;
    } else {
        UpdateWanderBehavior(deltaTime, velocity);
    }
}

void EnemyFireSlime::UpdateWanderBehavior(float deltaTime, engine::Vector3& velocity) {
    // 索敵外は攻撃速度より遅い巡回速度で動き、進行方向へ体を向ける。
    const float speed = (std::max)(0.55f, param_->speed * 0.42f);
    velocity = CalculateWanderVelocity(deltaTime, speed, 0.72f);
    UpdateFacing({velocity.x, 0.0f, velocity.z});
}

void EnemyFireSlime::ApplyGroundMovementAndAnimation(float deltaTime, engine::Vector3& velocity) {
    // 詠唱中の跳ね動作は止め、飛び道具の発射姿勢と移動を競合させない。
    velocity.y = (std::min)(GetVelocity().y, 0.0f);
    if (!fireballSequence_.ControlsActor() && breathTimer_ <= 0.0f && SlimeBounceAnimator::StepGroundHop(groundHopTimer_, velocity, isGrounded_, deltaTime,
                                                                   kMoveHopInterval, 0.10f)) {
        velocity.y = (std::max)(velocity.y, kMoveHopPower);
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    SyncGroundCollisionRadius();
}

void EnemyFireSlime::BeginThrown(const engine::Vector3& initialVelocity) {
    // 運搬から投擲へ移る前に詠唱・予告・炎を片付け、通常サイズへ戻す。
    fireballSequence_.Cancel();
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        const float capturedMaxScale =
            (std::max)({std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z)});
        if (capturedMaxScale < 1.2f) {
            baseScale_ = {2.0f, 2.0f, 2.0f};
        }
        hasBaseScale_ = true;
    }

    breathTimer_ = 0.0f;
    fireballWindupTimer_ = 0.0f;
    breathParticleTimer_ = 0.0f;
    breathEmberTimer_ = 0.0f;
    breathParticleCursor_ = 0;
    attackTimer_ = 0.0f;
    breathDamageDone_ = false;
    breathWarningTriggered_ = false;
    fireballWarningTriggered_ = false;
    fireballAimLocked_ = false;
    HideAttackTelegraph();
    HideBreathFlameVisuals();
    SetColor(defaultColor_);
    SetScale(baseScale_);
    SyncThrownCollisionRadius();
    BaseEnemy::BeginThrown(initialVelocity);
}

std::unique_ptr<engine::Object3d> EnemyFireSlime::Clone() const {
    // シーン配置の複製は初期化後に設定を写し、目標と索敵範囲を引き継ぐ。
    auto clone = std::make_unique<EnemyFireSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

// ブレス攻撃、火球、炎演出の補助処理
void EnemyFireSlime::UpdateFacing(const engine::Vector3& direction) {
    // 停止中の微小ベクトルで向きを跳ねさせず、モデルの前方軸だけ補正する。
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f)
        return;
    const float targetYaw = std::atan2(direction.x, direction.z) + kFireSlimeModelYawOffset;
    SetRotationY(engine::Math::LerpShortAngle(GetRotation().y, targetYaw, 0.16f));
}

void EnemyFireSlime::StartBreath() {
    // ブレス開始時に前の火球選択を消し、予告と継続ダメージの時計を初期化する。
    selectedFireball_ = {};
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    breathTimer_ = attack.activeDuration;
    breathParticleTimer_ = 0.0f;
    breathEmberTimer_ = 0.0f;
    breathParticleCursor_ = 0;
    breathDamageDone_ = false;
    breathWarningTriggered_ = false;
    attackCooldown_ = attack.cooldown;
    attackTimer_ = attack.activeDuration;
    SetColor({1.0f, 0.82f, 0.62f, 1.0f});
}

void EnemyFireSlime::UpdateBreath(float deltaTime, const engine::Vector3& direction, float distance) {
    // 攻撃判定より先に扇形の予告を見せ、判定発生後は予告を消す。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    const float activeDuration = (std::max)(0.01f, attack.activeDuration);
    breathTimer_ = (std::max)(0.0f, breathTimer_ - deltaTime);
    breathParticleTimer_ -= deltaTime;
    breathEmberTimer_ -= deltaTime;
    const float progress = attack.GetPhaseProgressFromRemaining(AttackAbilityPhase::Active, breathTimer_);
    const float damageTriggerTime = activeDuration * 0.62f;
    const float damageTriggerProgress = 1.0f - damageTriggerTime / activeDuration;
    const float timingProgress = std::clamp(progress / (std::max)(damageTriggerProgress, 0.01f), 0.0f, 1.0f);
    if (!breathDamageDone_) {
        ShowAttackTelegraphCone(GetTranslate(), direction, attack.maxRange + 0.55f, 0.62f, attack.radius * 2.0f,
                                timingProgress, {1.0f, 0.30f, 0.05f, 0.78f});
    } else {
        HideAttackTelegraph();
    }
    UpdateBreathFlameVisuals(direction, deltaTime);

    if (!breathWarningTriggered_ && breathTimer_ <= damageTriggerTime + attack.warningLeadTime) {
        TriggerAttackTelegraphCue({1.0f, 0.28f, 0.04f, 1.0f});
        breathWarningTriggered_ = true;
    }

    if (!breathDamageDone_ && breathTimer_ <= damageTriggerTime) {
        DispatchBreathDamage(direction, distance);
        breathDamageDone_ = true;
        HideAttackTelegraph();
    }

    if (breathParticleTimer_ <= 0.0f) {
        engine::Vector3 origin = GetTranslate();
        origin.y += 0.72f;
        EmitBreathParticles(origin, direction);
        breathParticleTimer_ += 0.065f;
    }

    if (breathTimer_ <= 0.0f) {
        HideAttackTelegraph();
        SetColor(defaultColor_);
        HideBreathFlameVisuals();
    }
}

void EnemyFireSlime::StartFireballWindup(const engine::Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = selectedFireball_;
    if (attack.sequence.enabled && attack.sequence.actorControl.enabled) {
        // 開始に失敗したデータを毎フレーム再試行しません。失敗理由は状態表示に残ります。
        if (!fireballSequence_.Start(attack, MakeSequenceContext(direction, distance)))
            attackCooldown_ = (std::max)(0.25f, attack.cooldown);
        fireballWindupTimer_ = attackTimer_ = 0;
        return;
    }
    fireballWindupTimer_ = (std::max)(0.01f, attack.windupDuration);
    pendingFireballDirection_ = direction;
    pendingFireballDistance_ = distance;
    fireballWarningTriggered_ = false;
    fireballAimLocked_ = false;
    attackTimer_ = fireballWindupTimer_;
    SetColor({1.0f, 0.72f, 0.48f, 1.0f});
    if (attack.sequence.enabled) {
        fireballSequence_.Start(attack, MakeSequenceContext(direction, distance));
        fireballWindupTimer_ = (std::max)(0.0001f, attack.sequence.duration);
    }
}

void EnemyFireSlime::UpdateFireballWindup(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction,
                                          float distance) {
    const EnemyAttackDefinition& attack = selectedFireball_;
    if (attack.sequence.enabled && attack.sequence.actorControl.enabled) {
        fireballSequence_.Update(deltaTime, MakeSequenceContext(direction, distance));
        if (attack.sequence.actorControl.stopMovement) velocity.x = velocity.z = 0;
        if (!fireballSequence_.IsRunning()) attackCooldown_ = attack.cooldown;
        return;
    }
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    if (attack.sequence.enabled) {
        const float elapsed = fireballSequence_.GetElapsed();
        if (!fireballAimLocked_) {
            pendingFireballDirection_ = direction;
            pendingFireballDistance_ = distance;
            UpdateFacing(direction);
        }
        if (elapsed < attack.windupDuration) {
            ShowAttackTelegraphLine(GetWorldPosition(), pendingFireballDirection_,
                std::clamp(pendingFireballDistance_, attack.minRange, attack.maxRange), attack.radius * 2,
                elapsed / (std::max)(0.001f, attack.windupDuration), {1, 0.34f, 0.06f, 0.8f});
            if (!fireballWarningTriggered_ && elapsed + deltaTime >= attack.windupDuration - attack.warningLeadTime) {
                TriggerAttackTelegraphCue({1, 0.32f, 0.04f, 1});
                fireballWarningTriggered_ = fireballAimLocked_ = true;
            }
        } else HideAttackTelegraph();
        fireballSequence_.Update(deltaTime, MakeSequenceContext(pendingFireballDirection_, distance));
        // 既存の溜め姿勢も共通時計へ追従させ、残り時間を二重に積算しません。
        fireballWindupTimer_ = (std::max)(0.0f, attack.windupDuration - fireballSequence_.GetElapsed());
        if (!fireballSequence_.IsRunning()) {
            fireballWindupTimer_ = 0;
            attackCooldown_ = attack.cooldown;
            attackTimer_ = attack.recoveryDuration;
            HideAttackTelegraph();
            SetColor(defaultColor_);
        }
        return;
    }

    if (!fireballAimLocked_) {
        pendingFireballDirection_ = direction;
        pendingFireballDistance_ = distance;
        UpdateFacing(direction);
    }

    fireballWindupTimer_ = (std::max)(0.0f, fireballWindupTimer_ - deltaTime);
    const float windupDuration = (std::max)(0.01f, attack.windupDuration);
    const float progress = attack.GetPhaseProgressFromRemaining(AttackAbilityPhase::Windup, fireballWindupTimer_);
    const float predictedDistance =
        std::clamp(pendingFireballDistance_ + attack.radius, attack.minRange, attack.maxRange);
    ShowAttackTelegraphLine(GetWorldPosition(), pendingFireballDirection_, predictedDistance, attack.radius * 2.0f,
                            progress, {1.0f, 0.34f, 0.06f, 0.80f});
    if (!fireballWarningTriggered_ && fireballWindupTimer_ <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue({1.0f, 0.32f, 0.04f, 1.0f});
        fireballWarningTriggered_ = true;
        fireballAimLocked_ = true;
    }

    if (fireballWindupTimer_ > 0.0f) {
        return;
    }

    FireFireball(pendingFireballDirection_, pendingFireballDistance_);
    HideAttackTelegraph();
    attackCooldown_ = attack.cooldown;
    attackTimer_ = attack.recoveryDuration;
    fireballAimLocked_ = false;
    SetColor(defaultColor_);
}

void EnemyFireSlime::DispatchBreathDamage(const engine::Vector3& direction, float distance) {
    // 扇形に加え、炎メッシュの高さと遮蔽物を確認してから命中を通知する。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    if (!target_ || distance > attack.maxRange + 0.55f) {
        return;
    }

    engine::Vector3 toTarget = NormalizePlanar(target_->GetTranslate() - GetTranslate());
    const float facingDot = direction.x * toTarget.x + direction.z * toTarget.z;
    if (facingDot < 0.34f) {
        return;
    }

    const float bodyHeight = (std::max)(0.7f, std::abs(GetScale().y));
    const float visualCenterY = GetTranslate().y + 0.68f * bodyHeight;
    const float visualHalfHeight = 0.75f * (std::max)(0.7f, std::abs(GetScale().x));
    const AABB targetBounds = target_->GetAABB();
    if (targetBounds.max.y < visualCenterY - visualHalfHeight ||
        targetBounds.min.y > visualCenterY + visualHalfHeight) {
        return;
    }

    const engine::Vector3 rayStart = GetWorldPosition() + engine::Vector3{0.0f, 0.72f * bodyHeight, 0.0f};
    const engine::Vector3 targetCenter = (targetBounds.min + targetBounds.max) * 0.5f;
    const engine::Vector3 rayDelta = targetCenter - rayStart;
    const float rayDistance = engine::Math::Length(rayDelta);
    if (rayDistance > 0.01f) {
        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("ground") | CollisionCategories::Get("map_block");
        filter.ignoredObject = this;
        const RaycastHit wall = CollisionManager::GetInstance()->Raycast(
            rayStart, rayDelta / rayDistance, rayDistance, filter);
        if (wall.isHit && wall.distance < rayDistance - 0.1f) {
            return;
        }
    }

    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = attack.damage;
    damageEvent.knockbackVelocity = attack.ResolveKnockback(direction, {direction.x * 9.5f, 4.4f, direction.z * 9.5f});
    damageEvent.damageType = DamageType::Fire;
    damageEvent.statusEffect = MakeStatusEffect(attack);
    damageEvent.invincibilityDuration = attack.ResolveInvincibilityDuration();
    EventManager::GetInstance()->Dispatch(damageEvent);
    if (!attack.feedbackCue.empty()) {
        VFXSequencer::PlayOneShot(attack.feedbackCue, target_->GetWorldPosition());
    }
}

void EnemyFireSlime::FireFireball(const engine::Vector3& direction, float distance) {
    // 弾の衝突属性・威力・燃焼設定を JSON の攻撃定義から同じ発射要求にまとめる。
    if (!target_)
        return;

    engine::Vector3 spawnPos = GetTranslate() + direction * 0.65f;
    spawnPos.y += 1.05f;

    engine::Vector3 aim = target_->GetTranslate() - spawnPos;
    aim.y += 0.35f;
    if (engine::Math::Length(aim) <= 0.001f) {
        aim = {direction.x, 0.12f, direction.z};
    }
    aim = engine::Math::Normalize(aim);

    const EnemyAttackDefinition& attack = GetAttackDefinition(kFireballAttackId);
    const float speed = std::clamp(distance * 1.65f, attack.minSpeed, attack.maxSpeed);
    BulletManager::GetInstance()->Fire(spawnPos, aim * speed, CollisionCategories::Get("enemy_attack"), CollisionCategories::Get("player") | CollisionCategories::Get("ground"), "Primitives/sphere",
                                       0.5f, attack.lifetime, MakeFireVisual(1.2f), attack.damage,
                                       MakeStatusEffect(attack), DamageType::Fire);

    EmitFirePreset(attack.activeVfx.empty() ? kCastPreset : attack.activeVfx.c_str(), spawnPos);
}

AttackSequenceContext EnemyFireSlime::MakeSequenceContext(const engine::Vector3& direction, float distance) {
    // シーケンスの条件評価へ、現在の向き・接地・目標位置を一度に渡す。
    AttackSequenceContext context;
    context.owner = this;
    context.position = GetWorldPosition();
    context.forward = direction;
    context.hasTarget = target_ != nullptr;
    if (target_) context.targetPosition = target_->GetWorldPosition();
    context.targetDistance = distance;
    context.grounded = isGrounded_;
    return context;
}

const EnemyAttackDefinition* EnemyFireSlime::SelectSequenceAttack(float distance) const {
    // プレビューでは指定攻撃、実戦では距離と索敵範囲に合う攻撃を選ぶ。
    return attackProfile_.SelectSequence(distance, detectionRange_, previewAttackId_);
}

void EnemyFireSlime::UpdateHeadFlame(float deltaTime) {
    // 頭上の炎は AI と別の更新経路で維持し、待機中も動かす。
    UpdateHeadFlameVisual(deltaTime);
}

void EnemyFireSlime::EnsureHeadFlameVisual() {
    if (headFlameVisual_ || headFlameRemoveRequested_) {
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (!scene || !scene->GetObject3dCommon()) {
        return;
    }

    auto flame = std::make_unique<engine::Object3d>();
    flame->Initialize(scene->GetObject3dCommon());
    flame->SetName(GetName() + "_HeadFlameShader");
    flame->SetClassName("Effect");
    // 敵に追従する一時演出はシーン保存の対象にしない。
    flame->SetEditorInternal(true);
    flame->SetModel("Primitives/sphere");
    flame->SetColliderType(ColliderType::kNone);
    flame->SetCollisionAttribute(0);
    flame->SetCollisionMask(0);
    flame->SetBlendMode(BlendMode::kNormal);
    flame->SetMaterialType(kHeadFlameMaterialType);
    flame->SetSelectedLighting(0);
    flame->SetEnableLighting(false);
    flame->SetColor({1.0f, 0.24f, 0.04f, 0.82f});
    flame->SetEmissive(1.7f);

    if (auto* renderer = flame->GetMeshRenderer()) {
        if (auto* water = renderer->GetWaterParamData()) {
            water->effectType = kHeadFlameEffectType;
            water->waveSpeed = 1.75f;
            water->effectScale = 0.95f;
            water->waveFrequency = 2.2f;
            water->waveHeight = 0.55f;
            water->effectSoftness = 0.48f;
            water->effectIntensity = 0.84f;
            water->billboardScale = 0.80f;
            water->effectScaleX = 0.92f;
            water->effectScaleY = 1.0f;
            water->effectScaleZ = 0.92f;
            water->uvOffsetX = 2.73f;
            water->uvOffsetY = 5.17f;
        }
    }

    headFlameVisual_ = flame.get();
    scene->AddObject(std::move(flame));
}

void EnemyFireSlime::UpdateHeadFlameVisual(float deltaTime) {
    EnsureHeadFlameVisual();
    if (!headFlameVisual_) {
        return;
    }

    const engine::Vector3 scale = GetScale();
    const float bodyScale = (std::max)({0.7f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    engine::Vector3 velocity = GetVelocity();
    velocity.y = 0.0f;
    const float velocityResponse = 1.0f - std::exp(-(std::max)(0.0f, deltaTime) * 9.0f);
    smoothedFlameVelocity_ = engine::Math::Lerp(smoothedFlameVelocity_, velocity, velocityResponse);
    const float moveSpeed = engine::Math::Length(smoothedFlameVelocity_);
    engine::Vector3 pos = GetTranslate();
    pos.y += kHeadFlameHeight * (std::max)(0.65f, std::abs(scale.y));
    if (moveSpeed > 0.001f) {
        const engine::Vector3 trailDirection = engine::Math::Normalize(smoothedFlameVelocity_) * -1.0f;
        pos = pos + trailDirection * (std::min)(0.32f, moveSpeed * 0.022f);
    }

    // 炎の向きを本体へ固定し、カメラ移動時にも内部の奥行きが保たれるようにします。
    const float yaw = GetRotation().y;
    const engine::Vector3 localRight = {std::cos(yaw), 0.0f, -std::sin(yaw)};
    const float flowX = (std::clamp)(-engine::Math::Dot(smoothedFlameVelocity_, localRight) * 0.12f, -1.0f, 1.0f);

    const float pulse = std::sin(idleTimer_ * 8.0f) * 0.06f;
    const float speedRate = (std::clamp)(moveSpeed * 0.055f, 0.0f, 1.0f);
    const float width = kHeadFlameBaseScale * (0.94f + pulse + speedRate * 0.06f) * bodyScale;
    const float height = kHeadFlameBaseScale * (0.82f - pulse * 0.16f + speedRate * 0.08f) * bodyScale;

    headFlameVisual_->SetIsVisible(GetIsVisible() && !isDead);
    headFlameVisual_->SetTranslate(pos);
    headFlameVisual_->SetRotation({0.0f, yaw, 0.0f});
    headFlameVisual_->SetScale({width, height, width});
    headFlameVisual_->SetColor({1.0f, 0.24f + std::sin(idleTimer_ * 5.3f) * 0.035f, 0.04f, 0.80f});

    if (auto* renderer = headFlameVisual_->GetMeshRenderer()) {
        if (auto* water = renderer->GetWaterParamData()) {
            water->flowSpeedX = flowX;
            water->flowSpeedY = speedRate;
            water->waveSpeed = 1.75f + std::sin(idleTimer_ * 1.7f) * 0.16f + speedRate * 0.42f;
            water->effectIntensity = 0.82f + std::sin(idleTimer_ * 4.1f) * 0.045f + speedRate * 0.07f;
            water->effectScaleX = 0.92f + speedRate * 0.10f;
            water->effectScaleY = 1.0f + speedRate * 0.08f;
        }
    }

    headFlameVisual_->UpdateLocalMatrix();
    headFlameVisual_->UpdateWorldMatrix();

    headFlameParticleTimer_ -= deltaTime;
    headEmberParticleTimer_ -= deltaTime;
    if (headFlameVisual_->GetIsVisible()) {
        engine::Vector3 particleDirection = {-smoothedFlameVelocity_.x * 0.075f, 0.78f, -smoothedFlameVelocity_.z * 0.075f};
        if (headFlameParticleTimer_ <= 0.0f) {
            constexpr float kGoldenAngle = 2.39996323f;
            const float phase = idleTimer_ * 2.8f + static_cast<float>(headFlameParticleCursor_) * kGoldenAngle;
            const float ringRadius = bodyScale * (0.10f + 0.025f * std::sin(phase * 1.7f));
            engine::Vector3 emitPosition = pos;
            emitPosition.x += std::cos(phase) * ringRadius;
            emitPosition.z += std::sin(phase) * ringRadius;
            emitPosition.y += bodyScale * (0.18f + 0.06f * std::sin(phase * 1.31f));
            EmitDirectedFirePreset(kHeadFlamePreset, emitPosition, particleDirection, 0.90f + speedRate * 0.34f);
            ++headFlameParticleCursor_;
            headFlameParticleTimer_ = 0.12f;
        }
        if (headEmberParticleTimer_ <= 0.0f) {
            const float phase = idleTimer_ * 4.2f + static_cast<float>(headFlameParticleCursor_) * 1.37f;
            engine::Vector3 emitPosition = pos;
            emitPosition.x += std::cos(phase) * bodyScale * 0.26f;
            emitPosition.z += std::sin(phase) * bodyScale * 0.26f;
            emitPosition.y += 0.12f;
            EmitDirectedFirePreset(kHeadEmberPreset, emitPosition, particleDirection, 1.02f + speedRate * 0.44f);
            headEmberParticleTimer_ = 0.24f;
        }
    }
}

void EnemyFireSlime::RequestRemoveHeadFlameVisual() {
    // シーン所有の表示物は削除要求を一度だけ送り、手元の参照を直ちに外す。
    if (headFlameRemoveRequested_) {
        return;
    }

    headFlameRemoveRequested_ = true;
    if (!headFlameVisual_) {
        return;
    }

    headFlameVisual_->SetIsVisible(false);
    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (scene) {
        scene->RequestRemoveObject(headFlameVisual_);
    }
    headFlameVisual_ = nullptr;
}

void EnemyFireSlime::EnsureBreathFlameVisuals() {
    if (breathFlameRemoveRequested_) {
        return;
    }

    bool needsCreate = false;
    for (int i = 0; i < kBreathFlameVisualCount; ++i) {
        if (!breathFlameVisuals_[i]) {
            needsCreate = true;
            break;
        }
    }
    if (!needsCreate) {
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (!scene || !scene->GetObject3dCommon()) {
        return;
    }

    for (int i = 0; i < kBreathFlameVisualCount; ++i) {
        if (breathFlameVisuals_[i]) {
            continue;
        }

        auto flame = std::make_unique<engine::Object3d>();
        flame->Initialize(scene->GetObject3dCommon());
        flame->SetName(GetName() + "_BreathFlameShader_" + std::to_string(i));
        flame->SetClassName("Effect");
        // ブレスの一時演出も配置オブジェクトとして保存しない。
        flame->SetEditorInternal(true);
        flame->SetModel("Primitives/sphere");
        flame->SetColliderType(ColliderType::kNone);
        flame->SetCollisionAttribute(0);
        flame->SetCollisionMask(0);
        flame->SetBlendMode(BlendMode::kNormal);
        flame->SetMaterialType(kHeadFlameMaterialType);
        flame->SetSelectedLighting(0);
        flame->SetEnableLighting(false);
        flame->SetIsVisible(false);
        flame->SetColor({1.0f, 0.31f, 0.05f, 0.0f});
        flame->SetEmissive(1.8f);

        if (auto* renderer = flame->GetMeshRenderer()) {
            if (auto* water = renderer->GetWaterParamData()) {
                water->effectType = kBreathFlameEffectType;
                water->waveSpeed = 2.45f;
                water->effectScale = 0.92f;
                water->effectSoftness = 0.58f;
                water->effectIntensity = 0.92f;
                water->billboardScale = 0.72f;
                water->effectScaleX = 1.0f;
                water->effectScaleY = 0.90f;
                water->effectScaleZ = 1.15f;
                water->uvOffsetX = 3.71f * static_cast<float>(i + 1);
                water->uvOffsetY = 6.13f * static_cast<float>(i + 1);
            }
        }

        breathFlameVisuals_[i] = flame.get();
        scene->AddObject(std::move(flame));
    }
}

void EnemyFireSlime::UpdateBreathFlameVisuals(const engine::Vector3& direction, float deltaTime) {
    // 複数の炎メッシュを射線上に配し、発生・消滅のアルファを一つの進行率で揃える。
    (void)deltaTime;
    EnsureBreathFlameVisuals();

    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    const float activeDuration = (std::max)(0.01f, attack.activeDuration);
    const engine::Vector3 scale = GetScale();
    const engine::Vector3 origin = GetTranslate();
    const float bodyScale = (std::max)({0.7f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    const float progress = 1.0f - (std::clamp)(breathTimer_ / activeDuration, 0.0f, 1.0f);
    const float fadeIn = (std::clamp)(progress / 0.16f, 0.0f, 1.0f);
    const float fadeOut = (std::clamp)(breathTimer_ / (activeDuration * 0.18f), 0.0f, 1.0f);
    const float breathAlpha = fadeIn * fadeOut;
    const engine::Vector3 side = {-direction.z, 0.0f, direction.x};

    for (int i = 0; i < kBreathFlameVisualCount; ++i) {
        engine::Object3d* flame = breathFlameVisuals_[i];
        if (!flame) {
            continue;
        }

        const float step = (static_cast<float>(i) + 0.5f) / static_cast<float>(kBreathFlameVisualCount);
        const float distance = 0.50f + step * (attack.maxRange * 0.84f);
        const float wobble = std::sin(idleTimer_ * (6.2f + step * 1.4f) + step * 8.3f) * (0.025f + step * 0.07f);
        engine::Vector3 pos = origin + direction * distance + side * wobble;
        pos.y += (0.62f + step * 0.12f) * (std::max)(0.7f, std::abs(scale.y));

        const float yaw = std::atan2(direction.x, direction.z);

        const float pulse = std::sin(idleTimer_ * (8.2f + step * 2.3f) + step * 5.7f) * 0.035f;
        const float width = (0.64f + step * 0.78f + pulse) * bodyScale;
        const float height = (0.34f + step * 0.23f - pulse * 0.18f) * bodyScale;
        const float alpha = breathAlpha * (0.78f - step * 0.12f);

        flame->SetIsVisible(alpha > 0.02f);
        flame->SetTranslate(pos);
        flame->SetRotation({0.0f, yaw, 0.0f});
        flame->SetScale({width, (std::max)(height, width * 0.68f), width * 1.12f});
        flame->SetColor({1.0f, 0.30f + step * 0.06f, 0.045f, alpha});

        if (auto* renderer = flame->GetMeshRenderer()) {
            if (auto* water = renderer->GetWaterParamData()) {
                water->flowSpeedX = wobble;
                water->flowSpeedY = 0.82f + step * 0.42f;
                water->waveSpeed = 2.35f + step * 0.58f;
                water->effectIntensity = 0.88f + step * 0.10f;
                water->effectScaleX = 1.08f + step * 0.56f;
                water->effectScaleY = 0.90f;
                water->effectScaleZ = 1.15f;
            }
        }

        flame->UpdateLocalMatrix();
        flame->UpdateWorldMatrix();
    }
}

void EnemyFireSlime::HideBreathFlameVisuals() {
    // 運搬や攻撃終了時はシーンから消す前にまず描画を止める。
    for (engine::Object3d* flame : breathFlameVisuals_) {
        if (flame) {
            flame->SetIsVisible(false);
        }
    }
}

void EnemyFireSlime::RequestRemoveBreathFlameVisuals() {
    // シーンが実体を保持するため、各メッシュへ削除要求を送り参照だけ破棄する。
    if (breathFlameRemoveRequested_) {
        return;
    }

    breathFlameRemoveRequested_ = true;
    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;

    for (engine::Object3d*& flame : breathFlameVisuals_) {
        if (!flame) {
            continue;
        }
        flame->SetIsVisible(false);
        if (scene) {
            scene->RequestRemoveObject(flame);
        }
        flame = nullptr;
    }
}

void EnemyFireSlime::EmitBreathParticles(const engine::Vector3& origin, const engine::Vector3& direction) {
    // 放出口を順に前へ進め、メッシュの炎と同じ距離帯に火の粉を散らす。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    const float step = (static_cast<float>(breathParticleCursor_ % kBreathFlameVisualCount) + 0.5f) /
                       static_cast<float>(kBreathFlameVisualCount);
    engine::Vector3 emitPosition = origin + direction * (0.48f + step * attack.maxRange * 0.82f);
    emitPosition.y += std::sin(idleTimer_ * 8.0f + step * 9.1f) * 0.08f;

    engine::Vector3 streamDirection = direction;
    streamDirection.y = 0.10f + step * 0.10f;
    streamDirection = engine::Math::Normalize(streamDirection);
    const char* bodyPreset = attack.activeVfx.empty() ? kBreathPreset : attack.activeVfx.c_str();
    EmitDirectedFirePreset(bodyPreset, emitPosition, streamDirection, 0.82f + step * 0.34f);

    if (breathEmberTimer_ <= 0.0f) {
        EmitDirectedFirePreset(kBreathEmberPreset, emitPosition, streamDirection, 0.95f + step * 0.42f);
        breathEmberTimer_ += 0.14f;
    }
    ++breathParticleCursor_;
}

void EnemyFireSlime::EmitFirePreset(const char* presetName, const engine::Vector3& position) {
    // 未初期化の GPU パーティクル管理には要求を送らない。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->Emit(presetName, position);
}

void EnemyFireSlime::EmitDirectedFirePreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                                            float speedScale) {
    // 火の粉の流れる向きを指定できる場合だけ方向付きプリセットを使う。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->EmitDirected(presetName, position, direction, speedScale);
}

void EnemyFireSlime::ApplySlimeAnimation(float deltaTime) {
    if (auto* controller = GetAnimatorControllerAsset()) {
        std::string stateName = "Idle";
        if (fireballSequence_.IsRunning() || fireballWindupTimer_ > 0.0f) {
            stateName = selectedFireball_.animation;
        } else if (breathTimer_ > 0.0f) {
            stateName = GetAttackDefinition(kBreathAttackId).animation;
        } else {
            const engine::Vector3 velocity = GetVelocity();
            if (velocity.x * velocity.x + velocity.z * velocity.z > 0.04f) stateName = "Move";
        }
        // 状態配列の並び替えでは値が変わらないよう、Controllerの遷移条件から入力値を読む。
        int motionMode = 0;
        for (const auto& transition : controller->transitions) {
            if (transition.toState != stateName) continue;
            for (const auto& condition : transition.conditions) {
                if (condition.parameter == "MotionMode" && condition.mode == AnimatorConditionMode::Equals) {
                    motionMode = static_cast<int>(condition.threshold);
                    break;
                }
            }
        }
        if (auto* runtime = GetAnimatorControllerRuntime()) runtime->SetInt("MotionMode", motionMode);
        // 当たり判定の物理Transformは基準値に保ち、Controllerの姿勢は描画だけに適用する。
        engine::Vector3 scale = baseScale_;
        engine::Vector3 rotation = {0.0f, GetRotation().y, 0.0f};
        ApplyDamageReactionPose(scale, rotation);
        const float poseRate = (std::min)(1.0f, deltaTime * 13.0f);
        SetScale(engine::Math::Lerp(GetScale(), scale, poseRate));
        const engine::Vector3 current = GetRotation();
        SetRotation({current.x + (rotation.x - current.x) * poseRate, current.y,
                     current.z + (rotation.z - current.z) * poseRate});
        return;
    }
    engine::Vector3 targetScale = baseScale_;
    engine::Vector3 targetRotation = {0.0f, GetRotation().y, 0.0f};
    if (fireballSequence_.ControlsActor()) {
        // Controllerを読めない環境でも、共通シーケンスの時計に合わせて溜めと反動を見せる。
        const float elapsed = fireballSequence_.GetElapsed();
        const float windup = (std::max)(0.01f, selectedFireball_.windupDuration);
        if (elapsed < windup) {
            const float charge = std::clamp(elapsed / windup, 0.0f, 1.0f);
            targetScale.x = baseScale_.x * (1.0f + charge * 0.18f);
            targetScale.y = baseScale_.y * (1.0f - charge * 0.24f);
            targetScale.z = baseScale_.z * (1.0f - charge * 0.08f);
            targetRotation.x = charge * 0.12f;
        } else {
            const float recovery = (std::max)(0.01f, selectedFireball_.recoveryDuration);
            const float recoil = std::sin(std::clamp((elapsed - windup) / recovery, 0.0f, 1.0f) * 3.14159265f);
            targetScale.x = baseScale_.x * (1.0f - recoil * 0.08f);
            targetScale.y = baseScale_.y * (1.0f + recoil * 0.12f);
            targetScale.z = baseScale_.z * (1.0f + recoil * 0.20f);
            targetRotation.x = -recoil * 0.16f;
        }
    } else if (breathTimer_ > 0.0f) {
        const float activeDuration = (std::max)(0.01f, GetAttackDefinition(kBreathAttackId).activeDuration);
        const float t = std::clamp(1.0f - (breathTimer_ / activeDuration), 0.0f, 1.0f);
        const float inhale = std::sin(std::clamp(t / 0.24f, 0.0f, 1.0f) * 3.14159265f);
        const float exhale = std::clamp((t - 0.16f) / 0.24f, 0.0f, 1.0f);
        const float pulse = std::sin(t * 22.0f) * 0.035f * exhale;
        targetScale.x = baseScale_.x * (1.0f + inhale * 0.12f + exhale * 0.08f + pulse);
        targetScale.y = baseScale_.y * (1.0f + inhale * 0.10f - exhale * 0.20f - pulse * 0.30f);
        targetScale.z = baseScale_.z * (1.0f - inhale * 0.12f + exhale * 0.30f - pulse * 0.55f);
        targetRotation.x = inhale * 0.09f - exhale * 0.07f;
    } else if (fireballWindupTimer_ > 0.0f) {
        const float windupDuration = (std::max)(0.01f, selectedFireball_.windupDuration);
        const float charge = std::clamp(1.0f - fireballWindupTimer_ / windupDuration, 0.0f, 1.0f);
        const float tremble = std::sin(idleTimer_ * 31.0f) * 0.025f * charge;
        targetScale.x = baseScale_.x * (1.0f + charge * 0.18f + tremble);
        targetScale.y = baseScale_.y * (1.0f - charge * 0.24f);
        targetScale.z = baseScale_.z * (1.0f - charge * 0.08f - tremble * 0.6f);
        targetRotation.x = charge * 0.12f;
        targetRotation.z = tremble * 1.6f;
    } else if (attackTimer_ > 0.0f) {
        const float recoveryDuration = (std::max)(0.01f, selectedFireball_.id.empty()
            ? GetAttackDefinition(kFireballAttackId).recoveryDuration : selectedFireball_.recoveryDuration);
        const float remaining = std::clamp(attackTimer_ / recoveryDuration, 0.0f, 1.0f);
        const float recoil = std::sin((1.0f - remaining) * 3.14159265f);
        targetScale.x = baseScale_.x * (1.0f - recoil * 0.08f);
        targetScale.y = baseScale_.y * (1.0f + recoil * 0.12f);
        targetScale.z = baseScale_.z * (1.0f + recoil * 0.20f);
        targetRotation.x = -recoil * 0.16f;
    } else {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 1.85f;
        params.idleAmplitude = 0.075f;
        params.moveAmplitude = 0.27f;
        params.hopFrequency = 10.4f;
        params.horizontalSquash = 0.28f;
        params.verticalStretch = 0.35f;
        params.airborneStretch = 0.34f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    }

    ApplyDamageReactionPose(targetScale, targetRotation);
    const float poseRate = (std::min)(1.0f, deltaTime * 13.0f);
    engine::Vector3 scale = engine::Math::Lerp(GetScale(), targetScale, poseRate);
    SetScale(scale);
    const engine::Vector3 currentRotation = GetRotation();
    SetRotation({currentRotation.x + (targetRotation.x - currentRotation.x) * poseRate, currentRotation.y,
                 currentRotation.z + (targetRotation.z - currentRotation.z) * poseRate});
}

void EnemyFireSlime::SyncWorldCollisionRadius(float worldRadius) {
    // 見た目の伸縮でワールド空間の当たり半径が変わらないよう逆スケールする。
    const engine::Vector3 scale = GetScale();
    const float maxScale = (std::max)({0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    SetCollisionRadius(worldRadius / maxScale);
}

void EnemyFireSlime::SyncGroundCollisionRadius() {
    // 地上では小さめの本体判定へ戻す。
    SyncWorldCollisionRadius(kGroundCollisionWorldRadius);
}

void EnemyFireSlime::SyncThrownCollisionRadius() {
    // 投擲時は接触を拾いやすい広い判定を使う。
    SyncWorldCollisionRadius(kThrownCollisionWorldRadius);
}
