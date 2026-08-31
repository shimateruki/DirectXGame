#define NOMINMAX
#include "PlayerCopyAbilityController.h"

#include "BaseEnemy.h"
#include "BaseScene.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EnemyAttackProfile.h"
#include "EnemyBomb.h"
#include "EnemyFactory.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "PlayerState.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <utility>

namespace {
constexpr float kPi = 3.1415926535f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

float SmoothStep(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

void SpawnEffect(const char* path, const Vector3& position, const Vector3& rotation = {},
    const Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAt(path, position, rotation, scale);
    }
}

void SpawnScopedEffect(MeshEffectManager::EffectScopeId scopeId, const char* path,
    const Vector3& position, const Vector3& rotation = {},
    const Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAtScoped(scopeId, path, position, rotation, scale);
    }
}

void EmitPreset(const char* preset, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized()) {
        particles->Emit(preset, position);
    }
}

void EmitDirectedPreset(const char* preset, const Vector3& position, const Vector3& direction, float scale = 1.0f) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized()) {
        particles->EmitDirected(preset, position, direction, scale);
    }
}

Object3d* FindEnemyRoot(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}

void DamageEnemy(Player& player, Object3d* target, float damage, DamageType type,
    const Vector3& knockback, const StatusEffectApplication& status = {}) {
    if (!target) {
        return;
    }
    DamageEvent event;
    event.target = target;
    event.attacker = &player;
    event.damageAmount = damage;
    event.damageType = type;
    event.knockbackVelocity = knockback;
    event.statusEffect = status;
    EventManager::GetInstance()->Dispatch(event);
}

void DamageSphere(Player& player, const Vector3& center, float radius, float damage, DamageType type,
    float horizontalKnockback, float verticalKnockback) {
    PhysicsQueryFilter filter;
    filter.mask = kEnemy;
    filter.ignoredObject = &player;
    std::unordered_set<Object3d*> damaged;
    for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, radius, filter)) {
        Object3d* target = FindEnemyRoot(hit.object);
        if (!target || !damaged.insert(target).second) {
            continue;
        }
        const Vector3 direction = NormalizePlanar(target->GetWorldPosition() - center);
        DamageEnemy(player, target, damage, type,
            { direction.x * horizontalKnockback, verticalKnockback, direction.z * horizontalKnockback });
    }
}

const EnemyAttackDefinition& FindAttackOrFallback(
    const EnemyAttackProfile& profile, const char* id, const EnemyAttackDefinition& fallback) {
    const EnemyAttackDefinition* attack = profile.FindAttack(id);
    return attack ? *attack : fallback;
}

StatusEffectApplication MakeBurnStatus(const EnemyAttackDefinition& attack) {
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

BulletVisualConfig MakeFireVisual() {
    BulletVisualConfig visual;
    visual.materialType = 11;
    visual.color = { 1.0f, 0.34f, 0.07f, 0.96f };
    visual.emissive = 2.8f;
    visual.visualScale = 1.28f;
    visual.effectType = 1.0f;
    visual.effectScale = 1.15f;
    visual.effectSoftness = 0.42f;
    visual.effectIntensity = 0.96f;
    visual.billboardScale = 0.68f;
    return visual;
}

class ICopyAbilitySession {
public:
    explicit ICopyAbilitySession(EnemyAttackProfile profile) : profile_(std::move(profile)) {}
    virtual ~ICopyAbilitySession() = default;
    virtual void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) = 0;
    virtual void Update(Player& player, float deltaTime) = 0;
    virtual void Cancel(Player& player) = 0;
    virtual bool NotifyGuardedHit(Player&, const Vector3&) { return false; }
    virtual bool CanBreakImpactGate() const { return false; }
    virtual bool IsPinkBounceSlamImpactActive() const { return false; }

protected:
    EnemyAttackProfile profile_;
};

