#include "CopyAbilitySupport.h"

// 入力・更新・キャンセルの順序と調整値は元実装を維持します。
namespace {
using namespace CopyAbilityDetail;
class PinkSlimeCopyAbility final : public ICopyAbilitySession {
public:
    explicit PinkSlimeCopyAbility(EnemyAttackProfile profile) : ICopyAbilitySession(std::move(profile)) {
        if (auto* effects = MeshEffectManager::GetInstance()) guardScope_ = effects->CreateEffectScope();
    }
    ~PinkSlimeCopyAbility() override { StopGuardVisuals(); }
    const char* GetStateLabel() const override {
        switch (state_) {
        case State::Idle: return "待機";
        case State::Guard: return "ガード中";
        case State::ElasticStrike: return comboStage_ == 2 ? "近接3段目: プレス" : (comboStage_ == 1 ? "近接2段目: 切り返し" : "近接1段目: ボディ打撃");
        case State::BounceCharge: return "E: 溜め";
        case State::BounceRise: return "E: 跳ね上がり";
        case State::BounceRush: return "E: 空中突進";
        case State::BounceRecovery: return "E: 着地硬直";
        }
        return "待機";
    }

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        guardHeld_ = input.secondaryHeld;
        if (state_ == State::ElasticStrike) {
            // 長押しではなく追加クリックを1段分だけ予約します。
            if (input.primaryTriggered && comboStage_ < 2 && duration_ - timer_ >= 0.10f) comboQueued_ = true;
            return;
        }
        if (input.specialTriggered) {
            bounceInputBuffer_ = kBounceInputBufferDuration;
        }
        if (state_ != State::Idle) {
            return;
        }
        if (input.secondaryTriggered && player.IsGrounded() && guardCooldown_ <= 0.0f) {
            state_ = State::Guard;
            guardTimer_ = 1.25f;
            guardHitMotionTimer_ = 0.0f;
            effectTimer_ = 0.0f;
            direction_ = NormalizePlanar(player.GetForwardDirection());
            player.SetVelocity({ 0.0f, player.GetVelocity().y, 0.0f });
            player.SetIsControlActive(false);
            player.SetGuardInvincible(true);
            player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::Guard);
            player.PlaySlimeAbilityMotion("player_ability_pink_guard", true, 1.0f, 0.05f, 0.08f);
            UpdateGuardVisuals(player);
            return;
        }
        if (bounceInputBuffer_ > 0.0f && CanStartBounceRush(player)) {
            BeginBounceCharge(player);
            return;
        }
        if (input.primaryTriggered && straightCooldown_ <= 0.0f && player.IsGrounded()) {
            // 短く踏み込んで全身を横に振り抜く地上打撃。長距離移動のEとは分離します。
            BeginElasticStrike(player);
            straightCooldown_ = 0.68f;
        }
    }

    void Update(Player& player, float deltaTime) override {
        straightCooldown_ = std::max(0.0f, straightCooldown_ - deltaTime);
        bounceCooldown_ = std::max(0.0f, bounceCooldown_ - deltaTime);
        guardCooldown_ = std::max(0.0f, guardCooldown_ - deltaTime);
        guardImpactCooldown_ = std::max(0.0f, guardImpactCooldown_ - deltaTime);
        bounceInputBuffer_ = std::max(0.0f, bounceInputBuffer_ - deltaTime);
        groundedGraceTimer_ = player.IsGrounded()
            ? kBounceGroundedGraceDuration
            : std::max(0.0f, groundedGraceTimer_ - deltaTime);
        visualTime_ += deltaTime;

        // 入力猶予は入力したフレームだけでなく、着地するまで毎フレーム再評価します。
        // これにより段差際や着地直前に押したEが消える不具合を防ぎます。
        if (state_ == State::Idle && bounceInputBuffer_ > 0.0f && CanStartBounceRush(player)) {
            BeginBounceCharge(player);
            return;
        }

        const bool guardHitMotionWasActive = guardHitMotionTimer_ > 0.0f;
        guardHitMotionTimer_ = std::max(0.0f, guardHitMotionTimer_ - deltaTime);
        if (guardHitMotionWasActive && guardHitMotionTimer_ <= 0.0f && state_ == State::Guard) {
            player.PlaySlimeAbilityMotion("player_ability_pink_guard", true,
                1.0f, 0.035f, 0.08f);
        }
        effectTimer_ -= deltaTime;
        storedPowerEffectTimer_ -= deltaTime;

        // ガード成功で蓄えた弾性を、次のE入力まで小さな脈動として見せます。
        // UIを見なくても強化が残っていることをプレイヤー本体から読み取れます。
        if (state_ == State::Idle && storedBouncePower_ > 0.0f && storedPowerEffectTimer_ <= 0.0f) {
            const float effectScale = 0.56f + storedBouncePower_ * 0.34f;
            SpawnEffect("Resources/json/effect/effect_pink_slime_charge_pulse_ring.json",
                player.GetWorldPosition() + Vector3{ 0.0f, 0.62f, 0.0f }, {},
                { effectScale, effectScale, effectScale });
            storedPowerEffectTimer_ = 0.32f;
        }

        if (state_ == State::Guard) {
            guardTimer_ = std::max(0.0f, guardTimer_ - deltaTime);
            if (!guardHeld_ || guardTimer_ <= 0.0f || !player.IsGrounded()) {
                Finish(player);
                return;
            }
            player.SetVelocity({ 0.0f, player.GetVelocity().y, 0.0f });
            player.SetIsControlActive(false);
            player.SetGuardInvincible(true);
            player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::Guard);
            player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Idle, direction_);
            if (effectTimer_ <= 0.0f) UpdateGuardVisuals(player);
            return;
        }

        if (state_ == State::BounceCharge) {
            timer_ = std::max(0.0f, timer_ - deltaTime);
            player.SetVelocity({ 0.0f, std::min(player.GetVelocity().y, 0.0f), 0.0f });
            player.SetIsControlActive(false);
            player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
            if (effectTimer_ <= 0.0f) {
                const float pulseScale = 0.72f + activeBouncePower_ * 0.28f;
                SpawnEffect("Resources/json/effect/effect_pink_slime_charge_pulse_ring.json",
                    player.GetWorldPosition() + Vector3{ 0.0f, 0.12f, 0.0f }, {},
                    { pulseScale, pulseScale, pulseScale });
                effectTimer_ = 0.065f;
            }
            if (timer_ <= 0.0f) {
                LaunchBounceRise(player);
            }
            return;
        }

        if (state_ == State::BounceRise) {
            timer_ = std::max(0.0f, timer_ - deltaTime);
            Vector3 velocity = player.GetVelocity();
            const float riseForwardSpeed = 6.2f + activeBouncePower_ * 1.4f;
            velocity.x = direction_.x * riseForwardSpeed;
            velocity.z = direction_.z * riseForwardSpeed;
            player.SetVelocity(velocity);
            player.SetIsControlActive(false);
            player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
            player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, direction_);

            if (timer_ <= 0.0f || velocity.y <= 7.5f) {
                BeginBounceRush(player);
            }
            return;
        }

        if (state_ == State::BounceRush) {
            timer_ = std::max(0.0f, timer_ - deltaTime);
            const float rushProgress = 1.0f - timer_ / std::max(duration_, 0.001f);
            const float rushSpeed = (34.0f + activeBouncePower_ * 6.0f) *
                (1.0f - SmoothStep(rushProgress) * 0.24f);
            Vector3 velocity = player.GetVelocity();
            velocity.x = direction_.x * rushSpeed;
            velocity.z = direction_.z * rushSpeed;
            const float rushGravity = 68.0f + activeBouncePower_ * 12.0f;
            const float rushTerminalDownwardSpeed = 34.0f + activeBouncePower_ * 6.0f;
            velocity.y = std::max(velocity.y - rushGravity * deltaTime, -rushTerminalDownwardSpeed);
            player.SetVelocity(velocity);
            player.SetIsControlActive(false);
            player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
            player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, direction_);
            DamageRush(player);

            if (effectTimer_ <= 0.0f) {
                const Vector3 trailPosition = player.GetWorldPosition() - direction_ * 0.62f +
                    Vector3{ 0.0f, 0.52f, 0.0f };
                SpawnEffect("Resources/json/effect/effect_player_pink_straight_arc.json",
                    trailPosition, { 0.0f, std::atan2(direction_.x, direction_.z), 0.0f },
                    { 0.88f, 0.78f, 1.12f });
                EmitDirectedPreset("player_pink_bounce_droplets", trailPosition,
                    direction_ * -1.0f + Vector3{ 0.0f, 0.16f, 0.0f }, 0.82f);
                effectTimer_ = 0.055f;
            }

            if (player.IsGrounded() && rushProgress > 0.06f) {
                FinishBounceRush(player);
            }
            else if (timer_ <= 0.0f) {
                // 地面へ届かなかった場合は空中で衝撃を発生させず、操作だけ安全に返します。
                Finish(player);
            }
            return;
        }

        if (state_ == State::BounceRecovery) {
            timer_ = std::max(0.0f, timer_ - deltaTime);
            if (timer_ <= 0.0f) {
                Finish(player);
            }
            return;
        }

        if (state_ != State::ElasticStrike) {
            return;
        }

        const float previousElapsed = duration_ - timer_;
        timer_ = std::max(0.0f, timer_ - deltaTime);
        const float elapsed = duration_ - timer_;
        const float hitStart = comboStage_ == 2 ? 0.40f : 0.22f;
        const float hitEnd = comboStage_ == 2 ? 0.49f : 0.36f;
        // 位置の直接書き換えはせず、通常の移動・壁衝突を通す短い踏み込み。
        const float stepOverlap = std::max(0.0f,
            std::min(elapsed, 0.34f) - std::max(previousElapsed, 0.18f));
        const float stepSpeed = (comboStage_ == 2 ? 0.0f : 8.0f) * stepOverlap / std::max(deltaTime, 0.0001f);
        Vector3 velocity = player.GetVelocity();
        velocity.x = direction_.x * stepSpeed;
        velocity.z = direction_.z * stepSpeed;
        if (player.IsGrounded() && velocity.y < 0.0f) velocity.y = 0.0f;
        player.SetVelocity(velocity);
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Idle, direction_);
        if (!strikeDidDamage_ && elapsed >= hitStart && player.IsGrounded()) {
            strikeDidDamage_ = true;
            // 空振りでは振り抜きの軌跡のみ。命中の閃光は実際のダメージ処理に任せます。
            const Vector3 origin = player.GetWorldPosition() + direction_ * 0.55f
                + Vector3{ 0.0f, comboStage_ == 2 ? 0.08f : 0.65f, 0.0f };
            const float effectYaw = std::atan2(direction_.x, direction_.z);
            if (comboStage_ == 2) {
                // 閉じた輪ではなく、打点から地面を叩き散らす短い立体の衝撃線。
                for (int i = 0; i < 7; ++i) {
                    const float yaw = effectYaw + (static_cast<float>(i) - 3.0f) * 0.48f;
                    const Vector3 offset{ std::sin(yaw) * 0.30f, 0.05f, std::cos(yaw) * 0.30f };
                    SpawnEffect("Resources/json/effect/effect_player_pink_combo_press.json", origin + offset,
                        { -0.20f - 0.08f * static_cast<float>(i % 2), yaw, 0.0f });
                }
                // 打点の縦方向にも短い衝撃を立て、地面の線だけに見えないようにします。
                for (int i = -1; i <= 1; ++i) {
                    SpawnEffect("Resources/json/effect/effect_player_pink_guard_impact.json", origin,
                        { -kPi * 0.5f + i * 0.30f, effectYaw, 0.0f }, { 1.3f, 1.3f, 1.8f });
                }
            } else {
                // Z軸半回転で三日月の先端と尾を反転。1段目は既存設定を維持。
                SpawnEffect(comboStage_ == 1 ? "Resources/json/effect/effect_player_pink_return_sweep.json" :
                    "Resources/json/effect/effect_player_pink_body_sweep.json", origin,
                    { 0.0f, effectYaw, comboStage_ == 1 ? kPi - 0.28f : 0.28f });
            }
            // 粒の散布ではなく、叩きつけた地面から短命の立体破片を出します。
            if (comboStage_ == 2) SpawnDebrisOnGround("player_pink_press_chips", origin);
        }
        if (elapsed >= hitStart && previousElapsed <= hitEnd && player.IsGrounded()) {
            // 有効時間中の前方薙ぎ払い。同じ敵には1振りにつき1回だけ命中します。
            const Vector3 scale = player.GetScale();
            const float bodySize = std::max(0.1f, std::max(std::abs(scale.x), std::abs(scale.z)));
            const Vector3 center = player.GetWorldPosition() + direction_ * (bodySize * 0.70f)
                + Vector3{ 0.0f, std::abs(scale.y) * 0.34f, 0.0f };
            PhysicsQueryFilter filter;
            filter.mask = kEnemy;
            filter.ignoredObject = &player;
            for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(
                center, bodySize * (comboStage_ == 2 ? 1.05f : 0.78f), filter)) {
                Object3d* target = FindEnemyRoot(hit.object);
                if (!target || !hitTargets_.insert(target).second) continue;
                const Vector3 push = NormalizePlanar(target->GetWorldPosition() - player.GetWorldPosition());
                DamageEnemy(player, target, rushDamage_, DamageType::Physical,
                    { push.x * (comboStage_ == 2 ? 10.0f : 4.0f), comboStage_ == 2 ? 5.0f : 1.0f, push.z * (comboStage_ == 2 ? 10.0f : 4.0f) });
            }
        }
        if (timer_ <= 0.0f) {
            if (comboQueued_ && comboStage_ < 2 && player.IsGrounded()) {
                BeginElasticStrike(player, comboStage_ + 1);
                return;
            }
            Finish(player);
        }
    }


    void Cancel(Player& player) override {
        player.ClearSlimeAbilityMotion();
        storedBouncePower_ = 0.0f;
        activeBouncePower_ = 0.0f;
        guardHitMotionTimer_ = 0.0f;
        Finish(player);
    }

