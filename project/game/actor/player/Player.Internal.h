#pragma once
// 分割実装専用の定数・補助関数。外部からは公開ヘッダーを使用します。
#define NOMINMAX
#include "Player.h"
#include "LightManager.h"
#include "game/actor/player/copy/PlayerCopyAbilityController.h"
#include "game/actor/player/copy/PlayerCopyTypeCatalog.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include "EventManager.h"
#include "CameraManager.h"
#include "IMoveStrategy.h"
#include "game/actor/player/state/PlayerState.h"
#include "PostEffect.h"
#include "GameDataManager.h"
#include "GameplayStatusManager.h"
#include "SceneManager.h"
#include "StageManager.h"
#include"Winapp.h"
#include <DebugConsole.h>
#include <algorithm>
#include <CollisionManager.h>
#include "DirectXCommon.h"
#include"BaseEnemy.h"
#include "Bullet.h"
#include "GimmickHookPullBlock.h"
#include "GimmickBreakableBlock.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include <cmath>
#include <unordered_set>

namespace PlayerImplementation {
    inline constexpr float kPi = 3.1415926535f;
    inline constexpr float kTwoPi = kPi * 2.0f;
    inline constexpr float kEnemyMorphDuration = 5.0f;
    inline constexpr float kEnemyMorphParticleInterval = 0.12f;
    inline constexpr float kEnemyMorphReleaseDuration = 0.42f;
    inline constexpr float kEnemyMorphExpiredReleaseDuration = 0.32f;
    inline constexpr float kEnemyMorphReleaseBurstTime = 0.11f;
    inline constexpr float kEnemyMorphExpiredReleaseBurstTime = 0.075f;
    inline constexpr float kAbsorbEffectDuration = 0.24f;
    inline constexpr float kAbsorbEffectParticleInterval = 0.035f;
    inline constexpr float kPlayerModelYawOffset = kPi;
    inline constexpr const char* kPinkSlimeMorphBurstPreset = "pink_slime_morph_burst";
    inline constexpr const char* kPinkSlimeMorphIdlePreset = "pink_slime_morph_glint";
    inline constexpr float kPinkSlimeMorphParticleInterval = 0.78f;
    inline constexpr const char* kBomberMorphAuraPreset = "player_bomb_morph_aura";
    inline constexpr const char* kFireMorphAuraPreset = "player_fire_morph_aura";
    inline constexpr const char* kWindMorphAuraPreset = "player_wind_morph_aura";
    inline constexpr const char* kBomberMorphPulseEffectPath = "Resources/json/effect/effect_player_bomb_morph_pulse.json";
    inline constexpr const char* kFireMorphPulseEffectPath = "Resources/json/effect/effect_player_fire_morph_pulse.json";
    inline constexpr const char* kWindMorphPulseEffectPath = "Resources/json/effect/effect_player_wind_morph_pulse.json";
    inline constexpr float kElectricShockShakeAmount = 0.045f;
    inline constexpr const char* kElectricShockAuraEffectPath = "Resources/json/effect/effect_thunder_slime_constant_aura.json";