class PinkSlimeCopyAbility final : public ICopyAbilitySession {
public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        guardHeld_ = input.secondaryHeld;
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
            player.TriggerSlimeImpulse({ 2.72f, 1.20f, 2.72f }, 0.14f);
            SpawnEffect("Resources/json/effect/effect_player_pink_guard_start.json",
                player.GetWorldPosition() + Vector3{ 0.0f, 0.66f, 0.0f });
            return;
        }
        if (input.specialTriggered && player.IsGrounded() && bounceCooldown_ <= 0.0f) {
            BeginBounceCharge(player);
            return;
        }
        if (input.primaryTriggered && straightCooldown_ <= 0.0f) {
            // 左クリックは空中でも前方直進に統一し、Eの落下攻撃と役割を重ねません。
            BeginRush(player, player.IsGrounded() ? 0.28f : 0.34f,
                player.IsGrounded() ? 31.0f : 26.0f, false,
                player.IsGrounded() ? 10.0f : 11.0f);
            straightCooldown_ = 0.62f;
        }
    }

    void Update(Player& player, float deltaTime) override {
        straightCooldown_ = std::max(0.0f, straightCooldown_ - deltaTime);
        bounceCooldown_ = std::max(0.0f, bounceCooldown_ - deltaTime);
        guardCooldown_ = std::max(0.0f, guardCooldown_ - deltaTime);
        guardImpactCooldown_ = std::max(0.0f, guardImpactCooldown_ - deltaTime);
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
                SpawnEffect("Resources/json/effect/effect_player_pink_guard_release.json",
                    player.GetWorldPosition() + Vector3{ 0.0f, 0.62f, 0.0f });
                Finish(player);
                return;
            }
            player.SetVelocity({ 0.0f, player.GetVelocity().y, 0.0f });
            player.SetIsControlActive(false);
            player.SetGuardInvincible(true);
            player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::Guard);
            if (effectTimer_ <= 0.0f) {
                SpawnEffect("Resources/json/effect/effect_player_pink_guard_shell.json",
                    player.GetWorldPosition() + Vector3{ 0.0f, 0.66f, 0.0f });
                effectTimer_ = 0.085f;
            }
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
                LaunchBounceSlam(player);
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

            if (timer_ <= 0.0f || velocity.y <= 3.5f) {
                state_ = State::BounceFall;
                duration_ = 1.25f;
                timer_ = duration_;
                velocity.y = std::min(velocity.y, -5.5f);
                player.SetVelocity(velocity);
                player.TriggerSlimeImpulse({ 1.30f, 0.54f, 1.30f }, 0.12f);
                player.PlaySlimeAbilityMotion("player_ability_pink_bounce_fall", true,
                    1.0f, 0.025f, 0.055f);
                SpawnEffect("Resources/json/effect/effect_pink_slime_apex_focus_flash.json",
                    player.GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f }, {},
                    { 0.72f, 0.72f, 0.72f });
                effectTimer_ = 0.0f;
            }
            return;
        }

        if (state_ == State::BounceFall) {
            timer_ = std::max(0.0f, timer_ - deltaTime);
            Vector3 velocity = player.GetVelocity();
            const float fallForwardSpeed = 3.8f + activeBouncePower_ * 1.0f;
            velocity.x = direction_.x * fallForwardSpeed;
            velocity.z = direction_.z * fallForwardSpeed;
            const float fallAcceleration = 68.0f + activeBouncePower_ * 12.0f;
            const float terminalSpeed = 34.0f + activeBouncePower_ * 6.0f;
            velocity.y = std::max(velocity.y - fallAcceleration * deltaTime, -terminalSpeed);
            player.SetVelocity(velocity);
            player.SetIsControlActive(false);
            player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
            player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, direction_);

            if (effectTimer_ <= 0.0f) {
                const Vector3 trailPosition = player.GetWorldPosition() - direction_ * 0.28f +
                    Vector3{ 0.0f, 0.82f, 0.0f };
                SpawnEffect("Resources/json/effect/effect_pink_slime_dive_streak.json",
                    trailPosition, { 0.0f, std::atan2(direction_.x, direction_.z), 0.0f },
                    { 0.78f, 0.92f, 0.78f });
                EmitDirectedPreset("player_pink_bounce_droplets", trailPosition,
                    Vector3{ 0.0f, 1.0f, 0.0f }, 0.68f);
                effectTimer_ = 0.075f;
            }

            const float fallProgress = 1.0f - timer_ / std::max(duration_, 0.001f);
            if (player.IsGrounded() && fallProgress > 0.06f) {
                LandBounceSlam(player);
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

        if (state_ != State::Rush) {
            return;
        }

        timer_ = std::max(0.0f, timer_ - deltaTime);
        const float progress = 1.0f - timer_ / std::max(duration_, 0.001f);
        const float speed = startSpeed_ * (1.0f - SmoothStep(progress) * 0.68f);
        Vector3 velocity = player.GetVelocity();
        velocity.x = direction_.x * speed;
        velocity.z = direction_.z * speed;
        if (diving_) {
            velocity.y = std::min(velocity.y, -10.0f - progress * 24.0f);
        }
        else if (player.IsGrounded() && velocity.y < 0.0f) {
            velocity.y = 0.0f;
        }
        player.SetVelocity(velocity);
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, direction_);
        DamageRush(player);
        if (effectTimer_ <= 0.0f) {
            const Vector3 position = player.GetWorldPosition() - direction_ * 0.55f + Vector3{ 0.0f, 0.42f, 0.0f };
            SpawnEffect("Resources/json/effect/effect_player_pink_straight_arc.json", position,
                { 0.0f, std::atan2(direction_.x, direction_.z), 0.0f }, { 0.72f, 0.72f, 0.96f });
            EmitPreset("player_pink_bounce_droplets", position);
            effectTimer_ = 0.055f;
        }
        if (timer_ <= 0.0f || (diving_ && player.IsGrounded() && progress > 0.18f)) {
            player.TriggerSlimeImpulse({ 2.8f, 0.68f, 2.5f }, 0.18f);
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
    enum class State { Idle, Rush, Guard, BounceCharge, BounceRise, BounceFall, BounceRecovery };

public:
    bool CanBreakImpactGate() const override {
        return state_ == State::Rush || state_ == State::BounceFall;
    }

    bool IsPinkBounceSlamImpactActive() const override {
        return state_ == State::BounceFall;
    }

    bool NotifyGuardedHit(Player& player, const Vector3& sourcePosition) override {
        if (state_ != State::Guard || guardImpactCooldown_ > 0.0f) {
            return false;
        }

        guardImpactCooldown_ = 0.22f;
        guardHitMotionTimer_ = 0.24f;
        storedBouncePower_ = std::min(1.0f, storedBouncePower_ + 0.36f);
        direction_ = NormalizePlanar(player.GetWorldPosition() - sourcePosition);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.PlaySlimeAbilityMotion("player_ability_pink_guard_hit", false,
            1.0f, 0.015f, 0.045f);
        player.TriggerSlimeImpulse({ 3.05f, 0.86f, 2.78f }, 0.13f);
        VFXSequencer::PlayOneShot("player_pink_guard_hit_cue",
            player.GetWorldPosition() + Vector3{ 0.0f, 0.66f, 0.0f });
        return true;
    }

private:
    void BeginRush(Player& player, float duration, float speed, bool diving, float damage) {
        state_ = State::Rush;
        duration_ = duration;
        timer_ = duration;
        startSpeed_ = speed;
        direction_ = NormalizePlanar(player.GetForwardDirection());
        diving_ = diving;
        rushDamage_ = damage;
        effectTimer_ = 0.0f;
        hitTargets_.clear();
        player.SetIsControlActive(false);
        player.SetDashInvincible(true);
        player.StartEvasionInvincibility(duration + 0.10f);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, direction_);
        player.PlaySlimeAbilityMotion("player_ability_pink_rush", false,
            0.55f / (std::max)(duration, 0.01f), 0.035f, 0.06f);
        player.TriggerSlimeImpulse({ 1.18f, 2.4f, 1.88f }, 0.15f);
    }

    void BeginBounceCharge(Player& player) {
        state_ = State::BounceCharge;
        direction_ = NormalizePlanar(player.GetForwardDirection());
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
        bounceCooldown_ = 1.15f;
    }

    void LaunchBounceSlam(Player& player) {
        state_ = State::BounceRise;
        duration_ = 0.40f;
        timer_ = duration_;
        effectTimer_ = 0.0f;
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        const float launchForwardSpeed = 6.2f + activeBouncePower_ * 1.4f;
        const float launchVerticalSpeed = 21.5f + activeBouncePower_ * 4.5f;
        player.SetVelocity({ direction_.x * launchForwardSpeed,
            std::max(player.GetVelocity().y, launchVerticalSpeed),
            direction_.z * launchForwardSpeed });
        player.StartEvasionInvincibility(0.42f);
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

    void LandBounceSlam(Player& player) {
        const Vector3 center = player.GetWorldPosition() + Vector3{ 0.0f, 0.24f, 0.0f };
        const float radius = 3.25f + activeBouncePower_ * 1.15f;
        const float damage = 14.0f + activeBouncePower_ * 8.0f;
        DamageSphere(player, center, radius, damage, DamageType::Physical,
            12.0f + activeBouncePower_ * 4.0f, 9.0f + activeBouncePower_ * 3.0f);
        VFXSequencer::PlayOneShot("player_pink_bounce_slam_cue", center);
        if (activeBouncePower_ >= 0.35f) {
            SpawnEffect("Resources/json/effect/effect_pink_slime_landing_burst_ring.json",
                center, {},
                { 1.0f + activeBouncePower_ * 0.42f,
                  1.0f + activeBouncePower_ * 0.42f,
                  1.0f + activeBouncePower_ * 0.42f });
        }
        player.PlaySlimeAbilityMotion("player_ability_pink_bounce_land", false,
            1.0f, 0.012f, 0.055f);
        player.TriggerSlimeImpulse({ 3.18f, 0.42f, 3.18f }, 0.16f);
        player.SetVelocity({ direction_.x * 1.8f,
            4.2f + activeBouncePower_ * 1.8f, direction_.z * 1.8f });
        state_ = State::BounceRecovery;
        timer_ = 0.34f;
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
    float rushDamage_ = 10.0f;
    bool guardHeld_ = false;
    bool diving_ = false;
};

class FireSlimeCopyAbility final : public ICopyAbilitySession {
public:
    explicit FireSlimeCopyAbility(EnemyAttackProfile profile)
        : ICopyAbilitySession(std::move(profile)) {
        if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
            effectScope_ = effects->CreateEffectScope();
        }
    }

    ~FireSlimeCopyAbility() override {
        StopDashEffects();
    }

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        primaryHeld_ = input.primaryHeld;
        if (input.secondaryTriggered && !dashActive_ && dashCooldown_ <= 0.0f) {
            BeginDash(player);
        }
        else if (input.specialTriggered && fireballCooldown_ <= 0.0f && !dashActive_) {
            Fireball(player);
        }
    }

    void Update(Player& player, float deltaTime) override {
        fireballCooldown_ = std::max(0.0f, fireballCooldown_ - deltaTime);
        dashCooldown_ = std::max(0.0f, dashCooldown_ - deltaTime);
        breathDamageTimer_ -= deltaTime;
        breathEffectTimer_ -= deltaTime;
        dashEffectTimer_ -= deltaTime;
        dashGroundWakeTimer_ -= deltaTime;
        actionMotionTimer_ = std::max(0.0f, actionMotionTimer_ - deltaTime);
        if (dashActive_) {
            UpdateDash(player, deltaTime);
            return;
        }
        if (actionMotionTimer_ > 0.0f) {
            return;
        }
        if (!primaryHeld_) {
            StopBreathMotion(player);
            return;
        }
        if (!breathMotionActive_) {
            breathMotionActive_ = player.PlaySlimeAbilityMotion(
                "player_ability_fire_breath", true, 1.0f, 0.05f, 0.08f);
        }
        const Vector3 direction = NormalizePlanar(player.GetForwardDirection());
        const Vector3 origin = player.GetWorldPosition() + Vector3{ 0.0f, 0.78f, 0.0f };
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Idle, direction);
        if (breathEffectTimer_ <= 0.0f) {
            EmitDirectedPreset("fire_slime_breath", origin, direction, 1.0f);
            EmitDirectedPreset("fire_slime_breath_embers", origin, direction, 0.85f);
            breathEffectTimer_ = 0.065f;
        }
        if (breathDamageTimer_ <= 0.0f) {
            DamageBreath(player, origin, direction);
            breathDamageTimer_ = 0.22f;
        }
    }

    void Cancel(Player& player) override {
        StopDashEffects();
        player.ClearSlimeAbilityMotion();
        dashActive_ = false;
        primaryHeld_ = false;
        breathMotionActive_ = false;
        breathDamageTimer_ = 0.0f;
        breathEffectTimer_ = 0.0f;
        dashTimer_ = 0.0f;
        dashEffectTimer_ = 0.0f;
        dashGroundWakeTimer_ = 0.0f;
        actionMotionTimer_ = 0.0f;
        hitTargets_.clear();
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
        player.SetIsControlActive(true);
    }

    bool CanBreakImpactGate() const override { return dashActive_; }

