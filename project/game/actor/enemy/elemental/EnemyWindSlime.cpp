#include "EnemyWindSlime.h"

#include "BaseScene.h"
#include "BulletManager.h"
#include "Character.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "GPUParticleManager.h"
#include "GroundEffectLocator.h"
#include "MeshEffectManager.h"
#include "SceneManager.h"
#include "game/actor/enemy/animation/SlimeBounceAnimator.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
constexpr const char* kGustAttackId = "gust_breath";
constexpr const char* kVolleyAttackId = "aerial_wind_volley";
constexpr int kWindOrbMaterialType = 26;
constexpr float kWindSlimeModelYawOffset = 3.1415926535f;
constexpr float kGroundCollisionWorldRadius = 0.82f;
constexpr float kThrownCollisionWorldRadius = 1.18f;
constexpr float kMoveHopInterval = 0.24f;
constexpr float kMoveHopPower = 5.25f;
constexpr float kUpdraftCooldown = 1.85f;
constexpr float kSoarCooldown = 0.92f;
constexpr float kSoarLaunchSpeed = 27.0f;
constexpr float kSoarForwardSpeed = 8.0f;
constexpr float kSoarSlowFallSpeed = -6.2f;
constexpr float kSoarInvincibleDuration = 0.22f;
constexpr float kSoarSafetyDuration = 8.0f;
constexpr float kSoarEffectInterval = 0.10f;
constexpr float kUpdraftRadius = 3.8f;
constexpr float kUpdraftLiftSpeed = 18.0f;
constexpr float kBreathPushInterval = 0.13f;
constexpr float kBreathParticleInterval = 0.045f;
constexpr float kVolleyShotInterval = 0.34f;
constexpr float kVolleyFirstShotDelay = 0.52f;
constexpr float kVolleyPreferredHoverHeight = 3.10f;
constexpr float kVolleyCeilingClearance = 2.25f;
constexpr float kVolleyOrbVisualScale = 0.46f;
constexpr int kVolleyOrbCount = 3;
constexpr float kVolleyImpactLateralSpacing = 1.05f;
constexpr float kVolleyImpactForwardSpacing = 0.38f;
constexpr const char* kIdlePreset = "wind_slime_idle_wisp";
constexpr const char* kChargePreset = "wind_slime_charge";
constexpr const char* kBreathStreamPreset = "wind_slime_breath_stream";
constexpr const char* kBreathDustPreset = "wind_slime_breath_dust";
constexpr const char* kBurstPreset = "wind_slime_gust_burst";
constexpr const char* kImpactPreset = "wind_slime_gust_impact";
constexpr const char* kOrbHoldPreset = "wind_slime_orb_hold";
constexpr const char* kOrbTrailPreset = "wind_slime_orb_trail";
constexpr const char* kOrbImpactPreset = "wind_slime_orb_impact";
constexpr const char* kUpdraftPreset = "player_wind_updraft";
constexpr const char* kSoarTrailPreset = "player_wind_dash";
constexpr const char* kBurstRingEffectPath = "Resources/json/effect/effect_wind_gust_ring.json";
constexpr const char* kUpdraftSpiralEffectPath = "Resources/json/effect/effect_player_wind_updraft_spiral.json";
constexpr const char* kUpdraftCounterSpiralEffectPath =
    "Resources/json/effect/effect_player_wind_updraft_spiral_counter.json";
constexpr const char* kSoarLaunchEffectPath = "Resources/json/effect/effect_player_wind_soar_launch.json";
constexpr const char* kSlowFallEffectPath = "Resources/json/effect/effect_player_wind_slow_fall.json";
constexpr const char* kSoarLandEffectPath = "Resources/json/effect/effect_player_wind_soar_land.json";

// 離陸と風弾の表示率を0～1へ収め、両端を滑らかにつなぎます。
float SmoothStep01(float value) {
    // 離陸や風弾の出現が段階境界で急に跳ねないよう進行率を滑らかにする。
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

// 風弾の材質、軌跡、着弾の見た目を一つの設定として組み立てます。
BulletVisualConfig MakeWindOrbVisual() {
    // 発射後の弾にも手持ち時と同じ材質・色・軌跡を適用する。
    BulletVisualConfig visual;
    visual.materialType = kWindOrbMaterialType;
    visual.blendMode = BlendMode::kNormal;
    visual.color = {0.42f, 1.0f, 0.80f, 0.96f};
    visual.emissive = 1.9f;
    visual.visualScale = 0.82f;
    visual.waveSpeed = 2.65f;
    visual.waveHeight = 0.72f;
    visual.waveFrequency = 12.0f;
    visual.effectType = 1.0f;
    visual.effectScale = 0.86f;
    visual.effectSoftness = 0.32f;
    visual.effectIntensity = 1.72f;
    visual.billboardScale = 1.0f;
    visual.trailPreset = kOrbTrailPreset;
    visual.impactPreset = kOrbImpactPreset;
    visual.trailInterval = 0.055f;
    visual.trailSpeedScale = 1.25f;
    return visual;
}

engine::Vector3 NormalizePlanar(engine::Vector3 value) {
    // 風の攻撃方向は水平面に固定し、対象と重なる場合は既定の前方を使う。
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {value.x / length, 0.0f, value.z / length};
}

engine::Object3d* FindEnemyRoot(engine::Object3d* object) {
    // 子オブジェクトへの接触から被弾すべき親の敵を探す。
    for (engine::Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}
} // namespace

void EnemyWindSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 攻撃プロフィールと地上時の球判定を本体初期化後に設定する。
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_WindSlime");
    SetEnemyType("WindSlime");
    ReloadAttackProfile();
    SetColor({0.78f, 1.0f, 0.92f, 1.0f});
    defaultColor_ = GetColor();
    SetCollisionAttribute(CollisionCategories::Get("enemy"));
    SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
}

