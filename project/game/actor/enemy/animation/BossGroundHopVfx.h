#pragma once

// BossGroundLocomotionの離着地に合わせる粒子発生。移動状態や攻撃の選択は呼出側が管理する。

#include "GpuParticleManager.h"

// 通常移動の着地だけに使う短い粉塵。攻撃の予告リングとは見分けられる形にします。
namespace BossGroundHopVfx {

inline void EmitPuff(const engine::Vector3& groundPosition, const engine::Vector4& color, float radius, bool landing) {
    GpuParticleManager* particles = GpuParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    const char* presetName = landing ? "player_land_dust" : "player_jump_dust";
    const auto preset = particles->GetPresets().find(presetName);
    if (preset == particles->GetPresets().end()) {
        return;
    }

    GpuParticleConfig config = preset->second;
    config.emitPos = groundPosition;
    config.emitArea = {radius * 0.55f, 0.04f, radius * 0.55f};
    config.shapeRadius = radius * 0.62f;
    config.emitVelocity = {0.0f, landing ? 0.68f : 0.46f, 0.0f};
    config.velocityVariance = landing ? 2.2f : 1.5f;
    config.emitLife = landing ? 0.48f : 0.34f;
    const float sizeScale = radius / 1.5f;
    config.baseSize *= sizeScale;
    config.midSize *= sizeScale;
    config.endSize *= sizeScale;
    config.baseColor = color;
    config.midColor = {color.x * 0.85f, color.y * 0.85f, color.z * 0.85f, color.w * 0.55f};
    config.endColor = {color.x * 0.6f, color.y * 0.6f, color.z * 0.6f, 0.0f};
    config.isLooping = false;
    particles->EmitFromConfig(config);
}

} // namespace BossGroundHopVfx
