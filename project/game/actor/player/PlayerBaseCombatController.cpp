#define NOMINMAX
#include "PlayerBaseCombatController.h"

#include "BaseEnemy.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kBashWindupDuration = 0.07f;
constexpr float kBashActiveDuration = 0.21f;
constexpr float kBashRecoveryDuration = 0.16f;
constexpr float kPressWindupDuration = 0.09f;
constexpr float kPressMaximumFallDuration = 0.95f;
constexpr float kPressRecoveryDuration = 0.17f;
constexpr float kAttackInputBufferDuration = 0.18f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

Object3d* FindEnemyRoot(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}

void SpawnEffect(const char* path, const Vector3& position, const Vector3& rotation = {},
    const Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAt(path, position, rotation, scale);
    }
}

void EmitDirectedPreset(const char* preset, const Vector3& position, const Vector3& direction, float scale = 1.0f) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized()) {
        particles->EmitDirected(preset, position, direction, scale);
    }
}

void DispatchDamage(Player& player, Object3d* target, float damage, const Vector3& knockback) {
    DamageEvent event;
    event.target = target;
    event.attacker = &player;
    event.damageAmount = damage;
    event.damageType = DamageType::Physical;
    event.knockbackVelocity = knockback;
    EventManager::GetInstance()->Dispatch(event);
}
}

void PlayerBaseCombatController::ProcessInput(Player& player, bool attackTriggered) {
    if (!attackTriggered || player.isDead || player.IsCinematicLocked() ||
        player.IsEnemyMorphed() || player.GetCarriedEnemy()) {
        return;
    }

    if (IsActive()) {
        // 復帰直前の入力を捨てず、次の体当たり／プレスへつなぎます。
        inputBufferTimer_ = kAttackInputBufferDuration;
        return;
    }

    if (player.IsGrounded()) {
        BeginBash(player);
    }
    else {
        BeginPress(player);
    }
}

void PlayerBaseCombatController::Update(Player& player, float deltaTime) {
    if (!IsActive() || deltaTime <= 0.0f) {
        return;
    }

    if (player.isDead || player.IsCinematicLocked() || player.IsEnemyMorphed()) {
        Cancel(player, false);
        return;
    }

    inputBufferTimer_ = std::max(0.0f, inputBufferTimer_ - deltaTime);

    switch (phase_) {
    case Phase::BashWindup:
    case Phase::BashActive:
    case Phase::BashRecovery:
        UpdateBash(player, deltaTime);
        break;
    case Phase::PressWindup:
    case Phase::PressFall:
    case Phase::PressRecovery:
        UpdatePress(player, deltaTime);
        break;
    case Phase::Idle:
    default:
        break;
    }
}

void PlayerBaseCombatController::Cancel(Player& player, bool restoreControl) {
    if (!IsActive()) {
        return;
    }
    player.StopSlimeAbilityMotion(0.05f);
    player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
    if (restoreControl && !player.isDead && !player.IsCinematicLocked()) {
        player.SetIsControlActive(true);
    }
    phase_ = Phase::Idle;
    timer_ = 0.0f;
    effectTimer_ = 0.0f;
    inputBufferTimer_ = 0.0f;
    pressHitEnemy_ = false;
    hitTargets_.clear();
}

void PlayerBaseCombatController::BeginBash(Player& player) {
    direction_ = NormalizePlanar(player.GetForwardDirection());
    phase_ = Phase::BashWindup;
    timer_ = kBashWindupDuration;
    effectTimer_ = 0.0f;
    hitTargets_.clear();
    player.SetIsControlActive(false);
    player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
    player.SetVelocity({ 0.0f, player.GetVelocity().y, 0.0f });
    player.PlaySlimeAbilityMotion("player_base_bash", false, 1.0f, 0.025f, 0.06f);
    player.TriggerSlimeImpulse({ 1.12f, 0.86f, 0.94f }, 0.07f);
}

void PlayerBaseCombatController::BeginPress(Player& player) {
    direction_ = NormalizePlanar(player.GetForwardDirection());
    phase_ = Phase::PressWindup;
    timer_ = kPressWindupDuration;
    effectTimer_ = 0.0f;
    pressHitEnemy_ = false;
    hitTargets_.clear();
    player.SetIsControlActive(false);
    player.SetVelocity({ player.GetVelocity().x * 0.35f, std::max(player.GetVelocity().y, 2.5f),
        player.GetVelocity().z * 0.35f });
    player.PlaySlimeAbilityMotion("player_base_press", false, 1.0f, 0.025f, 0.06f);
    player.TriggerSlimeImpulse({ 0.72f, 1.55f, 0.72f }, 0.11f);
}