private:
    void Fireball(Player& player) {
        EnemyAttackDefinition fallback;
        fallback.id = "fireball";
        fallback.damage = 1.0f;
        fallback.maxSpeed = 31.0f;
        fallback.lifetime = 2.2f;
        const EnemyAttackDefinition& attack = FindAttackOrFallback(profile_, "fireball", fallback);
        const Vector3 direction = NormalizePlanar(player.GetForwardDirection());
        const Vector3 position = player.GetWorldPosition() + direction * 1.75f + Vector3{ 0.0f, 1.35f, 0.0f };
        const float speed = attack.maxSpeed > 0.0f ? attack.maxSpeed : 31.0f;
        BulletManager::GetInstance()->Fire(position, direction * speed + Vector3{ 0.0f, 1.4f, 0.0f },
            kPlayerAttack, kEnemy | kAllSolid, "Primitives/sphere", 0.52f,
            attack.lifetime > 0.0f ? attack.lifetime : 2.2f, MakeFireVisual(), std::max(attack.damage, 14.0f),
            MakeBurnStatus(attack), DamageType::Fire);
        StopBreathMotion(player);
        player.PlaySlimeAbilityMotion("player_ability_fireball", false, 1.0f, 0.035f, 0.08f);
        actionMotionTimer_ = 0.58f;
        EmitPreset(attack.activeVfx.empty() ? "fire_slime_cast" : attack.activeVfx.c_str(), position);
        fireballCooldown_ = 0.58f;
    }

    void BeginDash(Player& player) {
        // 前回の突進が異常終了していても、新しい炎を重ねる前に必ず回収します。
        StopDashEffects();
        StopBreathMotion(player);
        dashDirection_ = NormalizePlanar(player.GetForwardDirection());
        player.PlaySlimeAbilityMotion("player_ability_fire_dash", false, 1.0f, 0.025f, 0.055f);
        dashActive_ = true;
        dashTimer_ = 0.44f;
        dashCooldown_ = 1.05f;
        dashEffectTimer_ = 0.0f;
        dashGroundWakeTimer_ = 0.0f;
        actionMotionTimer_ = 0.0f;
        hitTargets_.clear();
        const Vector3 position = player.GetWorldPosition();
        SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_burst.json",
            position + Vector3{ 0.0f, 0.18f, 0.0f });
        SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_body.json",
            position + Vector3{ 0.0f, 0.60f, 0.0f },
            { 0.0f, std::atan2(dashDirection_.x, dashDirection_.z), 0.0f });
        EmitPreset("player_fire_blaze_burst", position + Vector3{ 0.0f, 0.42f, 0.0f });
        player.SetVelocity({ dashDirection_.x * 38.0f, std::max(player.GetVelocity().y, 0.0f), dashDirection_.z * 38.0f });
        player.SetMoveYaw(std::atan2(dashDirection_.x, dashDirection_.z));
        player.SetIsControlActive(false);
        player.StartEvasionInvincibility(0.50f);
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::BlazeDash);
        player.TriggerSlimeImpulse({ 1.08f, 1.16f, 3.62f }, 0.17f);
    }

    void UpdateDash(Player& player, float deltaTime) {
        dashTimer_ = std::max(0.0f, dashTimer_ - deltaTime);
        const float progress = 1.0f - dashTimer_ / 0.44f;
        const float speed = 38.0f * (1.0f - progress * 0.36f);
        Vector3 velocity = player.GetVelocity();
        velocity.x = dashDirection_.x * speed;
        velocity.z = dashDirection_.z * speed;
        if (player.IsGrounded() && velocity.y < 0.0f) velocity.y = 0.0f;
        player.SetVelocity(velocity);
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(dashDirection_.x, dashDirection_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, dashDirection_);
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::BlazeDash);
        DamageDash(player);
        if (dashEffectTimer_ <= 0.0f) {
            const Vector3 trail = player.GetWorldPosition() - dashDirection_ * 0.72f + Vector3{ 0.0f, 0.12f, 0.0f };
            SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_trail.json", trail,
                { 0.0f, std::atan2(dashDirection_.x, dashDirection_.z), 0.0f });
            SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_core.json",
                trail + Vector3{ 0.0f, 0.36f, 0.0f });
            EmitDirectedPreset("player_fire_blaze_trail", trail, dashDirection_ * -1.0f, 0.85f);
            dashEffectTimer_ = 0.055f;
        }
        if (player.IsGrounded() && dashGroundWakeTimer_ <= 0.0f) {
            const Vector3 wake = player.GetWorldPosition() - dashDirection_ * 0.42f +
                Vector3{ 0.0f, 0.04f, 0.0f };
            SpawnScopedEffect(effectScope_,
                "Resources/json/effect/effect_player_fire_blaze_ground_wake.json", wake,
                { 0.0f, std::atan2(dashDirection_.x, dashDirection_.z), 0.0f },
                { 0.92f, 0.92f, 1.16f });
            dashGroundWakeTimer_ = 0.10f;
        }
        if (dashTimer_ <= 0.0f) {
            StopDashEffects();
            dashActive_ = false;
            player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
            player.StopSlimeAbilityMotion(0.055f);
            player.SetIsControlActive(true);
            velocity.x *= 0.30f;
            velocity.z *= 0.30f;
            player.SetVelocity(velocity);
            hitTargets_.clear();
            EmitPreset("player_fire_blaze_burst", player.GetWorldPosition() + Vector3{ 0.0f, 0.20f, 0.0f });
        }
    }

    void StopDashEffects() {
        if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
            effects->StopEffectScope(effectScope_);
        }
    }

    void StopBreathMotion(Player& player) {
        if (!breathMotionActive_) {
            return;
        }
        player.StopSlimeAbilityMotion(0.08f);
        breathMotionActive_ = false;
    }

    void DamageDash(Player& player) {
        PhysicsQueryFilter filter;
        filter.mask = kEnemy;
        filter.ignoredObject = &player;
        const Vector3 center = player.GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f };
        for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, 1.55f, filter)) {
            Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !hitTargets_.insert(target).second) continue;
            EnemyAttackDefinition fallback;
            const EnemyAttackDefinition& attack = FindAttackOrFallback(profile_, "flame_breath", fallback);
            DamageEnemy(player, target, 12.0f, DamageType::Fire,
                { dashDirection_.x * 11.0f, 5.5f, dashDirection_.z * 11.0f }, MakeBurnStatus(attack));
            VFXSequencer::PlayOneShot("player_fire_dash_hit_cue",
                target->GetWorldPosition() + Vector3{ 0.0f, 0.58f, 0.0f });
        }
    }

    void DamageBreath(Player& player, const Vector3& origin, const Vector3& direction) {
        EnemyAttackDefinition fallback;
        fallback.maxRange = 8.0f;
        fallback.radius = 1.4f;
        fallback.damage = 0.5f;
        const EnemyAttackDefinition& attack = FindAttackOrFallback(profile_, "flame_breath", fallback);
        PhysicsQueryFilter filter;
        filter.mask = kEnemy;
        filter.ignoredObject = &player;
        std::unordered_set<Object3d*> damaged;
        const Vector3 end = origin + direction * std::max(attack.maxRange, 4.0f);
        for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapCapsule(origin, end,
            std::max(attack.radius, 1.0f), filter)) {
            Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !damaged.insert(target).second) continue;
            DamageEnemy(player, target, std::max(attack.damage, 2.5f), DamageType::Fire,
                { direction.x * 5.0f, 2.5f, direction.z * 5.0f }, MakeBurnStatus(attack));
        }
    }

    Vector3 dashDirection_{ 0.0f, 0.0f, 1.0f };
    std::unordered_set<Object3d*> hitTargets_;
    float fireballCooldown_ = 0.0f;
    float dashCooldown_ = 0.0f;
    float dashTimer_ = 0.0f;
    float dashEffectTimer_ = 0.0f;
    float breathDamageTimer_ = 0.0f;
    float breathEffectTimer_ = 0.0f;
    float dashGroundWakeTimer_ = 0.0f;
    float actionMotionTimer_ = 0.0f;
    MeshEffectManager::EffectScopeId effectScope_ = MeshEffectManager::kInvalidEffectScope;
    bool dashActive_ = false;
    bool primaryHeld_ = false;
    bool breathMotionActive_ = false;
};