private:
    enum class State { Idle, ElasticStrike, Guard, BounceCharge, BounceRise, BounceRush, BounceRecovery };

public:
    bool CanBreakImpactGate() const override {
        const float elapsed = duration_ - timer_;
        return (state_ == State::ElasticStrike && elapsed >= (comboStage_ == 2 ? 0.40f : 0.22f) && elapsed <= (comboStage_ == 2 ? 0.49f : 0.36f))
            || state_ == State::BounceRush;
    }

    bool IsPinkBounceSlamImpactActive() const override {
        return state_ == State::BounceRush;
    }

    bool NotifyGuardedHit(Player& player, const Vector3& sourcePosition) override {
        if (state_ != State::Guard || guardImpactCooldown_ > 0.0f) {
            return false;
        }

        guardImpactCooldown_ = 0.22f;
        guardHitMotionTimer_ = 0.24f;
        storedBouncePower_ = std::min(1.0f, storedBouncePower_ + 0.36f);
        direction_ = NormalizePlanar(sourcePosition - player.GetWorldPosition());
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.PlaySlimeAbilityMotion("player_ability_pink_guard_hit", false,
            1.0f, 0.015f, 0.045f);
        const AABB bounds = player.GetAABB();
        const float radius = std::max(0.35f, (bounds.max.x - bounds.min.x) * 0.5f);
        const Vector3 contact = player.GetWorldPosition() + direction_ * radius
            + Vector3{ 0.0f, (bounds.max.y - bounds.min.y) * 0.32f, 0.0f };
        const float yaw = std::atan2(direction_.x, direction_.z);
        for (int i = -1; i <= 1; ++i) {
            SpawnEffect("Resources/json/effect/effect_player_pink_guard_impact.json", contact,
                { -0.25f, yaw + i * 0.65f, 0.0f });
        }
        UpdateGuardVisuals(player);
        return true;
    }

