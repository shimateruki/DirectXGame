#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>
#include <memory>
#include <string>

class BaseScene;
namespace engine { class Object3d; }
class Object3dCommon;

/// <summary>地面や壁へ沿わせる薄い投影面の生成設定。</summary>
struct DecalSpawnDesc {
    std::string name = "Decal";
    std::string texturePath = "Resources/sprite/effect/thunder/thunder_ground_scorch.dds";
    engine::Vector2 size = { 2.0f, 2.0f };
    engine::Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float rotationRadians = 0.0f;
    float depthOffset = 0.012f;
    float lifetime = 0.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.35f;
    float emissive = 1.0f;
    int blendMode = 1;
    bool transient = false;
};

/// <summary>表面位置と法線からForward描画用のメッシュデカールを生成する。</summary>
class DecalSystem final {
public:
    /// <summary>指定面に配置したデカールObjectを生成し、呼び出し側へ所有権を渡す。</summary>
    static std::unique_ptr<engine::Object3d> Create(
        Object3dCommon* common,
        const engine::Vector3& surfacePoint,
        const engine::Vector3& surfaceNormal,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    /// <summary>指定面にデカールを生成してシーンへ登録する。</summary>
    static engine::Object3d* Spawn(
        BaseScene& scene,
        const engine::Vector3& surfacePoint,
        const engine::Vector3& surfaceNormal,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    /// <summary>レイが最初に当たった面へデカールを生成する。</summary>
    static engine::Object3d* SpawnFromRaycast(
        BaseScene& scene,
        const engine::Vector3& rayStart,
        const engine::Vector3& rayDirection,
        float maxDistance,
        uint32_t collisionMask,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    /// <summary>指定法線へデカールの向きを合わせ、面からわずかに浮かせる。</summary>
    static void AlignToSurface(
        engine::Object3d& object,
        const engine::Vector3& surfacePoint,
        const engine::Vector3& surfaceNormal,
        float rotationRadians,
        float depthOffset);
};