class ThunderSlimeCopyAbility final : public ICopyAbilitySession {
public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        if (input.secondaryTriggered && evadeCooldown_ <= 0.0f) {
            Evade(player);
        }
        else if (input.specialTriggered && lineCooldown_ <= 0.0f && lineTimer_ <= 0.0f) {
            lineDirection_ = NormalizePlanar(player.GetForwardDirection());
            lineTimer_ = 0.50f;
            strikeIndex_ = 0;
            lineCooldown_ = 1.65f;
            lineHitTargets_.clear();
            player.PlaySlimeAbilityMotion("player_ability_thunder_cast", false, 1.0f, 0.04f, 0.08f);
            SpawnEffect("Resources/json/effect/effect_player_thunder_warning.json",
                player.GetWorldPosition() + lineDirection_ * 2.8f);
        }
        else if (input.primaryTriggered && dischargeCooldown_ <= 0.0f && dischargeTimer_ <= 0.0f) {
            dischargeTimer_ = 0.48f;
            dischargeCooldown_ = 1.35f;
            player.PlaySlimeAbilityMotion("player_ability_thunder_cast", false, 1.0f, 0.04f, 0.08f);
            EmitPreset("player_thunder_discharge_charge", player.GetWorldPosition() + Vector3{ 0.0f, 0.45f, 0.0f });
        }
    }

    void Update(Player& player, float deltaTime) override {
        lineCooldown_ = std::max(0.0f, lineCooldown_ - deltaTime);
        dischargeCooldown_ = std::max(0.0f, dischargeCooldown_ - deltaTime);
        evadeCooldown_ = std::max(0.0f, evadeCooldown_ - deltaTime);
        if (dischargeTimer_ > 0.0f) {
            dischargeTimer_ = std::max(0.0f, dischargeTimer_ - deltaTime);
            if (dischargeTimer_ <= 0.0f) {
                const Vector3 center = player.GetWorldPosition() + Vector3{ 0.0f, 0.45f, 0.0f };
                SpawnEffect("Resources/json/effect/effect_player_thunder_discharge_burst.json", center);
                EmitPreset("player_thunder_discharge_burst", center);
                DamageSphere(player, center, 4.6f, 14.0f, DamageType::Electric, 10.0f, 6.5f);
                player.TriggerSlimeImpulse({ 2.4f, 0.68f, 2.4f }, 0.16f);
            }
        }
        if (lineTimer_ <= 0.0f) return;
        lineTimer_ = std::max(0.0f, lineTimer_ - deltaTime);
        const float elapsed = 0.50f - lineTimer_;
        while (strikeIndex_ < 5 && elapsed >= 0.10f + static_cast<float>(strikeIndex_) * 0.075f) {
            const Vector3 ground = player.GetWorldPosition() + lineDirection_ * (2.8f + 2.2f * static_cast<float>(strikeIndex_));
            SpawnStrike(player, ground);
            ++strikeIndex_;
        }
    }

    void Cancel(Player& player) override {
        player.ClearSlimeAbilityMotion();
        lineTimer_ = 0.0f;
        dischargeTimer_ = 0.0f;
        lineHitTargets_.clear();
    }