    // 描画用の液体には固体Colliderがないため、液面を直接評価します。
    // Water.hlsli / Magma.VS.hlsl と同じ式。波の設定は既存のInspectorに追従します。
    inline float LiquidWaveHeight(const MeshRenderer::WaterParamForGPU& p, float x, float z, bool magma) {
        if (!magma) {
            // 穏やかな海はWater.VS.hlslと同じ固定液面で判定します。
            if (p.effectType >= 1.5f) return 0.0f;
            const float s = std::max(p.waveFrequency, 0.1f) / 5.4f;
            const float t = p.time * std::max(p.waveSpeed, 0.0f);
            const float a = (x * .065f + z * .024f) * s - t * 1.3f
                + .75f * std::sin((x * -.011f + z * .017f) * s - t * .31f);
            const float b = (x * .041f + z * .025f) * s - t * 1.7f + 1.2f
                + .55f * std::sin((x * .019f + z * -.009f) * s - t * .23f);
            const float c = (x * -.022f + z * .087f) * s - t * 2.1f + 2.4f;
            auto profile = [](float phase) {
                return (std::sin(phase) - .18f * std::cos(2.0f * phase)) / 1.18f;
            };
            return std::max(p.waveHeight, 0.0f) * (.65f * profile(a) + .25f * profile(b) + .10f * profile(c));
        }
        const float viscosity = std::max(p.effectScale, .1f);
        const float t = p.time * std::max(p.waveSpeed, .05f) / (.8f + viscosity * .65f);
        const float s = .04f * std::max(p.waveFrequency, .15f) / (.85f + viscosity * .4f);
        x = x * s + p.flowSpeedX * t * .045f + t * .03f;
        z = z * s + p.flowSpeedY * t * .045f - t * .018f;
        const float ix = std::floor(x), iz = std::floor(z);
        const float fx = x - ix, fz = z - iz;
        const float u = fx * fx * (3.0f - 2.0f * fx);
        const float v = fz * fz * (3.0f - 2.0f * fz);
        auto noise = [](float nx, float nz) {
            const float n = std::sin(nx * 12.9898f + nz * 78.233f) * 43758.5453123f;
            return n - std::floor(n);
        };
        const float a = noise(ix, iz), b = noise(ix + 1, iz);
        const float c = noise(ix, iz + 1), d = noise(ix + 1, iz + 1);
        return (a + (b - a) * u + (c - a) * v * (1 - u) + (d - b) * u * v - .5f)
            * std::max(p.waveHeight, 0.0f) * .14f;
    }

    inline bool FindLethalLiquidSurface(Player& player, float& contactY) {
        auto* manager = SceneManager::GetInstance();
        if (!manager || !manager->IsPlaying() || !manager->GetCurrentScene()) return false;
        player.UpdateLocalMatrix();
        player.UpdateWorldMatrix();
        const AABB body = player.GetModelWorldAABB();
        const float cx = (body.min.x + body.max.x) * .5f;
        const float cz = (body.min.z + body.max.z) * .5f;
        // 胴体中心と足元の四隅。足場の端から触れた場合も拾います。
        const float rx = (body.max.x - body.min.x) * .3f;
        const float rz = (body.max.z - body.min.z) * .3f;
        const Vector3 probes[] = {{cx, body.min.y, cz},
            {cx-rx, body.min.y, cz-rz}, {cx+rx, body.min.y, cz-rz},
            {cx-rx, body.min.y, cz+rz}, {cx+rx, body.min.y, cz+rz}};
        bool touched = false;
        contactY = -1.0e30f;
        for (const auto& owned : manager->GetCurrentScene()->GetObjects()) {
            Object3d* liquid = owned.get();
            if (!liquid || liquid == &player || !liquid->GetIsVisible()) continue;
            const int material = liquid->GetMaterialType();
            // マグマを使った敵・弾・装飾は、通常のダメージ処理のままにします。
            if (material != 8 && !(material == 9 && (liquid->GetGimmickType() == "MagmaHazard" || liquid->GetGimmickType() == "LiquidLevel"
                || liquid->GetModelName() == "Stages/block"))) continue;
            const auto* renderer = liquid->GetMeshRenderer();
            const auto* params = renderer ? renderer->GetWaterParamData() : nullptr;
            if (!params) continue;
            const bool isWaterGrid = params->effectType < .5f ||
                (params->effectType >= 1.5f && params->effectType < 2.5f);
            if (material == 8 && !isWaterGrid) continue;
            const auto& world = liquid->GetWorldMatrix();
            // 海・溶岩池の水平面を対象にし、傾けた演出用モデルは除外します。
            if (std::abs(world.m[1][0]) > .001f || std::abs(world.m[1][2]) > .001f
                || world.m[1][1] <= .001f) continue;
            const Matrix4x4 inverse = Math::Inverse(world);
            const AABB bounds = liquid->GetModelWorldAABB();
            for (const Vector3& probe : probes) {
                const Vector3 local = Math::Transform(probe, inverse);
                float surface = 0.0f;
                if (material == 8) {
                    if (std::abs(local.x) > 1.0f || std::abs(local.z) > 1.0f) continue;
                    surface = world.m[3][1] + LiquidWaveHeight(*params, probe.x, probe.z, false);
                } else {
                    if (probe.x < bounds.min.x || probe.x > bounds.max.x
                        || probe.z < bounds.min.z || probe.z > bounds.max.z) continue;
                    surface = bounds.max.y + LiquidWaveHeight(*params, probe.x, probe.z, true) * world.m[1][1];
                }
                if (std::isfinite(surface) && body.min.y <= surface) {
                    contactY = std::max(contactY, surface);
                    touched = true;
                }
            }
        }
        return touched;
    }