void PlayerBaseCombatController::UpdateBash(Player& player, float deltaTime) {
    timer_ -= deltaTime;
    player.SetIsControlActive(false);
    player.SetMoveYaw(std::atan2(direction_.x, direction_.z));

    if (phase_ == Phase::BashWindup) {
        player.SetVelocity({ 0.0f, player.GetVelocity().y, 0.0f });
        if (timer_ <= 0.0f) {
            phase_ = Phase::BashActive;
            timer_ = kBashActiveDuration;
            player.SetVelocity({ direction_.x * 22.0f, std::max(player.GetVelocity().y, 0.0f), direction_.z * 22.0f });
            player.TriggerSlimeImpulse({ 0.84f, 1.12f, 1.92f }, 0.14f);
            SpawnEffect("Resources/json/effect/effect_player_base_bash_arc.json",
                player.GetWorldPosition() + direction_ * 0.7f + Vector3{ 0.0f, 0.58f, 0.0f },
                { 0.0f, std::atan2(direction_.x, direction_.z), 0.0f },
                { 0.82f, 0.82f, 1.10f });
            EmitDirectedPreset("player_base_bash_droplets",
                player.GetWorldPosition() - direction_ * 0.34f + Vector3{ 0.0f, 0.34f, 0.0f },
                direction_ * -1.0f, 0.78f);
            effectTimer_ = 0.08f;
        }
        return;
    }

    if (phase_ == Phase::BashActive) {
        Vector3 velocity = player.GetVelocity();
        velocity.x = direction_.x * std::max(8.0f, 22.0f * (timer_ / kBashActiveDuration));
        velocity.z = direction_.z * std::max(8.0f, 22.0f * (timer_ / kBashActiveDuration));
        player.SetVelocity(velocity);
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, direction_);
        DamageBash(player);
        effectTimer_ -= deltaTime;
        if (effectTimer_ <= 0.0f) {
            EmitDirectedPreset("player_base_bash_droplets",
                player.GetWorldPosition() - direction_ * 0.38f + Vector3{ 0.0f, 0.30f, 0.0f },
                direction_ * -1.0f, 0.46f);
            effectTimer_ = 0.085f;
        }
        if (timer_ <= 0.0f) {
            phase_ = Phase::BashRecovery;
            timer_ = kBashRecoveryDuration;
            velocity.x *= 0.20f;
            velocity.z *= 0.20f;
            player.SetVelocity(velocity);
        }
        return;
    }

    if (timer_ <= 0.0f) {
        Finish(player);
    }
}

void PlayerBaseCombatController::UpdatePress(Player& player, float deltaTime) {
    timer_ -= deltaTime;
    player.SetIsControlActive(false);

    if (phase_ == Phase::PressWindup) {
        if (timer_ <= 0.0f) {
            phase_ = Phase::PressFall;
            timer_ = kPressMaximumFallDuration;
            player.SetVelocity({ direction_.x * 3.0f, -25.0f, direction_.z * 3.0f });
            player.PlaySlimeAbilityMotion("player_base_press_fall", true, 1.0f, 0.025f, 0.04f);
        }
        return;
    }

    if (phase_ == Phase::PressFall) {
        Vector3 velocity = player.GetVelocity();
        velocity.y = std::min(velocity.y, -25.0f);
        player.SetVelocity(velocity);
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, direction_);
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
        DamagePress(player, false);

        if (pressHitEnemy_) {
            phase_ = Phase::PressRecovery;
            timer_ = kPressRecoveryDuration;
            player.SetVelocity({ direction_.x * 2.0f, 14.0f, direction_.z * 2.0f });
            player.PlaySlimeAbilityMotion("player_base_press_land", false, 1.0f, 0.02f, 0.05f);
            player.TriggerSlimeImpulse({ 1.55f, 0.68f, 1.55f }, 0.14f);
        }
        else if (player.IsGrounded() || timer_ <= 0.0f) {
            DamagePress(player, true);
            VFXSequencer::PlayOneShot(
                "player_base_press_land_cue",
                player.GetWorldPosition() + Vector3{ 0.0f, 0.06f, 0.0f });
            phase_ = Phase::PressRecovery;
            timer_ = kPressRecoveryDuration;
            player.SetVelocity({ 0.0f, 4.0f, 0.0f });
            player.PlaySlimeAbilityMotion("player_base_press_land", false, 1.0f, 0.02f, 0.05f);
            player.TriggerSlimeImpulse({ 1.85f, 0.52f, 1.85f }, 0.16f);
        }
        return;
    }

    if (timer_ <= 0.0f) {
        Finish(player);
    }
}