private:
    void SpawnStrike(Player& player, const Vector3& ground) {
        const float yaw = std::atan2(lineDirection_.x, lineDirection_.z);
        SpawnEffect("Resources/json/effect/effect_player_thunder_bolt.json", ground + Vector3{ 0.0f, 4.75f, 0.0f },
            { kPi * 0.5f, yaw, 0.0f });
        SpawnEffect("Resources/json/effect/effect_player_thunder_impact_ring.json", ground + Vector3{ 0.0f, 0.06f, 0.0f });
        EmitPreset("player_thunder_strike_impact", ground + Vector3{ 0.0f, 0.22f, 0.0f });
        PhysicsQueryFilter filter;
        filter.mask = kEnemy;
        filter.ignoredObject = &player;
        for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(
            ground + Vector3{ 0.0f, 1.1f, 0.0f }, 1.45f, filter)) {
            Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !lineHitTargets_.insert(target).second) continue;
            DamageEnemy(player, target, 10.0f, DamageType::Electric,
                { lineDirection_.x * 6.5f, 7.0f, lineDirection_.z * 6.5f });
        }
    }

    void Evade(Player& player) {
        const Vector3 direction = NormalizePlanar(player.GetForwardDirection());
        const Vector3 start = player.GetWorldPosition();
        PhysicsQueryFilter filter;
        filter.mask = kAllSolid;
        filter.ignoredObject = &player;
        const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(start + Vector3{ 0.0f, 0.65f, 0.0f },
            0.48f, direction, 9.5f, filter);
        const float distance = hit.isHit ? std::max(1.15f, hit.distance - 0.75f) : 9.5f;
        const Vector3 destination = start + direction * distance;
        SpawnEffect("Resources/json/effect/effect_player_thunder_evade_cross.json", start + Vector3{ 0.0f, 0.62f, 0.0f });
        SpawnEffect("Resources/json/effect/effect_player_thunder_evade_seal.json", destination + Vector3{ 0.0f, 0.08f, 0.0f });
        EmitDirectedPreset("player_thunder_evade_sparks", start, direction, 1.0f);
        player.SetTranslate(destination);
        player.SetMoveYaw(std::atan2(direction.x, direction.z));
        player.PlaySlimeAbilityMotion("player_ability_thunder_evade", false, 1.0f, 0.02f, 0.06f);
        player.StartEvasionInvincibility(0.24f);
        player.TriggerSlimeImpulse({ 1.95f, 0.68f, 1.95f }, 0.13f);
        DamageSphere(player, destination + Vector3{ 0.0f, 0.55f, 0.0f }, 2.25f,
            5.0f, DamageType::Electric, 9.0f, 5.0f);
        evadeCooldown_ = 0.85f;
    }

    Vector3 lineDirection_{ 0.0f, 0.0f, 1.0f };
    std::unordered_set<Object3d*> lineHitTargets_;
    float lineCooldown_ = 0.0f;
    float dischargeCooldown_ = 0.0f;
    float evadeCooldown_ = 0.0f;
    float lineTimer_ = 0.0f;
    float dischargeTimer_ = 0.0f;
    int strikeIndex_ = 0;
};