    inline float NormalizeYaw(float yaw) {
        while (yaw > kPi) yaw -= kTwoPi;
        while (yaw < -kPi) yaw += kTwoPi;
        return yaw;
    }

    inline Vector3 GetStableEnemyCarryScale(Object3d* enemy) {
        if (!enemy) {
            return { 1.0f, 1.0f, 1.0f };
        }

        const Vector3 scale = enemy->GetScale();
        if (!dynamic_cast<BaseEnemy*>(enemy)) {
            return scale;
        }

        const float absX = std::abs(scale.x);
        const float absY = std::abs(scale.y);
        const float absZ = std::abs(scale.z);
        const float minScale = (std::min)({ absX, absY, absZ });
        const float maxScale = (std::max)({ absX, absY, absZ });
        if (minScale <= 0.0001f || maxScale / minScale < 1.18f) {
            return scale;
        }

        const float stableScale = std::cbrt(absX * absY * absZ);
        return { stableScale, stableScale, stableScale };
    }

    inline Vector3 ResolveTransformEuler(const Transform& transform) {
        if (!transform.isQuaternionMaster) {
            return transform.rotate;
        }

        return Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(transform.quaternion));
    }

    inline void CalculateThunderAuraShape(const Vector3& position, const Vector3& scale, Vector3& center, float& horizontalDiameter, float& verticalDiameter) {
        const float maxXZ = (std::max)({ std::abs(scale.x), std::abs(scale.z), 1.0f });
        const float yScale = (std::max)(std::abs(scale.y), 0.18f);

        horizontalDiameter = (std::max)(2.55f, maxXZ * 1.35f);
        verticalDiameter = (std::max)(0.46f, yScale * 1.28f);

        center = position;
        center.y += (std::max)(0.22f, yScale * 0.43f);
    }

    inline void EmitOuterThunderParticles(const Vector3& position, const Vector3& scale, float phase, int count, const char* presetName = "thunder_slime_idle_spark") {
        auto* gpuParticleManager = GPUParticleManager::GetInstance();
        if (!gpuParticleManager || !gpuParticleManager->IsInitialized() || count <= 0) {
            return;
        }

        Vector3 center{};
        float horizontalDiameter = 1.0f;
        float verticalDiameter = 1.0f;
        CalculateThunderAuraShape(position, scale, center, horizontalDiameter, verticalDiameter);

        const float horizontalRadius = horizontalDiameter * 0.52f;
        const float verticalRadius = verticalDiameter * 0.38f;

        for (int i = 0; i < count; ++i) {
            const float angle = phase + kTwoPi * static_cast<float>(i) / static_cast<float>(count);
            Vector3 emitPos = center;
            emitPos.x += std::cos(angle) * horizontalRadius;
            emitPos.z += std::sin(angle) * horizontalRadius;
            emitPos.y += std::sin(angle * 1.37f) * verticalRadius;
            gpuParticleManager->Emit(presetName, emitPos);
        }
    }

    inline void EmitThunderMorphBurst(const Vector3& position, const Vector3& scale) {
        EmitOuterThunderParticles(position, scale, 0.35f, 4);
    }

    inline void EmitCopyMorphPreset(const char* presetName, const Vector3& position) {
        auto* gpuParticleManager = GPUParticleManager::GetInstance();
        if (gpuParticleManager && gpuParticleManager->IsInitialized()) {
            gpuParticleManager->Emit(presetName, position);
        }
    }

    inline void SpawnCopyMorphPulse(const char* effectPath, const Vector3& position, const Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
        auto* meshEffects = MeshEffectManager::GetInstance();
        if (meshEffects) {
            meshEffects->SpawnEffectAt(effectPath, position, { 0.0f, 0.0f, 0.0f }, scale);
        }
    }

}

// =================================================================
// 初期化・更新・描画
// =================================================================