void EnemyWindSlime::Update(float deltaTime) {
    // 非活動時の演出を先に整理し、通常時だけ攻撃・移動・風弾表示を進める。
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    EnsureBaseScale();
    idleTimer_ += deltaTime;
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    ambientParticleTimer_ -= deltaTime;
    volleyRecoilTimer_ = (std::max)(0.0f, volleyRecoilTimer_ - deltaTime);

    if (ambientParticleTimer_ <= 0.0f) {
        EmitDirectedWindPreset(kIdlePreset, GetWorldPosition() + engine::Vector3{0.0f, 0.72f, 0.0f}, {0.15f, 1.0f, 0.08f},
                               0.55f);
        ambientParticleTimer_ = 0.18f;
    }

    engine::Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    UpdateWildBehavior(deltaTime, velocity);

    if (attackState_ == AttackState::Idle && SlimeBounceAnimator::StepGroundHop(groundHopTimer_, velocity, isGrounded_,
                                                                                deltaTime, kMoveHopInterval, 0.10f)) {
        velocity.y = (std::max)(velocity.y, kMoveHopPower);
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    SyncGroundCollisionRadius();
    BaseEnemy::Update(deltaTime);
}

bool EnemyWindSlime::HasOwnedSpecialMaterialVisuals() const {
    // 手持ち風弾が一つでも見えていれば、専用材質の描画パスを要求する。
    for (const auto& orb : heldWindOrbVisuals_) {
        if (orb && orb->GetIsVisible()) {
            return true;
        }
    }
    return false;
}

void EnemyWindSlime::DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 風弾の特殊材質を通常の本体描画と分け、必要な画面テクスチャを渡す。
    for (const auto& orb : heldWindOrbVisuals_) {
        if (orb && orb->GetIsVisible()) {
            orb->DrawWindOrb(depthSrvHandle, grabSrvHandle);
        }
    }
}

bool EnemyWindSlime::UpdateInactiveState(float deltaTime) {
    if (isDead || !GetIsVisible()) {
        // 非表示・死亡時は予告と手持ち弾を消し、復帰時に攻撃の途中から再開しない。
        HideAttackTelegraph();
        HideHeldWindOrbs();
        attackState_ = AttackState::Idle;
        attackStateTimer_ = 0.0f;
        SetVelocity({0.0f, 0.0f, 0.0f});
        return true;
    }
    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        HideHeldWindOrbs();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (isCarried_) {
        HideAttackTelegraph();
        HideHeldWindOrbs();
        return true;
    }
    return UpdateThrowRecoveryState(deltaTime);
}

bool EnemyWindSlime::UpdateThrowRecoveryState(float deltaTime) {
    // 投擲中と復帰中でワールド判定半径を切り替えてから共通物理を進める。
    if (!IsThrowRecovering()) {
        return false;
    }
    if (IsThrownPhysics()) {
        SyncThrownCollisionRadius();
    } else {
        SyncGroundCollisionRadius();
    }
    BaseEnemy::Update(deltaTime);
    return true;
}

