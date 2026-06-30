#define NOMINMAX
#include "PlayerDeathAnimation.h"

#include "Player.h"
#include "MeshEffectManager.h"
#include "ParticleSystem.h"
#include "GPUParticleManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kHitStopTime = 0.10f;
constexpr float kBurstTime = 0.38f;
constexpr float kFadeReadyTime = 0.88f;
constexpr float kFinishTime = 1.08f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr const char* kDeathCoreFlashEffect = "Resources/json/effect/effect_player_death_slime_core_flash.json";
constexpr const char* kDeathSplashRingEffect = "Resources/json/effect/effect_player_death_slime_splash_ring.json";
constexpr const char* kDeathSplashArcEffect = "Resources/json/effect/effect_player_death_slime_splash_arc.json";
constexpr const char* kDeathSplashStreakPreset = "player_death_slime_splash_streaks";
constexpr const char* kDeathSoftMistPreset = "player_death_slime_soft_mist";

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOutCubic(float value) {
    value = Clamp01(value);
    const float inv = 1.0f - value;
    return 1.0f - inv * inv * inv;
}

float EaseInOut(float value) {
    value = Clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

Vector3 MakeSafeScale(const Vector3& scale) {
    return {
        std::abs(scale.x) > 0.001f ? scale.x : 1.0f,
        std::abs(scale.y) > 0.001f ? scale.y : 1.0f,
        std::abs(scale.z) > 0.001f ? scale.z : 1.0f
    };
}
}

void PlayerDeathAnimation::Start(Player* player, bool finalDeath) {
    timer_ = 0.0f;
    puddleEmitTimer_ = 0.0f;
    active_ = true;
    finalDeath_ = finalDeath;
    burstEmitted_ = false;
    collisionStored_ = false;
    childVisible_.clear();

    if (!player) {
        return;
    }

    baseScale_ = MakeSafeScale(player->GetScale());
    baseRotation_ = player->GetRotation();
    baseVisible_ = player->GetIsVisible();
    baseCollisionAttribute_ = player->GetCollisionAttribute();
    baseCollisionMask_ = player->GetCollisionMask();
    collisionStored_ = true;
    player->SetCollisionAttribute(0);
    player->SetCollisionMask(0);

    childVisible_.reserve(player->GetChildren().size());
    for (Object3d* child : player->GetChildren()) {
        childVisible_.push_back(child ? child->GetIsVisible() : false);
    }

    player->SetIsVisible(true);
    for (Object3d* child : player->GetChildren()) {
        if (child) {
            child->SetIsVisible(true);
        }
    }
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });
}

void PlayerDeathAnimation::Update(Player* player, float deltaTime) {
    if (!active_ || !player) {
        return;
    }

    timer_ += std::max(0.0f, deltaTime);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });

    if (timer_ < kBurstTime) {
        ApplyPreBurstPose(player);
        return;
    }

    if (!burstEmitted_) {
        burstEmitted_ = true;
        EmitBurst(player);
        HidePlayerBody(player);
    }

    puddleEmitTimer_ -= deltaTime;
    if (timer_ < kFadeReadyTime && puddleEmitTimer_ <= 0.0f) {
        EmitPuddle(player);
        puddleEmitTimer_ = finalDeath_ ? 0.065f : 0.085f;
    }
}

void PlayerDeathAnimation::RestoreVisual(Player* player) {
    active_ = false;
    if (!player) {
        return;
    }

    player->SetIsVisible(baseVisible_);
    const auto& children = player->GetChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        Object3d* child = children[i];
        if (!child) {
            continue;
        }
        const bool visible = i < childVisible_.size() ? childVisible_[i] : true;
        child->SetIsVisible(visible);
        child->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    player->SetScale(baseScale_);
    player->SetRotation(baseRotation_);
    if (collisionStored_) {
        player->SetCollisionAttribute(baseCollisionAttribute_);
        player->SetCollisionMask(baseCollisionMask_);
    }
    player->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    player->UpdateLocalMatrix();
    player->UpdateWorldMatrix();
}

bool PlayerDeathAnimation::IsReadyForFade() const {
    return active_ && timer_ >= kFadeReadyTime;
}

bool PlayerDeathAnimation::IsFinished() const {
    return active_ && timer_ >= kFinishTime;
}

void PlayerDeathAnimation::ApplyPreBurstPose(Player* player) {
    if (!player) {
        return;
    }

    const float hitT = Clamp01(timer_ / kHitStopTime);
    const float chargeT = Clamp01((timer_ - kHitStopTime) / (kBurstTime - kHitStopTime));
    const float charge = EaseInOut(chargeT);
    const float pulse = std::sin(chargeT * kPi);
    const float tinyShake = std::sin(timer_ * 84.0f) * (1.0f - chargeT) * 0.035f;

    Vector3 scale = baseScale_;
    if (timer_ < kHitStopTime) {
        const float flashPulse = std::sin(hitT * kPi);
        scale.x *= 1.0f + flashPulse * 0.10f;
        scale.y *= 1.0f - flashPulse * 0.08f;
        scale.z *= 1.0f + flashPulse * 0.10f;
    } else {
        scale.x *= 1.0f + charge * 0.42f + pulse * 0.08f;
        scale.y *= 1.0f + charge * 0.10f - pulse * 0.05f;
        scale.z *= 1.0f + charge * 0.38f + pulse * 0.06f;
    }

    Vector3 rotation = baseRotation_;
    rotation.z += tinyShake;
    rotation.x += std::sin(timer_ * 50.0f) * (1.0f - chargeT) * 0.025f;

    const float flash = std::max(0.0f, 1.0f - timer_ / 0.20f);
    const Vector4 color = {
        0.42f + flash * 0.58f,
        0.92f + flash * 0.08f,
        1.00f,
        1.0f
    };

    player->SetScale(scale);
    player->SetRotation(rotation);
    player->SetColor(color);
    for (Object3d* child : player->GetChildren()) {
        if (child) {
            child->SetColor(color);
        }
    }
    player->UpdateLocalMatrix();
    player->UpdateWorldMatrix();
}

