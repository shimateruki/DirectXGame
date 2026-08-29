#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>
#include <memory>
#include <string>

class BaseScene;
class Object3d;
class Object3dCommon;

// DecalSpawnDescは、地面や壁へ沿わせる薄い投影面の生成設定です。
struct DecalSpawnDesc {
    std::string name = "Decal";
    std::string texturePath = "Resources/sprite/common/circle2.png";
    Vector2 size = { 2.0f, 2.0f };
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float rotationRadians = 0.0f;
    float depthOffset = 0.012f;
    float lifetime = 0.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.35f;
    float emissive = 1.0f;
    int blendMode = 1;
    bool transient = false;
};

// DecalSystemは、Raycast結果から法線へ沿うDecal Objectを生成します。
// 現在のForward描画を保ったまま使えるMesh Decal方式です。
class DecalSystem final {
public:
    static std::unique_ptr<Object3d> Create(
        Object3dCommon* common,
        const Vector3& surfacePoint,
        const Vector3& surfaceNormal,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    static Object3d* Spawn(
        BaseScene& scene,
        const Vector3& surfacePoint,
        const Vector3& surfaceNormal,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    static Object3d* SpawnFromRaycast(
        BaseScene& scene,
        const Vector3& rayStart,
        const Vector3& rayDirection,
        float maxDistance,
        uint32_t collisionMask,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    static void AlignToSurface(
        Object3d& object,
        const Vector3& surfacePoint,
        const Vector3& surfaceNormal,
        float rotationRadians,
        float depthOffset);
};