void EnemyWindSlime::EnsureBaseScale() {
    // 攻撃の変形が始まる前の配置スケールを一度だけ記録する。
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyWindSlime::UpdateWildBehavior(float deltaTime, engine::Vector3& velocity) {
    engine::Vector3 direction = lockedAttackDirection_;
    float distance = lockedTargetDistance_;
    if (target_ && param_.has_value()) {
        const engine::Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    }

    if (attackState_ != AttackState::Idle) {
        // 攻撃中は通常の追跡移動を止め、攻撃フェーズごとに向きと予告を制御する。
        UpdateAttackState(deltaTime, velocity, direction, distance);
        return;
    }
    if (!target_ || !param_.has_value()) {
        const float speed = param_.has_value() ? (std::max)(0.55f, param_->speed * 0.45f) : 0.8f;
        velocity = CalculateWanderVelocity(deltaTime, speed, 0.72f);
        UpdateFacing(velocity);
        return;
    }

    if (UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateFacing(direction);
        return;
    }

    const EnemyAttackDefinition& gust = GetAttackDefinition(kGustAttackId);
    const EnemyAttackDefinition& volley = GetAttackDefinition(kVolleyAttackId);
    const bool allowGust = debugPreviewAttackId_.empty() || debugPreviewAttackId_ == kGustAttackId;
    const bool allowVolley = debugPreviewAttackId_.empty() || debugPreviewAttackId_ == kVolleyAttackId;
    UpdateFacing(direction);

    if (allowGust && distance >= gust.minRange && distance <= gust.maxRange && attackCooldown_ <= 0.0f) {
        StartGustBreath(direction);
    } else if (allowVolley && distance >= volley.minRange && distance <= volley.maxRange && attackCooldown_ <= 0.0f) {
        StartAerialWindVolley(direction, distance);
    } else if (distance <= detectionRange_) {
        // 近付き過ぎた場合は後退し、息攻撃に適した距離を取り直す。
        const float speed = (std::max)(0.0f, param_->speed) * 1.12f;
        const float approach = distance > gust.maxRange * 0.78f ? 1.0f : -0.46f;
        velocity.x = direction.x * speed * approach;
        velocity.z = direction.z * speed * approach;
    } else {
        velocity = CalculateWanderVelocity(deltaTime, (std::max)(0.55f, param_->speed * 0.48f), 0.72f);
        UpdateFacing(velocity);
    }
}

void EnemyWindSlime::UpdateAttackState(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction, float distance) {
    // 攻撃中は通常の水平移動を止め、状態ごとの高度・予告・発射だけを更新する。
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    // 後隙では予告を消し、タイマー終了で待機へ戻る。
    if (attackState_ == AttackState::Recover) {
        HideAttackTelegraph();
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        if (attackStateTimer_ <= 0.0f) {
            attackState_ = AttackState::Idle;
        }
        return;
    }

    // ブレス中は固定した方向へ周期的に押し出し判定と風粒子を出す。
    if (attackState_ == AttackState::GustActive) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        breathParticleTimer_ -= deltaTime;
        breathPushTimer_ -= deltaTime;
        const engine::Vector3 origin = GetWorldPosition() + engine::Vector3{0.0f, 0.72f, 0.0f};
        const float pushProgress =
            breathPushTimer_ <= 0.0f ? 1.0f : 1.0f - std::clamp(breathPushTimer_ / kBreathPushInterval, 0.0f, 1.0f);

        ShowAttackTelegraphCone(GetWorldPosition(), lockedAttackDirection_, attack.maxRange, 0.72f,
                                attack.radius * 2.0f, pushProgress, {0.46f, 1.0f, 0.84f, 0.72f});
        if (breathParticleTimer_ <= 0.0f) {
            EmitWindBreathParticles(origin, lockedAttackDirection_, attack.maxRange);
            breathParticleTimer_ += kBreathParticleInterval;
        }
        if (breathPushTimer_ <= 0.0f) {
            DispatchEnemyBreathPush(lockedAttackDirection_, distance);
            breathPushTimer_ += kBreathPushInterval;
        }
        if (attackStateTimer_ <= 0.0f) {
            BeginRecover(attack);
        }
        return;
    }

    // 弾幕の準備中は上昇しながら着弾地点を示し、後半に狙いを固定する。
    if (attackState_ == AttackState::VolleyTakeoff) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.01f, attack.windupDuration);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        breathParticleTimer_ -= deltaTime;
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);

        if (progress < 0.64f) {
            // 離陸前半だけ追尾し、後半は着弾予告の中心を固定する。
            lockedAttackDirection_ = direction;
            lockedTargetDistance_ = distance;
            UpdateVolleyImpactCenters();
        }
        UpdateFacing(lockedAttackDirection_);
        const float height = volleyGroundY_ + (volleyHoverY_ - volleyGroundY_) * SmoothStep01(progress);
        HoldVolleyAltitude(height, deltaTime, velocity);
        ShowVolleyImpactTelegraphs(progress);
        UpdateHeldWindOrbs(deltaTime, SmoothStep01((progress - 0.38f) / 0.52f));

        if (breathParticleTimer_ <= 0.0f) {
            const engine::Vector3 liftOrigin = {GetWorldPosition().x, volleyGroundY_ + 0.12f, GetWorldPosition().z};
            EmitDirectedWindPreset(kUpdraftPreset, liftOrigin,
                                   {-lockedAttackDirection_.x * 0.12f, 1.0f, -lockedAttackDirection_.z * 0.12f},
                                   1.0f + progress * 0.72f);
            EmitDirectedWindPreset(kChargePreset, GetWorldPosition() + engine::Vector3{0.0f, 0.72f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                   0.88f + progress * 0.64f);
            breathParticleTimer_ += 0.065f;
        }
        if (!warningTriggered_ && attackStateTimer_ <= attack.warningLeadTime) {
            TriggerAttackTelegraphCue({0.72f, 1.0f, 0.92f, 1.0f});
            warningTriggered_ = true;
        }
        // 上昇完了後に弾の保持演出を発射段階へ移す。
        if (attackStateTimer_ <= 0.0f) {
            attackState_ = AttackState::VolleyActive;
            const float minimumActive = kVolleyFirstShotDelay + kVolleyShotInterval * 2.0f + 0.28f;
            attackStateTimer_ = (std::max)(minimumActive, attack.activeDuration);
            volleyShotTimer_ = kVolleyFirstShotDelay;
            volleyShotCount_ = 0;
            volleyVisualTimer_ = 0.0f;
            EmitWindPreset(kBurstPreset, GetWorldPosition() + engine::Vector3{0.0f, 0.82f, 0.0f});
        }
        return;
    }

    // 空中では次弾の予告を保ち、発射数が尽きた後に着地へ進む。
    if (attackState_ == AttackState::VolleyActive) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        volleyShotTimer_ -= deltaTime;
        const float hoverBob = std::sin(idleTimer_ * 5.8f) * 0.10f;
        HoldVolleyAltitude(volleyHoverY_ + hoverBob, deltaTime, velocity);
        UpdateFacing(direction);
        lockedAttackDirection_ = direction;
        lockedTargetDistance_ = distance;
        if (volleyShotCount_ < kVolleyOrbCount) {
            const float shotInterval = volleyShotCount_ == 0 ? (std::max)(kVolleyFirstShotDelay, 0.01f)
                                                             : (std::max)(kVolleyShotInterval, 0.01f);
            const float shotProgress = 1.0f - std::clamp(volleyShotTimer_ / shotInterval, 0.0f, 1.0f);
            ShowVolleyImpactTelegraphs(shotProgress);
        } else {
            HideAttackTelegraph();
        }
        UpdateHeldWindOrbs(deltaTime);

        // 大きなdeltaTimeで発射時刻を跨いでも、必要な弾数を順に処理する。
        while (volleyShotCount_ < kVolleyOrbCount && volleyShotTimer_ <= 0.0f) {
            FireAerialWindOrb(volleyShotCount_);
            ++volleyShotCount_;
            volleyRecoilTimer_ = 0.18f;
            volleyShotTimer_ += kVolleyShotInterval;
        }
        if (attackStateTimer_ <= 0.0f && volleyShotCount_ >= kVolleyOrbCount) {
            BeginVolleyLanding(attack);
        }
        return;
    }

    // 着地中は保持弾を隠し、地面に戻った時点で接地状態と衝撃演出を確定する。
    if (attackState_ == AttackState::VolleyLanding) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.16f, attack.recoveryDuration);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float height = volleyLandingStartY_ + (volleyGroundY_ - volleyLandingStartY_) * SmoothStep01(progress);
        HoldVolleyAltitude(height, deltaTime, velocity);
        HideHeldWindOrbs();
        breathParticleTimer_ -= deltaTime;
        if (breathParticleTimer_ <= 0.0f) {
            EmitDirectedWindPreset(kUpdraftPreset, GetWorldPosition(), {0.0f, -1.0f, 0.0f}, 0.78f);
            breathParticleTimer_ += 0.09f;
        }
        if (attackStateTimer_ <= 0.0f) {
            engine::Vector3 position = GetTranslate();
            position.y = volleyGroundY_;
            SetTranslate(position);
            velocity.y = 0.0f;
            SetGrounded(true);
            EmitWindPreset(kImpactPreset, position + engine::Vector3{0.0f, 0.16f, 0.0f});
            attackState_ = AttackState::Recover;
            recoveryDuration_ = 0.18f;
            attackStateTimer_ = recoveryDuration_;
        }
        return;
    }

    // 残る状態はブレスの予備動作。前半だけ方向を追い、予告後は狙いを固定する。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
    const float duration = (std::max)(0.01f, attack.windupDuration);
    attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
    breathParticleTimer_ -= deltaTime;
    const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);

    if (progress < 0.58f) {
        lockedAttackDirection_ = direction;
        lockedTargetDistance_ = distance;
        UpdateFacing(direction);
    }
    ShowAttackTelegraphCone(GetWorldPosition(), lockedAttackDirection_, attack.maxRange, 0.72f, attack.radius * 2.0f,
                            progress, {0.42f, 1.0f, 0.86f, 0.82f});
    if (!warningTriggered_ && attackStateTimer_ <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue({0.70f, 1.0f, 0.94f, 1.0f});
        warningTriggered_ = true;
    }
    if (breathParticleTimer_ <= 0.0f) {
        EmitDirectedWindPreset(kChargePreset, GetWorldPosition() + engine::Vector3{0.0f, 0.72f, 0.0f},
                               lockedAttackDirection_ * -1.0f + engine::Vector3{0.0f, 0.18f, 0.0f}, 0.82f + progress * 0.75f);
        breathParticleTimer_ += 0.07f;
    }

    if (attackStateTimer_ <= 0.0f) {
        attackState_ = AttackState::GustActive;
        attackStateTimer_ = (std::max)(0.18f, attack.activeDuration);
        breathParticleTimer_ = 0.0f;
        breathPushTimer_ = 0.0f;
        breathParticleCursor_ = 0;
        EmitWindPreset(kBurstPreset, GetWorldPosition() + engine::Vector3{0.0f, 0.72f, 0.0f});
    }
}