class WindSlimeCopyAbility final : public ICopyAbilitySession {
public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        primaryHeld_ = input.primaryHeld;
        if (input.secondaryTriggered && player.IsGrounded() && !soarActive_ && soarCooldown_ <= 0.0f) {
            BeginSoar(player);
        }
        else if (input.specialTriggered && !updraftConsumed_ && updraftCooldown_ <= 0.0f) {
            Updraft(player);
        }
    }

    void Update(Player& player, float deltaTime) override {
        updraftCooldown_ = std::max(0.0f, updraftCooldown_ - deltaTime);
        soarCooldown_ = std::max(0.0f, soarCooldown_ - deltaTime);
        effectTimer_ -= deltaTime;
        pushTimer_ -= deltaTime;
        actionMotionTimer_ = std::max(0.0f, actionMotionTimer_ - deltaTime);
        if (!soarActive_ && player.IsGrounded() && player.GetVelocity().y <= 0.1f) {
            updraftConsumed_ = false;
        }
        if (soarActive_) {
            Vector3 velocity = player.GetVelocity();
            if (velocity.y < 0.0f) {
                if (!descending_) {
                    player.PlaySlimeAbilityMotion("player_ability_wind_slow_fall", true, 1.0f, 0.10f, 0.10f);
                    const Vector3 transition = player.GetWorldPosition() + Vector3{ 0.0f, 0.58f, 0.0f };
                    SpawnEffect("Resources/json/effect/effect_player_wind_slow_fall.json", transition);
                    EmitDirectedPreset("player_wind_dash", transition, Vector3{ 0.0f, 1.0f, 0.0f }, 0.92f);
                    player.TriggerSlimeImpulse({ 2.24f, 0.72f, 2.24f }, 0.12f);
                    effectTimer_ = 0.11f;
                }
                descending_ = true;
                velocity.y = std::max(velocity.y, -5.2f);
                player.SetVelocity(velocity);
                player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::SlowFall);
                if (effectTimer_ <= 0.0f) {
                    const Vector3 position = player.GetWorldPosition() + Vector3{ 0.0f, 0.58f, 0.0f };
                    SpawnEffect("Resources/json/effect/effect_player_wind_slow_fall.json", position);
                    EmitDirectedPreset("player_wind_dash", position, Vector3{ 0.0f, 1.0f, 0.0f }, 0.78f);
                    effectTimer_ = 0.11f;
                }
            }
            player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, soarDirection_);
            // 落下速度低下は時間切れで空中解除せず、着地するまで維持します。
            if (descending_ && player.IsGrounded()) {
                FinishSoar(player);
            }
            return;
        }
        if (actionMotionTimer_ > 0.0f) {
            return;
        }
        if (!primaryHeld_) {
            StopBreathMotion(player);
            return;
        }
        if (!breathMotionActive_) {
            breathMotionActive_ = player.PlaySlimeAbilityMotion(
                "player_ability_wind_breath", true, 1.0f, 0.05f, 0.08f);
        }
        const Vector3 direction = NormalizePlanar(player.GetForwardDirection());
        const Vector3 origin = player.GetWorldPosition() + Vector3{ 0.0f, 0.74f, 0.0f };
        if (effectTimer_ <= 0.0f) {
            EmitDirectedPreset("wind_slime_breath", origin, direction, 1.0f);
            effectTimer_ = 0.055f;
        }
        if (pushTimer_ <= 0.0f) {
            PushCone(player, origin, direction);
            pushTimer_ = 0.16f;
        }
    }

    void Cancel(Player& player) override {
        player.ClearSlimeAbilityMotion();
        primaryHeld_ = false;
        breathMotionActive_ = false;
        soarActive_ = false;
        descending_ = false;
        updraftConsumed_ = false;
        actionMotionTimer_ = 0.0f;
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
    }