private:
    void StopGuardVisuals() {
        if (guardScope_ != 0) {
            if (auto* effects = MeshEffectManager::GetInstance()) effects->StopEffectScope(guardScope_);
        }
    }

    void UpdateGuardVisuals(Player& player) {
        // 球体の膜と交差する輪郭で、どの視点からでも防御範囲が読めるバリアにします。
        StopGuardVisuals();
        const AABB bounds = player.GetModelWorldAABB();
        const float radius = std::max(0.35f,
            std::max(bounds.max.x - bounds.min.x, bounds.max.z - bounds.min.z) * 0.5f);
        const float height = bounds.max.y - bounds.min.y;
        const float yaw = std::atan2(direction_.x, direction_.z);
        const Vector3 position{ (bounds.min.x + bounds.max.x) * 0.5f,
            bounds.min.y + height * 0.34f, (bounds.min.z + bounds.max.z) * 0.5f };
        const Vector3 size{ radius * 1.62f, std::max(height * 0.64f, radius), radius * 1.62f };
        SpawnScopedEffect(guardScope_, "Resources/json/effect/effect_player_pink_guard_barrier.json", position, {}, size);
        SpawnScopedEffect(guardScope_, "Resources/json/effect/effect_player_pink_guard_barrier_rim.json", position,
            { 0.0f, yaw, 0.0f }, size);
        SpawnScopedEffect(guardScope_, "Resources/json/effect/effect_player_pink_guard_barrier_rim.json", position,
            { kPi * 0.5f, yaw, 0.0f }, { size.x, size.z, size.y });
        effectTimer_ = 0.12f;
    }

    bool CanStartBounceRush(Player& player) const {
        return bounceCooldown_ <= 0.0f &&
            (player.IsGrounded() || groundedGraceTimer_ > 0.0f);
    }

    void BeginElasticStrike(Player& player, int stage = 0) {
        state_ = State::ElasticStrike;
        comboStage_ = stage;
        comboQueued_ = false;
        duration_ = stage == 2 ? 0.86f : (stage == 1 ? 0.64f : 0.68f);
        straightCooldown_ = duration_;
        timer_ = duration_;
        direction_ = NormalizePlanar(player.GetLockOnAttackDirection());
        rushDamage_ = stage == 2 ? 18.0f : (stage == 1 ? 11.0f : 10.0f);
        effectTimer_ = 0.0f;
        strikeDidDamage_ = false;
        hitTargets_.clear();
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.SetVelocity({ 0.0f, player.GetVelocity().y, 0.0f });
        player.PlaySlimeAbilityMotion(stage == 2 ? "player_ability_pink_combo_press" : (stage == 1 ? "player_ability_pink_combo_return" : "player_ability_pink_elastic_strike"), false,
            1.0f, 0.025f, 0.055f);
    }


    void BeginBounceCharge(Player& player) {
        state_ = State::BounceCharge;
        bounceInputBuffer_ = 0.0f;
        direction_ = NormalizePlanar(player.GetLockOnAttackDirection());
        duration_ = 0.18f;
        timer_ = duration_;
        effectTimer_ = 0.0f;
        activeBouncePower_ = storedBouncePower_;
        storedBouncePower_ = 0.0f;
        storedPowerEffectTimer_ = 0.0f;
        hitTargets_.clear();
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.SetVelocity({ 0.0f, std::min(player.GetVelocity().y, 0.0f), 0.0f });
        player.PlaySlimeAbilityMotion("player_ability_pink_bounce_charge", false,
            1.0f, 0.015f, 0.035f);
        player.TriggerSlimeImpulse({ 2.82f, 0.74f, 2.82f }, 0.11f);
        SpawnDebrisOnGround("pink_slime_charge_pebble_pull", player.GetWorldPosition());
        bounceCooldown_ = 1.15f;
    }

    void LaunchBounceRise(Player& player) {
        player.SetGrounded(false);
        state_ = State::BounceRise;
        duration_ = 0.40f;
        timer_ = duration_;
        effectTimer_ = 0.0f;
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        const float launchForwardSpeed = 7.5f + activeBouncePower_ * 1.8f;
        const float launchVerticalSpeed = 19.5f + activeBouncePower_ * 4.0f;
        player.SetVelocity({ direction_.x * launchForwardSpeed,
            std::max(player.GetVelocity().y, launchVerticalSpeed),
            direction_.z * launchForwardSpeed });
        player.StartEvasionInvincibility(0.68f);
        player.PlaySlimeAbilityMotion("player_ability_pink_bounce_rise", false,
            1.0f, 0.018f, 0.045f);
        player.TriggerSlimeImpulse({ 0.72f, 1.70f, 0.72f }, 0.16f);
        SpawnEffect("Resources/json/effect/effect_player_pink_bounce_launch.json",
            player.GetWorldPosition() + Vector3{ 0.0f, 0.08f, 0.0f }, {},
            { 1.0f + activeBouncePower_ * 0.24f,
              1.0f + activeBouncePower_ * 0.24f,
              1.0f + activeBouncePower_ * 0.24f });
        EmitDirectedPreset("player_pink_bounce_droplets",
            player.GetWorldPosition() + Vector3{ 0.0f, 0.15f, 0.0f },
            { 0.0f, -1.0f, 0.0f }, 0.78f + activeBouncePower_ * 0.28f);
    }

    void BeginBounceRush(Player& player) {
        state_ = State::BounceRush;
        duration_ = 0.38f;
        timer_ = duration_;
        effectTimer_ = 0.0f;
        rushDamage_ = 16.0f + activeBouncePower_ * 5.0f;
        hitTargets_.clear();
        Vector3 velocity = player.GetVelocity();
        const float launchSpeed = 34.0f + activeBouncePower_ * 6.0f;
        velocity.x = direction_.x * launchSpeed;
        velocity.z = direction_.z * launchSpeed;
        velocity.y = std::max(velocity.y, 3.8f);
        player.SetVelocity(velocity);
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.SetDashInvincible(true);
        player.StartEvasionInvincibility(duration_ + 0.12f);
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, direction_);
        player.PlaySlimeAbilityMotion("player_ability_pink_rush", false,
            0.78f / duration_, 0.02f, 0.05f);
        player.TriggerSlimeImpulse({ 1.56f, 1.04f, 1.56f }, 0.12f);
        SpawnEffect("Resources/json/effect/effect_pink_slime_apex_focus_flash.json",
            player.GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f }, {},
            { 0.82f, 0.82f, 0.82f });
    }

    void FinishBounceRush(Player& player) {
        const Vector3 groundPosition = HitEffectDirector::ResolveGroundEffectPosition(player.GetWorldPosition());
        if (player.IsGrounded()) {
            SpawnDebrisOnGround("pink_slime_landing_pebble_burst", groundPosition);
            SpawnEffect("Resources/json/effect/effect_player_pink_straight_impact.json",
                groundPosition + Vector3{ 0.0f, 0.10f, 0.0f }, {}, { 0.72f, 0.72f, 0.72f });
            player.TriggerSlimeImpulse({ 2.36f, 0.42f, 2.36f }, 0.12f);
        }
        state_ = State::BounceRecovery;
        timer_ = 0.18f;
    }

    void DamageRush(Player& player) {
        PhysicsQueryFilter filter;
        filter.mask = kEnemy;
        filter.ignoredObject = &player;
        const Vector3 center = player.GetWorldPosition() + Vector3{ 0.0f, 0.68f, 0.0f };
        for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, 1.35f, filter)) {
            Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !hitTargets_.insert(target).second) {
                continue;
            }
            DamageEnemy(player, target, rushDamage_, DamageType::Physical,
                { direction_.x * 12.0f, 5.5f, direction_.z * 12.0f });
            SpawnEffect("Resources/json/effect/effect_player_pink_straight_impact.json",
                target->GetWorldPosition() + Vector3{ 0.0f, 0.55f, 0.0f });
        }
    }

    void Finish(Player& player) {
        if (state_ == State::Idle) {
            return;
        }
        if (state_ == State::Guard) {
            StopGuardVisuals();
            // 防御中に再使用待ちを消化させず、解除後に必ず隙を作ります。
            guardCooldown_ = std::max(guardCooldown_, 0.82f);
        }
        player.SetGuardInvincible(false);
        player.SetDashInvincible(false);
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
        player.StopSlimeAbilityMotion(0.06f);
        player.SetIsControlActive(true);
        Vector3 velocity = player.GetVelocity();
        velocity.x *= 0.30f;
        velocity.z *= 0.30f;
        player.SetVelocity(velocity);
        state_ = State::Idle;
        timer_ = 0.0f;
        guardTimer_ = 0.0f;
        guardHitMotionTimer_ = 0.0f;
        activeBouncePower_ = 0.0f;
        diving_ = false;
        hitTargets_.clear();
    }

    State state_ = State::Idle;
    MeshEffectManager::EffectScopeId guardScope_ = 0;
    Vector3 direction_{ 0.0f, 0.0f, 1.0f };
    std::unordered_set<Object3d*> hitTargets_;
    float timer_ = 0.0f;
    float duration_ = 0.0f;
    float startSpeed_ = 0.0f;
    float effectTimer_ = 0.0f;
    float straightCooldown_ = 0.0f;
    float bounceCooldown_ = 0.0f;
    float guardCooldown_ = 0.0f;
    float guardTimer_ = 0.0f;
    float guardImpactCooldown_ = 0.0f;
    float guardHitMotionTimer_ = 0.0f;
    float storedPowerEffectTimer_ = 0.0f;
    float storedBouncePower_ = 0.0f;
    float activeBouncePower_ = 0.0f;
    float bounceInputBuffer_ = 0.0f;
    float groundedGraceTimer_ = 0.0f;
    float visualTime_ = 0.0f;
    float rushDamage_ = 10.0f;
    bool guardHeld_ = false;
    bool diving_ = false;
    bool strikeDidDamage_ = false;
    int comboStage_ = 0;
    bool comboQueued_ = false;

    static constexpr float kBounceInputBufferDuration = 0.36f;
    static constexpr float kBounceGroundedGraceDuration = 0.16f;
};
}

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreatePinkSlimeCopyAbility(const EnemyAttackProfile& profile) {
    return std::make_unique<PinkSlimeCopyAbility>(profile);
}
}