void EnemyWindSlime::StartGustBreath(const engine::Vector3& direction) {
    // ブレスの方向を記録し、予告を出す時間を攻撃プロフィールから取得する。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
    attackState_ = AttackState::GustWindup;
    attackStateTimer_ = (std::max)(0.01f, attack.windupDuration);
    lockedAttackDirection_ = NormalizePlanar(direction);
    warningTriggered_ = false;
    breathParticleTimer_ = 0.0f;
}

void EnemyWindSlime::StartAerialWindVolley(const engine::Vector3& direction, float distance) {
    // 離陸前の地面高さと着弾方向を保存し、三連風弾の予告を準備する。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    attackState_ = AttackState::VolleyTakeoff;
    attackStateTimer_ = (std::max)(0.01f, attack.windupDuration);
    lockedAttackDirection_ = NormalizePlanar(direction);
    lockedTargetDistance_ = distance;
    volleyGroundY_ = GetTranslate().y;
    volleyHoverY_ = ResolveVolleyHoverY();
    volleyLandingStartY_ = volleyHoverY_;
    volleyShotTimer_ = 0.0f;
    volleyVisualTimer_ = 0.0f;
    volleyRecoilTimer_ = 0.0f;
    volleyShotCount_ = 0;
    warningTriggered_ = false;
    breathParticleTimer_ = 0.0f;
    UpdateVolleyImpactCenters();
    SetGrounded(false);
    EnsureHeldWindOrbs();
    HideHeldWindOrbs();
}

void EnemyWindSlime::BeginRecover(const EnemyAttackDefinition& attack) {
    // 攻撃表示を終え、プロフィールの硬直と再使用待ちへ移る。
    attackState_ = AttackState::Recover;
    recoveryDuration_ = (std::max)(0.05f, attack.recoveryDuration);
    attackStateTimer_ = recoveryDuration_;
    attackCooldown_ = attack.cooldown;
    HideAttackTelegraph();
}