void PlayerBaseCombatController::DamageBash(Player& player) {
    PhysicsQueryFilter filter;
    filter.mask = kEnemy;
    filter.ignoredObject = &player;
    const Vector3 center = player.GetWorldPosition() + direction_ * 1.0f + Vector3{ 0.0f, 0.62f, 0.0f };
    for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, 1.38f, filter)) {
        Object3d* target = FindEnemyRoot(hit.object);
        if (!target || !hitTargets_.insert(target).second) {
            continue;
        }
        DispatchDamage(player, target, 9.0f,
            { direction_.x * 10.0f, 4.5f, direction_.z * 10.0f });
        VFXSequencer::PlayOneShot(
            "player_base_bash_hit_cue",
            target->GetWorldPosition() + Vector3{ 0.0f, 0.55f, 0.0f });
    }
}

void PlayerBaseCombatController::DamagePress(Player& player, bool landingImpact) {
    PhysicsQueryFilter filter;
    filter.mask = kEnemy;
    filter.ignoredObject = &player;
    const Vector3 center = player.GetWorldPosition() + Vector3{ 0.0f, landingImpact ? 0.32f : -0.10f, 0.0f };
    const float radius = landingImpact ? 2.35f : 1.48f;
    for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, radius, filter)) {
        Object3d* target = FindEnemyRoot(hit.object);
        if (!target || !hitTargets_.insert(target).second) {
            continue;
        }
        Vector3 knockback = NormalizePlanar(target->GetWorldPosition() - center);
        DispatchDamage(player, target, landingImpact ? 8.0f : 12.0f,
            { knockback.x * 7.5f, landingImpact ? 7.0f : 10.0f, knockback.z * 7.5f });
        VFXSequencer::PlayOneShot(
            "player_base_press_enemy_hit_cue",
            target->GetWorldPosition() + Vector3{ 0.0f, 0.45f, 0.0f },
            landingImpact ? Vector3{ 1.15f, 1.15f, 1.15f } : Vector3{ 0.86f, 0.86f, 0.86f });
        pressHitEnemy_ = !landingImpact;
    }
}

void PlayerBaseCombatController::Finish(Player& player) {
    const bool consumeBufferedAttack =
        inputBufferTimer_ > 0.0f &&
        !player.isDead &&
        !player.IsCinematicLocked() &&
        !player.IsEnemyMorphed() &&
        !player.GetCarriedEnemy();

    player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
    player.StopSlimeAbilityMotion(0.06f);
    if (!player.isDead && !player.IsCinematicLocked()) {
        player.SetIsControlActive(true);
    }
    phase_ = Phase::Idle;
    timer_ = 0.0f;
    effectTimer_ = 0.0f;
    inputBufferTimer_ = 0.0f;
    pressHitEnemy_ = false;
    hitTargets_.clear();

    if (consumeBufferedAttack) {
        if (player.IsGrounded()) {
            BeginBash(player);
        } else {
            BeginPress(player);
        }
    }
}

bool PlayerBaseCombatController::CanBreakImpactGate() const {
    return phase_ == Phase::BashActive || phase_ == Phase::PressFall;
}

PlayerBaseCombatController::ReplayState PlayerBaseCombatController::CaptureReplayState() const {
    ReplayState state;
    state.phase = static_cast<int>(phase_);
    state.direction = direction_;
    state.timer = timer_;
    state.effectTimer = effectTimer_;
    state.inputBufferTimer = inputBufferTimer_;
    state.pressHitEnemy = pressHitEnemy_;
    return state;
}

void PlayerBaseCombatController::RestoreReplayState(const ReplayState& state) {
    phase_ = static_cast<Phase>(std::clamp(
        state.phase,
        static_cast<int>(Phase::Idle),
        static_cast<int>(Phase::PressRecovery)));
    direction_ = NormalizePlanar(state.direction);
    timer_ = std::max(0.0f, state.timer);
    effectTimer_ = state.effectTimer;
    inputBufferTimer_ = std::max(0.0f, state.inputBufferTimer);
    pressHitEnemy_ = state.pressHitEnemy;
    // リプレイ復元後に既に命中済みの実体参照を持ち越さないようにします。
    hitTargets_.clear();
}