void PlayerDeathAnimation::EmitBurst(Player* player) {
    if (!player) {
        return;
    }

    ParticleSystem* particle = player->GetParticleSystem();
    const Vector3 origin = player->GetWorldPosition();
    const Vector3 center = origin + Vector3{ 0.0f, baseScale_.y * 0.45f, 0.0f };
    const Vector3 foot = origin + Vector3{ 0.0f, 0.08f, 0.0f };

    if (particle) {
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        particle->SpawnParticles(
            center,
            finalDeath_ ? 24 : 18,
            finalDeath_ ? 5.6f : 4.8f,
            &up,
            124.0f,
            finalDeath_ ? Vector4{ 0.38f, 0.72f, 0.95f, 0.98f } : Vector4{ 0.38f, 0.95f, 1.0f, 0.96f },
            finalDeath_ ? Vector4{ 0.02f, 0.10f, 0.18f, 0.0f } : Vector4{ 0.08f, 0.42f, 0.72f, 0.0f },
            0.22f,
            finalDeath_ ? 0.82f : 0.68f,
            0.30f,
            0.035f
        );

        Vector3 low = { 0.0f, 0.08f, 0.0f };
        particle->SpawnParticles(
            foot,
            finalDeath_ ? 14 : 10,
            finalDeath_ ? 2.7f : 2.25f,
            &low,
            178.0f,
            finalDeath_ ? Vector4{ 0.25f, 0.58f, 0.78f, 0.80f } : Vector4{ 0.26f, 0.88f, 1.0f, 0.78f },
            Vector4{ 0.04f, 0.22f, 0.34f, 0.0f },
            0.38f,
            0.92f,
            0.20f,
            0.055f
        );
    }

    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager* meshEffects = MeshEffectManager::GetInstance();
        meshEffects->SpawnEffectAt(kDeathCoreFlashEffect, center, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
        meshEffects->SpawnEffectAt(kDeathSplashRingEffect, foot, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
        meshEffects->SpawnRingWaveEffect(foot);

        const int arcCount = finalDeath_ ? 7 : 5;
        for (int i = 0; i < arcCount; ++i) {
            const float ratio = static_cast<float>(i) / static_cast<float>(arcCount);
            const float angle = ratio * kTwoPi + (finalDeath_ ? 0.23f : 0.0f);
            Vector3 arcPos = center;
            arcPos.x += std::cos(angle) * 0.20f;
            arcPos.y += 0.08f + 0.05f * static_cast<float>(i % 2);
            arcPos.z += std::sin(angle) * 0.20f;

            Vector3 arcRot = {
                0.32f + 0.08f * static_cast<float>(i % 2),
                angle,
                (i % 2 == 0 ? 0.26f : -0.22f)
            };
            const float arcScale = finalDeath_ ? 1.18f : 1.0f;
            meshEffects->SpawnEffectAt(kDeathSplashArcEffect, arcPos, arcRot, { arcScale, arcScale, arcScale });
        }
    }

    GPUParticleManager* gpuParticles = GPUParticleManager::GetInstance();
    if (gpuParticles && gpuParticles->IsInitialized()) {
        gpuParticles->Emit(kDeathSplashStreakPreset, center);
        gpuParticles->Emit(kDeathSplashStreakPreset, center + Vector3{ 0.0f, 0.22f, 0.0f });
        gpuParticles->Emit(kDeathSoftMistPreset, foot);
    }
}

void PlayerDeathAnimation::EmitPuddle(Player* player) {
    if (!player || !player->GetParticleSystem()) {
        return;
    }

    Vector3 low = { 0.0f, 0.05f, 0.0f };
    const Vector3 foot = player->GetWorldPosition() + Vector3{ 0.0f, 0.06f, 0.0f };
    player->GetParticleSystem()->SpawnParticles(
        foot,
        finalDeath_ ? 6 : 5,
        finalDeath_ ? 1.25f : 1.05f,
        &low,
        175.0f,
        finalDeath_ ? Vector4{ 0.18f, 0.48f, 0.70f, 0.44f } : Vector4{ 0.20f, 0.82f, 1.0f, 0.46f },
        Vector4{ 0.04f, 0.18f, 0.28f, 0.0f },
        0.34f,
        0.76f,
        0.22f,
        0.04f
    );
}

void PlayerDeathAnimation::HidePlayerBody(Player* player) {
    if (!player) {
        return;
    }

    player->SetIsVisible(false);
    for (Object3d* child : player->GetChildren()) {
        if (child) {
            child->SetIsVisible(false);
        }
    }
}