void EnemyWindSlime::BeginVolleyLanding(const EnemyAttackDefinition& attack) {
    // 最後の弾を放した後に予告と手持ち弾を消し、現在高度から降下する。
    attackState_ = AttackState::VolleyLanding;
    attackStateTimer_ = (std::max)(0.16f, attack.recoveryDuration);
    recoveryDuration_ = attackStateTimer_;
    attackCooldown_ = attack.cooldown;
    volleyLandingStartY_ = GetTranslate().y;
    volleyVisualTimer_ = 0.0f;
    breathParticleTimer_ = 0.0f;
    HideAttackTelegraph();
    HideHeldWindOrbs();
}

void EnemyWindSlime::FireAerialWindOrb(int orbIndex) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    const engine::Vector3 spawnPosition = ComputeHeldWindOrbPosition(orbIndex);
    engine::Vector3 aim = lockedAttackDirection_ + engine::Vector3{0.0f, -0.08f, 0.0f};
    float targetDistance = lockedTargetDistance_;
    if (orbIndex >= 0 && orbIndex < static_cast<int>(volleyImpactCenters_.size())) {
        // 地上の予告円と実際の弾着点を一致させます。
        aim = volleyImpactCenters_[orbIndex] + engine::Vector3{0.0f, 0.24f, 0.0f} - spawnPosition;
        targetDistance = engine::Math::Length(aim);
    }
    if (engine::Math::Length(aim) <= 0.001f) {
        aim = {lockedAttackDirection_.x, -0.08f, lockedAttackDirection_.z};
    }
    aim = engine::Math::Normalize(aim);

    const float minimumSpeed = attack.minSpeed > 0.0f ? attack.minSpeed : 14.0f;
    const float maximumSpeed = attack.maxSpeed > minimumSpeed ? attack.maxSpeed : 22.0f;
    const float speed = std::clamp(targetDistance * 1.65f, minimumSpeed, maximumSpeed);
    const float lifetime = attack.lifetime > 0.0f ? attack.lifetime : 2.8f;
    const float radius = (std::max)(0.32f, attack.radius);
    BulletManager::GetInstance()->Fire(spawnPosition, aim * speed, CollisionCategories::Get("enemy_attack"), CollisionCategories::Get("player") | CollisionCategories::Get("ground"),
                                       "Primitives/sphere", radius, lifetime, MakeWindOrbVisual(), attack.damage, {},
                                       DamageType::Physical);

    EmitDirectedWindPreset(kBurstPreset, spawnPosition, aim, 1.12f);
    if (orbIndex >= 0 && orbIndex < static_cast<int>(heldWindOrbVisuals_.size()) && heldWindOrbVisuals_[orbIndex]) {
        heldWindOrbVisuals_[orbIndex]->SetIsVisible(false);
    }
}

void EnemyWindSlime::UpdateVolleyImpactCenters() {
    // 対象の水平移動を上限付きで先読みし、三つの着弾点を左右に分散する。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    const engine::Vector3 forward = NormalizePlanar(lockedAttackDirection_);
    const engine::Vector3 right = {forward.z, 0.0f, -forward.x};

    engine::Vector3 targetPosition =
        GetWorldPosition() + forward * std::clamp(lockedTargetDistance_, attack.minRange, attack.maxRange);
    engine::Vector3 targetVelocity = {};
    if (const Character* targetCharacter = dynamic_cast<const Character*>(target_)) {
        targetPosition = targetCharacter->GetWorldPosition();
        targetVelocity = targetCharacter->GetVelocity();
        targetVelocity.y = 0.0f;

        const float speed = engine::Math::Length(targetVelocity);
        constexpr float kMaximumPredictionSpeed = 6.0f;
        if (speed > kMaximumPredictionSpeed && speed > 0.001f) {
            targetVelocity = targetVelocity * (kMaximumPredictionSpeed / speed);
        }
    }

    for (int index = 0; index < kVolleyOrbCount; ++index) {
        const float centeredIndex = static_cast<float>(index - 1);
        const float leadTime = kVolleyFirstShotDelay + kVolleyShotInterval * static_cast<float>(index);
        engine::Vector3 impact = targetPosition + targetVelocity * (leadTime * 0.42f);
        impact += right * (centeredIndex * kVolleyImpactLateralSpacing);
        impact += forward * (-centeredIndex * kVolleyImpactForwardSpacing);
        volleyImpactCenters_[index] = GroundEffectLocator::ResolveGroundPosition(impact);
    }
}