private:
    void Updraft(Player& player) {
        StopBreathMotion(player);
        Vector3 velocity = player.GetVelocity();
        velocity.y = std::max(velocity.y, 18.0f);
        player.SetVelocity(velocity);
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, player.GetForwardDirection());
        player.PlaySlimeAbilityMotion("player_ability_wind_takeoff", false, 1.0f, 0.03f, 0.08f);
        player.TriggerSlimeImpulse({ 0.74f, 1.62f, 0.74f }, 0.22f);
        const Vector3 center = player.GetWorldPosition();
        SpawnEffect("Resources/json/effect/effect_player_wind_updraft_spiral.json", center + Vector3{ 0.0f, 1.72f, 0.0f });
        SpawnEffect("Resources/json/effect/effect_player_wind_updraft_spiral_counter.json", center + Vector3{ 0.0f, 1.18f, 0.0f });
        EmitPreset("player_wind_updraft", center);
        DamageSphere(player, center + Vector3{ 0.0f, 1.0f, 0.0f }, 4.2f, 10.0f, DamageType::Physical, 5.0f, 16.5f);
        actionMotionTimer_ = 0.52f;
        updraftConsumed_ = true;
        updraftCooldown_ = 1.0f;
    }

    void BeginSoar(Player& player) {
        StopBreathMotion(player);
        soarDirection_ = NormalizePlanar(player.GetVelocity());
        if (std::abs(player.GetVelocity().x) + std::abs(player.GetVelocity().z) < 1.2f) {
            soarDirection_ = NormalizePlanar(player.GetForwardDirection());
        }
        soarActive_ = true;
        descending_ = false;
        soarCooldown_ = 1.15f;
        effectTimer_ = 0.0f;
        player.SetVelocity({ soarDirection_.x * 9.0f, std::max(player.GetVelocity().y, 19.5f), soarDirection_.z * 9.0f });
        player.SetMoveYaw(std::atan2(soarDirection_.x, soarDirection_.z));
        player.PlaySlimeAbilityMotion("player_ability_wind_takeoff", false, 1.0f, 0.03f, 0.08f);
        player.StartEvasionInvincibility(0.28f);
        player.TriggerSlimeImpulse({ 1.18f, 3.42f, 1.18f }, 0.20f);
        SpawnEffect("Resources/json/effect/effect_player_wind_soar_launch.json", player.GetWorldPosition());
        EmitPreset("player_wind_dash", player.GetWorldPosition());
    }

    void FinishSoar(Player& player) {
        if (!soarActive_) return;
        soarActive_ = false;
        descending_ = false;
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
        player.PlaySlimeAbilityMotion("player_ability_wind_land", false, 1.0f, 0.02f, 0.07f);
        actionMotionTimer_ = 0.36f;
        VFXSequencer::PlayOneShot("player_wind_soar_land_cue", player.GetWorldPosition());
        player.TriggerSlimeImpulse({ 2.68f, 1.26f, 2.68f }, 0.15f);
    }

    void StopBreathMotion(Player& player) {
        if (!breathMotionActive_) {
            return;
        }
        player.StopSlimeAbilityMotion(0.08f);
        breathMotionActive_ = false;
    }

    void PushCone(Player& player, const Vector3& origin, const Vector3& direction) {
        SceneManager* manager = SceneManager::GetInstance();
        BaseScene* scene = manager ? manager->GetCurrentScene() : nullptr;
        if (!scene) return;
        for (const auto& object : scene->GetObjects()) {
            BaseEnemy* enemy = dynamic_cast<BaseEnemy*>(object.get());
            if (!enemy || enemy->isDead || enemy->IsCarried() || !enemy->GetIsVisible()) continue;
            Vector3 toTarget = enemy->GetWorldPosition() - origin;
            toTarget.y = 0.0f;
            const float distance = Math::Length(toTarget);
            if (distance <= 0.001f || distance > 8.5f) continue;
            const Vector3 targetDirection = NormalizePlanar(toTarget);
            if (Math::Dot(direction, targetDirection) < 0.20f) continue;
            DamageEnemy(player, enemy, 3.0f, DamageType::Physical,
                { direction.x * 8.0f, 3.0f, direction.z * 8.0f });
            enemy->ApplyExternalImpulse({ direction.x * 24.0f, 7.0f, direction.z * 24.0f }, 0.24f);
            EmitDirectedPreset("wind_slime_gust_impact", enemy->GetWorldPosition(), direction, 1.0f);
        }
    }

    Vector3 soarDirection_{ 0.0f, 0.0f, 1.0f };
    float updraftCooldown_ = 0.0f;
    float soarCooldown_ = 0.0f;
    float effectTimer_ = 0.0f;
    float pushTimer_ = 0.0f;
    float actionMotionTimer_ = 0.0f;
    bool primaryHeld_ = false;
    bool breathMotionActive_ = false;
    bool soarActive_ = false;
    bool descending_ = false;
    bool updraftConsumed_ = false;
};

class BomberCopyAbility final : public ICopyAbilitySession {
public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        if (input.secondaryTriggered && blastCooldown_ <= 0.0f) BlastJump(player);
        else if (input.specialTriggered && throwCooldown_ <= 0.0f) SpawnBomb(player, false);
        else if (input.primaryTriggered && placeCooldown_ <= 0.0f) SpawnBomb(player, true);
    }

    void Update(Player& player, float deltaTime) override {
        throwCooldown_ = std::max(0.0f, throwCooldown_ - deltaTime);
        placeCooldown_ = std::max(0.0f, placeCooldown_ - deltaTime);
        blastCooldown_ = std::max(0.0f, blastCooldown_ - deltaTime);
        trailTimer_ -= deltaTime;
        if (!blastActive_) return;
        blastElapsed_ += deltaTime;
        if (trailTimer_ <= 0.0f) {
            const Vector3 position = player.GetWorldPosition() - blastDirection_ * 0.32f + Vector3{ 0.0f, 0.12f, 0.0f };
            SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump_trail.json", position);
            EmitDirectedPreset("player_bomb_blast_jump", position, { 0.0f, -1.0f, 0.0f }, 0.62f);
            trailTimer_ = 0.055f;
        }
        if (blastElapsed_ > 0.16f && player.IsGrounded() && player.GetVelocity().y <= 0.0f) {
            SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump_land.json", player.GetWorldPosition());
            blastActive_ = false;
        }
    }

    void Cancel(Player&) override { blastActive_ = false; }

