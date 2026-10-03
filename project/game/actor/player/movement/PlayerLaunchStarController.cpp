#define NOMINMAX
#include "PlayerLaunchStarController.h"
#include "game/actor/player/Player.h"
#include "GPUParticleManager.h"
#include <algorithm>
#include <cmath>

void PlayerLaunchStarController::Start(Player& player, PlayerSlimeAnimator& animator, const Vector3& destination, float arcHeight, float duration)
{
    if (launchStarActive_ || player.isDead || player.IsCinematicLocked()) {
        return;
    }

    launchStarActive_ = true;
    launchStarSavedControlActive_ = player.IsControlActive();
    launchStarSavedCollisionAttribute_ = player.GetCollisionAttribute();
    launchStarSavedCollisionMask_ = player.GetCollisionMask();
    launchStarStart_ = player.GetWorldPosition();
    launchStarDestination_ = destination;
    launchStarArcHeight_ = (std::max)(2.0f, arcHeight);
    launchStarDuration_ = (std::max)(0.35f, duration);
    launchStarTimer_ = 0.0f;
    launchStarTrailTimer_ = 0.0f;

    player.SetIsControlActive(false);
    player.SetVelocity({ 0.0f, 0.0f, 0.0f });
    player.SetCollisionMask(0);

    Vector3 direction = launchStarDestination_ - launchStarStart_;
    direction.y = 0.0f;
    if (Math::Length(direction) > 0.001f) {
        direction = Math::Normalize(direction);
        player.SetMoveYaw(std::atan2(direction.x, direction.z));
        animator.SetMode(PlayerSlimeAnimator::Mode::Jump);
        animator.SetMotionDirection(direction);
    }
    player.TriggerSlimeImpulse({ 0.78f, 1.28f, 0.78f }, 0.2f);
    player.StartEvasionInvincibility(launchStarDuration_ + 0.2f);

    if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
        gpuParticleManager->Emit("launch_star_burst", launchStarStart_ + Vector3{ 0.0f, 0.7f, 0.0f });
    }
}

void PlayerLaunchStarController::Cancel(Player& player, bool restoreControl)
{
    if (!launchStarActive_) {
        return;
    }

    launchStarActive_ = false;
    launchStarTimer_ = 0.0f;
    launchStarTrailTimer_ = 0.0f;
    player.SetCollisionAttribute(launchStarSavedCollisionAttribute_);
    player.SetCollisionMask(launchStarSavedCollisionMask_);
    player.SetVelocity({ 0.0f, 0.0f, 0.0f });
    player.SetIsControlActive(
        restoreControl && launchStarSavedControlActive_ && !player.isDead && !player.IsCinematicLocked());
}

void PlayerLaunchStarController::Update(Player& player, PlayerSlimeAnimator& animator, float deltaTime, bool updateAnimation)
{
    if (!launchStarActive_) {
        return;
    }

    launchStarTimer_ += (std::max)(0.0f, deltaTime);
    const float t = std::clamp(launchStarTimer_ / launchStarDuration_, 0.0f, 1.0f);
    const float eased = t * t * (3.0f - 2.0f * t);

    Vector3 position = launchStarStart_ + (launchStarDestination_ - launchStarStart_) * eased;
    position.y += std::sin(t * 3.14159265f) * launchStarArcHeight_;
    player.SetTranslate(position);
    player.SetVelocity({ 0.0f, 0.0f, 0.0f });
    player.SetIsControlActive(false);

    Vector3 direction = launchStarDestination_ - launchStarStart_;
    direction.y = 0.0f;
    if (Math::Length(direction) > 0.001f) {
        direction = Math::Normalize(direction);
        animator.SetMode(PlayerSlimeAnimator::Mode::Jump);
        animator.SetMotionDirection(direction);
    }

    if (updateAnimation) {
        animator.Update(&player, deltaTime);
    }

    launchStarTrailTimer_ -= deltaTime;
    if (launchStarTrailTimer_ <= 0.0f) {
        if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
            gpuParticleManager->Emit("launch_star_trail", position);
        }
        launchStarTrailTimer_ = 0.045f;
    }

    if (t < 1.0f) {
        return;
    }

    launchStarActive_ = false;
    player.SetTranslate(launchStarDestination_);
    player.SetRespawnPosition(launchStarDestination_);
    player.SetVelocity({ 0.0f, -1.0f, 0.0f });
    player.SetCollisionAttribute(launchStarSavedCollisionAttribute_);
    player.SetCollisionMask(launchStarSavedCollisionMask_);
    player.SetIsControlActive(launchStarSavedControlActive_);
    player.TriggerSlimeImpulse({ 1.18f, 0.82f, 1.18f }, 0.18f);

    if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
        gpuParticleManager->Emit("launch_star_burst", launchStarDestination_ + Vector3{ 0.0f, 0.45f, 0.0f });
    }
}