void EnemyWindSlime::ShowVolleyImpactTelegraphs(float progress) {
    // 発射済みの弾の予告は消し、残りの着弾点だけ表示する。
    if (volleyShotCount_ >= kVolleyOrbCount) {
        HideAttackTelegraph();
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    const std::size_t firstIndex = static_cast<std::size_t>((std::max)(0, volleyShotCount_));
    ShowAttackTelegraphImpactAreas(volleyImpactCenters_.data() + firstIndex, volleyImpactCenters_.size() - firstIndex,
                                   attack.radius * 1.48f, progress, {0.48f, 1.0f, 0.82f, 0.80f});
}

float EnemyWindSlime::ResolveVolleyHoverY() {
    // 頭上を球で調べ、天井と重ならない高さまで浮遊目標を下げる。
    constexpr float kProbeHeight = 0.92f;
    const engine::Vector3 origin = GetWorldPosition() + engine::Vector3{0.0f, kProbeHeight, 0.0f};
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground");
    filter.ignoredObject = this;
    filter.ignoreDescendants = true;
    filter.includeTriggers = false;
    const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(
        origin, 0.46f, {0.0f, 1.0f, 0.0f}, kVolleyPreferredHoverHeight + kVolleyCeilingClearance, filter);

    float hoverHeight = kVolleyPreferredHoverHeight;
    if (hit.isHit) {
        const float availableHeight = kProbeHeight + hit.distance - kVolleyCeilingClearance;
        hoverHeight = (std::min)(hoverHeight, (std::max)(0.42f, availableHeight));
    }
    return GetTranslate().y + hoverHeight;
}

void EnemyWindSlime::HoldVolleyAltitude(float height, float deltaTime, engine::Vector3& velocity) {
    // 滞空フェーズでは高さを演出側が決め、地上接触扱いを解除する。
    engine::Vector3 position = GetTranslate();
    position.y = height;
    SetTranslate(position);
    velocity.y = param_.has_value() ? param_->gravity * deltaTime : 0.0f;
    SetGrounded(false);
}

void EnemyWindSlime::EnsureHeldWindOrbs() {
    // 発射前の三つの風弾は表示専用とし、接触判定を持つ実弾は発射時に作る。
    if (!common_) {
        return;
    }
    for (int index = 0; index < static_cast<int>(heldWindOrbVisuals_.size()); ++index) {
        if (heldWindOrbVisuals_[index]) {
            continue;
        }
        auto orb = std::make_unique<engine::Object3d>();
        orb->Initialize(common_);
        orb->SetName("WindSlime_HeldOrb_" + std::to_string(index));
        orb->SetModel("Primitives/sphere");
        orb->SetCollisionAttribute(0);
        orb->SetCollisionMask(0);
        orb->SetMaterialType(kWindOrbMaterialType);
        orb->SetBlendMode(BlendMode::kNormal);
        orb->SetColor({0.46f, 1.0f, 0.82f, 0.94f});
        orb->SetEmissive(1.8f);
        orb->SetMetallic(0.0f);
        orb->SetRoughness(0.24f);
        if (MeshRenderer* renderer = orb->GetMeshRenderer()) {
            if (auto* param = renderer->GetWaterParamData()) {
                param->waveSpeed = 2.35f;
                param->waveHeight = 0.62f;
                param->waveFrequency = 11.0f;
                param->effectType = static_cast<float>(index) * 0.5f;
                param->effectScale = 0.88f;
                param->effectSoftness = 0.36f;
                param->effectIntensity = 1.58f;
            }
        }
        orb->SetScale({kVolleyOrbVisualScale, kVolleyOrbVisualScale, kVolleyOrbVisualScale});
        orb->SetIsVisible(false);
        heldWindOrbVisuals_[index] = std::move(orb);
    }
}

void EnemyWindSlime::UpdateHeldWindOrbs(float deltaTime, float appearProgress) {
    // 弾を順に可視化し、発射済みの位置は隠して残数を見た目へ反映する。
    EnsureHeldWindOrbs();
    volleyVisualTimer_ -= deltaTime;
    for (int index = 0; index < static_cast<int>(heldWindOrbVisuals_.size()); ++index) {
        engine::Object3d* orb = heldWindOrbVisuals_[index].get();
        if (!orb) {
            continue;
        }
        const float appearThreshold = static_cast<float>(index) / static_cast<float>(kVolleyOrbCount);
        const bool isReady = appearProgress >= appearThreshold && index >= volleyShotCount_;
        orb->SetIsVisible(isReady);
        if (!isReady) {
            continue;
        }

        const float localAppear =
            SmoothStep01((appearProgress - appearThreshold) * static_cast<float>(kVolleyOrbCount));
        const float pulse = 1.0f + std::sin(idleTimer_ * 9.0f + static_cast<float>(index) * 1.8f) * 0.07f;
        const float scale = kVolleyOrbVisualScale * (0.32f + localAppear * 0.68f) * pulse;
        const engine::Vector3 position = ComputeHeldWindOrbPosition(index);
        orb->SetTranslate(position);
        orb->SetScale({scale, scale, scale});
        orb->SetRotation({idleTimer_ * 1.8f, idleTimer_ * 2.7f + static_cast<float>(index), idleTimer_ * 1.2f});
        orb->Update(deltaTime);

        if (volleyVisualTimer_ <= 0.0f) {
            const engine::Vector3 tangent =
                NormalizePlanar(lockedAttackDirection_ + engine::Vector3{static_cast<float>(index - 1) * 0.28f, 0.22f,
                                                                 static_cast<float>(1 - index) * 0.18f});
            EmitDirectedWindPreset(kOrbHoldPreset, position, tangent, 0.88f + static_cast<float>(index) * 0.08f);
        }
    }
    if (volleyVisualTimer_ <= 0.0f) {
        volleyVisualTimer_ = 0.075f;
    }
}

void EnemyWindSlime::HideHeldWindOrbs() {
    // 攻撃中断・着地時に表示用の弾だけを隠す。
    for (auto& orb : heldWindOrbVisuals_) {
        if (orb) {
            orb->SetIsVisible(false);
        }
    }
}

engine::Vector3 EnemyWindSlime::ComputeHeldWindOrbPosition(int orbIndex) const {
    // 三つの風弾を進行方向に対して左右へ並べ、中央だけ少し高く置く。
    const engine::Vector3 direction = NormalizePlanar(lockedAttackDirection_);
    const engine::Vector3 right = {direction.z, 0.0f, -direction.x};
    const float sideOffset = static_cast<float>(orbIndex - 1) * 0.92f;
    const float centerLift = orbIndex == 1 ? 0.18f : 0.0f;
    const float bob = std::sin(idleTimer_ * 5.8f + static_cast<float>(orbIndex) * 1.9f) * 0.10f;
    return GetWorldPosition() + right * sideOffset + direction * 0.18f + engine::Vector3{0.0f, 1.82f + centerLift + bob, 0.0f};
}

void EnemyWindSlime::DispatchEnemyBreathPush(const engine::Vector3& direction, float distance) {
    // 射程だけでなく向きも確認し、近距離ほど強い外力を標的へ与える。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
    Character* character = dynamic_cast<Character*>(target_);
    if (!character || distance > attack.maxRange + 0.75f) {
        return;
    }

    const engine::Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
    const engine::Vector3 targetDirection = NormalizePlanar(toTarget);
    const float facingDot = engine::Math::Dot(direction, targetDirection);
    if (facingDot < 0.24f) {
        return;
    }

    const float falloff = 1.0f - std::clamp(distance / (attack.maxRange + 0.75f), 0.0f, 1.0f);
    const float planarForce = 22.0f + falloff * 8.0f;
    character->ApplyExternalImpulse({direction.x * planarForce, 6.0f + falloff * 2.5f, direction.z * planarForce},
                                    0.24f);
    EmitDirectedWindPreset(kImpactPreset, target_->GetWorldPosition() + engine::Vector3{0.0f, 0.72f, 0.0f}, direction, 1.15f);
}

void EnemyWindSlime::BeginThrown(const engine::Vector3& initialVelocity) {
    // 風攻撃を止めて予告と手持ち弾を消し、投擲時の判定半径へ切り替える。
    EnsureBaseScale();
    attackState_ = AttackState::Idle;
    attackStateTimer_ = 0.0f;
    warningTriggered_ = false;
    HideAttackTelegraph();
    HideHeldWindOrbs();
    SetScale(baseScale_);
    SyncThrownCollisionRadius();
    BaseEnemy::BeginThrown(initialVelocity);
}

std::unique_ptr<engine::Object3d> EnemyWindSlime::Clone() const {
    // 複製先を初期化してから配置設定と対象を引き継ぐ。
    auto clone = std::make_unique<EnemyWindSlime>();
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyWindSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    // プレビュー対象の変更時は前の攻撃を破棄し、選択した技をすぐ確認できるようにする。
    debugPreviewAttackId_ = attackId;
    attackCooldown_ = 0.0f;
    attackState_ = AttackState::Idle;
    attackStateTimer_ = 0.0f;
    volleyShotCount_ = 0;
    HideHeldWindOrbs();
}

const char* EnemyWindSlime::GetDebugAttackPhaseName() const {
    // 実行中の段階をプレビューのタイムラインに表示する名前へ変換する。
    switch (attackState_) {
    case AttackState::GustWindup:
        return "暴風ブレス・溜め";
    case AttackState::GustActive:
        return "暴風ブレス・吹き飛ばし中";
    case AttackState::VolleyTakeoff:
        return "三連風弾・風で上昇";
    case AttackState::VolleyActive:
        return volleyShotCount_ == 0 ? "三連風弾・頭上に形成" : "三連風弾・順番に投射";
    case AttackState::VolleyLanding:
        return "三連風弾・着地";
    case AttackState::Recover:
        return "攻撃後・反動";
    default:
        return attackCooldown_ > 0.0f ? "再使用待ち" : "待機・移動";
    }
}

void EnemyWindSlime::ApplyManagedScale(const engine::Vector3& scale) {
    // エディターの大きさ変更を攻撃中の変形基準にも反映する。
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
}

void EnemyWindSlime::UpdateFacing(const engine::Vector3& direction) {
    // モデルの正面補正を加え、標的方向へ最短角で振り向く。
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z) + kWindSlimeModelYawOffset;
    SetRotationY(engine::Math::LerpShortAngle(GetRotation().y, targetYaw, 0.18f));
}