private:
    void SpawnBomb(Player& player, bool placed) {
        SceneManager* manager = SceneManager::GetInstance();
        BaseScene* scene = manager ? manager->GetCurrentScene() : nullptr;
        if (!scene || !scene->GetObject3dCommon()) return;
        auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", scene->GetObject3dCommon());
        if (!bomb) return;
        const Vector3 forward = NormalizePlanar(player.GetForwardDirection());
        Vector3 position = player.GetWorldPosition() + forward * (placed ? 1.55f : 2.1f);
        position.y += placed ? 0.42f : 2.15f;
        bomb->SetTranslate(position);
        bomb->SetRotationY(std::atan2(forward.x, forward.z));
        bomb->SetTarget(&player);
        if (EnemyBomb* enemyBomb = dynamic_cast<EnemyBomb*>(bomb.get())) {
            enemyBomb->SetPlayerOwned(true);
            enemyBomb->Ignite(placed ? 1.35f : 2.25f);
        }
        bomb->SetCarried(false);
        bomb->SetVelocity(placed ? Vector3{ 0.0f, 1.8f, 0.0f }
            : Vector3{ forward.x * 24.0f, 8.0f, forward.z * 24.0f });
        SpawnEffect(placed ? "Resources/json/effect/effect_player_bomb_place.json"
            : "Resources/json/effect/effect_carry_bomber_throw_burst.json", position);
        EmitPreset(placed ? "player_bomb_place_fuse" : "carry_bomber_throw_sparks", position);
        scene->AddObject(std::move(bomb));
        if (placed) {
            placeCooldown_ = 0.85f;
            player.TriggerSlimeImpulse({ 1.62f, 0.78f, 1.62f }, 0.13f);
        }
        else {
            throwCooldown_ = 0.42f;
        }
    }

    void BlastJump(Player& player) {
        blastDirection_ = NormalizePlanar(player.GetForwardDirection());
        const Vector3 center = player.GetWorldPosition();
        player.SetVelocity({ blastDirection_.x * 8.5f, 18.0f, blastDirection_.z * 8.5f });
        player.StartEvasionInvincibility(0.32f);
        player.SetMoveYaw(std::atan2(blastDirection_.x, blastDirection_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, blastDirection_);
        player.TriggerSlimeImpulse({ 2.55f, 0.62f, 2.55f }, 0.17f);
        DamageSphere(player, center + Vector3{ 0.0f, 0.58f, 0.0f }, 3.2f, 8.0f, DamageType::Explosion, 13.0f, 9.5f);
        SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump.json", center);
        EmitPreset("player_bomb_blast_jump", center);
        blastCooldown_ = 1.10f;
        trailTimer_ = 0.0f;
        blastElapsed_ = 0.0f;
        blastActive_ = true;
    }

    Vector3 blastDirection_{ 0.0f, 0.0f, 1.0f };
    float throwCooldown_ = 0.0f;
    float placeCooldown_ = 0.0f;
    float blastCooldown_ = 0.0f;
    float trailTimer_ = 0.0f;
    float blastElapsed_ = 0.0f;
    bool blastActive_ = false;
};

std::unique_ptr<ICopyAbilitySession> CreateSession(int morphType, const EnemyAttackProfile& profile) {
    if (morphType == static_cast<int>(Player::EnemyMorphType::Slime)) {
        return std::make_unique<PinkSlimeCopyAbility>(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::FireSlime)) {
        return std::make_unique<FireSlimeCopyAbility>(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::ThunderSlime)) {
        return std::make_unique<ThunderSlimeCopyAbility>(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::WindSlime)) {
        return std::make_unique<WindSlimeCopyAbility>(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::Bomber)) {
        return std::make_unique<BomberCopyAbility>(profile);
    }
    return nullptr;
}

const char* GetEnemyTypeForMorph(int morphType) {
    if (morphType == static_cast<int>(Player::EnemyMorphType::Slime)) return "Slime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::FireSlime)) return "FireSlime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::ThunderSlime)) return "ThunderSlime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::WindSlime)) return "WindSlime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::Bomber)) return "Bomber";
    return "";
}
}

class PlayerCopyAbilityController::Impl {
public:
    int morphType = static_cast<int>(Player::EnemyMorphType::None);
    std::unique_ptr<ICopyAbilitySession> session;
};

PlayerCopyAbilityController::PlayerCopyAbilityController() : impl_(std::make_unique<Impl>()) {}
PlayerCopyAbilityController::~PlayerCopyAbilityController() = default;

void PlayerCopyAbilityController::Activate(int morphType, const EnemyAttackProfile& attackProfile) {
    impl_->morphType = morphType;
    impl_->session = CreateSession(morphType, attackProfile);
}

void PlayerCopyAbilityController::ActivateDefault(int morphType) {
    const char* enemyType = GetEnemyTypeForMorph(morphType);
    if (!enemyType || enemyType[0] == '\0') {
        impl_->session.reset();
        impl_->morphType = static_cast<int>(Player::EnemyMorphType::None);
        return;
    }
    Activate(morphType, EnemyAttackProfile::CreateDefault(enemyType));
}

void PlayerCopyAbilityController::ProcessInput(Player& player, const PlayerCopyAbilityInput& input) {
    if (impl_->session) {
        impl_->session->ProcessInput(player, input);
    }
}

void PlayerCopyAbilityController::Update(Player& player, float deltaTime) {
    if (impl_->session && deltaTime > 0.0f) {
        impl_->session->Update(player, deltaTime);
    }
}

void PlayerCopyAbilityController::Cancel(Player& player) {
    if (impl_->session) {
        impl_->session->Cancel(player);
        impl_->session.reset();
    }
    impl_->morphType = static_cast<int>(Player::EnemyMorphType::None);
}

bool PlayerCopyAbilityController::NotifyGuardedHit(
    Player& player, const Vector3& sourcePosition) {
    return impl_->session &&
        impl_->session->NotifyGuardedHit(player, sourcePosition);
}

bool PlayerCopyAbilityController::HandlesMorphType(int morphType) const {
    return impl_->session && impl_->morphType == morphType;
}

bool PlayerCopyAbilityController::IsActive() const {
    return impl_->session != nullptr;
}

bool PlayerCopyAbilityController::CanBreakImpactGate() const {
    return impl_->session && impl_->session->CanBreakImpactGate();
}

bool PlayerCopyAbilityController::IsPinkBounceSlamImpactActive() const {
    return impl_->session &&
        impl_->session->IsPinkBounceSlamImpactActive();
}