void EnemyWindSlime::ApplySlimeAnimation(float deltaTime) {
    // ブレス・浮遊・発射反動・着地の姿勢を状態別に作り、通常の跳ね姿勢へ補間する。
    engine::Vector3 targetScale = baseScale_;
    engine::Vector3 targetRotation = {0.0f, GetRotation().y, 0.0f};
    if (attackState_ == AttackState::GustWindup) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
        const float duration = (std::max)(0.01f, attack.windupDuration);
        const float charge = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float flutter = std::sin(charge * 18.8495559f) * 0.035f * charge;
        targetScale = {
            baseScale_.x * (1.0f + charge * 0.28f + flutter),
            baseScale_.y * (1.0f - charge * 0.24f),
            baseScale_.z * (1.0f + charge * 0.18f - flutter),
        };
        targetRotation.z = flutter * 1.4f;
    } else if (attackState_ == AttackState::VolleyTakeoff) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.01f, attack.windupDuration);
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float launchSquash = std::sin(std::clamp(progress / 0.46f, 0.0f, 1.0f) * 3.14159265f) * 0.19f;
        const float airborneStretch = std::sin(progress * 3.14159265f) * 0.17f;
        targetScale = {
            baseScale_.x * (1.0f + launchSquash - airborneStretch * 0.42f),
            baseScale_.y * (1.0f - launchSquash * 0.58f + airborneStretch),
            baseScale_.z * (1.0f + launchSquash - airborneStretch * 0.42f),
        };
        targetRotation.x = -std::sin(progress * 3.14159265f) * 0.10f;
    } else if (attackState_ == AttackState::VolleyActive) {
        const float hoverWave = std::sin(idleTimer_ * 6.2f);
        const float recoilRate = std::clamp(volleyRecoilTimer_ / 0.18f, 0.0f, 1.0f);
        const float recoil = std::sin((1.0f - recoilRate) * 3.14159265f) * 0.16f;
        targetScale = {
            baseScale_.x * (1.0f + hoverWave * 0.045f + recoil),
            baseScale_.y * (1.0f - hoverWave * 0.035f - recoil * 0.54f),
            baseScale_.z * (1.0f - hoverWave * 0.025f + recoil * 0.36f),
        };
        targetRotation.x = -recoil * 0.72f;
        targetRotation.z = hoverWave * 0.055f;
    } else if (attackState_ == AttackState::VolleyLanding) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.16f, attack.recoveryDuration);
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float fallingStretch = std::sin(progress * 3.14159265f) * 0.22f;
        targetScale = {
            baseScale_.x * (1.0f - fallingStretch * 0.38f),
            baseScale_.y * (1.0f + fallingStretch),
            baseScale_.z * (1.0f - fallingStretch * 0.38f),
        };
        targetRotation.x = 0.08f * progress;
    } else if (attackState_ == AttackState::GustActive) {
        const float activeDuration = (std::max)(0.01f, GetAttackDefinition(kGustAttackId).activeDuration);
        const float progress = 1.0f - std::clamp(attackStateTimer_ / activeDuration, 0.0f, 1.0f);
        const float pulse = std::sin(progress * 31.4159265f) * 0.045f;
        targetScale = {
            baseScale_.x * (1.12f + pulse),
            baseScale_.y * (0.78f - pulse * 0.35f),
            baseScale_.z * (1.38f - pulse * 0.60f),
        };
        targetRotation.x = -0.08f;
    } else if (attackState_ == AttackState::Recover) {
        const float duration = (std::max)(0.01f, recoveryDuration_ > 0.0f ? recoveryDuration_ : 0.28f);
        const float remaining = std::clamp(attackStateTimer_ / duration, 0.0f, 1.0f);
        const float rebound = std::sin((1.0f - remaining) * 3.14159265f);
        targetScale = {
            baseScale_.x * (1.0f - rebound * 0.09f),
            baseScale_.y * (1.0f + rebound * 0.16f),
            baseScale_.z * (1.0f + rebound * 0.12f),
        };
    } else {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 1.9f;
        params.idleAmplitude = 0.09f;
        params.moveAmplitude = 0.31f;
        params.hopFrequency = 11.4f;
        params.horizontalSquash = 0.30f;
        params.verticalStretch = 0.39f;
        params.airborneStretch = 0.40f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    }
    ApplyDamageReactionPose(targetScale, targetRotation);
    const float rate = (std::min)(1.0f, deltaTime * 13.0f);
    SetScale(engine::Math::Lerp(GetScale(), targetScale, rate));
    const engine::Vector3 currentRotation = GetRotation();
    SetRotation({
        currentRotation.x + (targetRotation.x - currentRotation.x) * rate,
        currentRotation.y,
        currentRotation.z + (targetRotation.z - currentRotation.z) * rate,
    });
}

void EnemyWindSlime::EmitWindBreathParticles(const engine::Vector3& origin, const engine::Vector3& direction, float range) {
    // 距離と横揺れをずらして粒子を並べ、連続した風の流れに見せる。
    const float step = (static_cast<float>(breathParticleCursor_ % kWindAnimationPhaseCount) + 0.5f) /
                       static_cast<float>(kWindAnimationPhaseCount);
    const engine::Vector3 planarDirection = NormalizePlanar(direction);
    const engine::Vector3 side = {-planarDirection.z, 0.0f, planarDirection.x};
    const float lateral = std::sin(idleTimer_ * 11.0f + step * 15.0f) * (0.08f + step * 0.34f);
    engine::Vector3 position = origin + planarDirection * (0.34f + step * range * 0.86f) + side * lateral;
    position.y += std::sin(idleTimer_ * 8.0f + step * 9.1f) * 0.12f;
    engine::Vector3 streamDirection = planarDirection + engine::Vector3{0.0f, 0.06f + step * 0.06f, 0.0f};
    streamDirection = engine::Math::Normalize(streamDirection);
    EmitDirectedWindPreset(kBreathStreamPreset, position, streamDirection, 1.25f + step * 1.05f);

    if (breathParticleCursor_ % 3 == 0) {
        const engine::Vector3 dustPosition = origin + planarDirection * (range * (0.48f + step * 0.42f));
        EmitDirectedWindPreset(kBreathDustPreset, dustPosition, streamDirection, 1.0f + step * 0.65f);
    }
    ++breathParticleCursor_;
}

void EnemyWindSlime::EmitWindPreset(const char* presetName, const engine::Vector3& position) {
    // GPU粒子が未初期化なら演出だけ省き、攻撃処理は続ける。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyWindSlime::EmitDirectedWindPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                                            float speedScale) {
    // 吹き出す方向と速度倍率をプリセットへ渡し、攻撃ごとに粒子の向きを揃える。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyWindSlime::SyncWorldCollisionRadius(float worldRadius) {
    // 見た目の伸縮を打ち消す局所半径へ換算し、ワールド上の当たり幅を維持する。
    const engine::Vector3 scale = GetScale();
    const float maxScale = (std::max)({0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    SetCollisionRadius(worldRadius / maxScale);
}

void EnemyWindSlime::SyncGroundCollisionRadius() {
    // 地上行動中は通常の接触半径を使う。
    SyncWorldCollisionRadius(kGroundCollisionWorldRadius);
}

void EnemyWindSlime::SyncThrownCollisionRadius() {
    // 投擲中は回転した体を拾いやすい半径へ広げる。
    SyncWorldCollisionRadius(kThrownCollisionWorldRadius);
}
