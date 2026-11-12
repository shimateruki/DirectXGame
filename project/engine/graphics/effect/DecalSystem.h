#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>
#include <memory>
#include <string>

class BaseScene;
namespace engine { class Object3D; }
namespace engine { class Object3DCommon; }

/// <summary>地面や壁へ沿わせる薄い投影面の生成設定。</summary>
// 関連：DecalSystem（この型を使う機能の本体）
struct DecalSpawnDesc {
    std::string name = "Decal";
    std::string texturePath = "Resources/sprite/effect/thunder/thunder_ground_scorch.dds";

    engine::Vector2 size = { 2.0f, 2.0f };
    engine::Vector4 color = engine::Colors::White;
    float rotationRadians = 0.0f; // 面の法線を軸とした回転（ラジアン）。
    float depthOffset = 0.012f; // 面から浮かせる距離。重なりのちらつきを抑える。

    float lifetime = 0.0f; // 寿命（秒）。0は永続。
    float fadeIn = 0.0f; // 出現フェードの秒数。
    float fadeOut = 0.35f; // 消失フェードの秒数。

    float emissive = 1.0f;
    int blendMode = 1;

    bool transient = false; // 一時的な描画物として扱うか。
};

// 担当：表面位置と法線に沿う、Forward描画用のデカールを生成する。
// 関連：DecalSpawnDesc（生成条件）、Object3D（デカール本体）、BaseScene（登録先）
// 主な入口：Create / Spawn / AlignToSurface
class DecalSystem final {
public:
    /// <summary>指定面に配置したデカールObjectを生成し、呼び出し側へ所有権を渡す。</summary>
    static std::unique_ptr<engine::Object3D> Create(
        engine::Object3DCommon* common,
        const engine::Vector3& surfacePoint,
        const engine::Vector3& surfaceNormal,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    /// <summary>指定面にデカールを生成してシーンへ登録する。</summary>
    static engine::Object3D* Spawn(
        BaseScene& scene,
        const engine::Vector3& surfacePoint,
        const engine::Vector3& surfaceNormal,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    /// <summary>レイが最初に当たった面へデカールを生成する。</summary>
    static engine::Object3D* SpawnFromRaycast(
        BaseScene& scene,
        const engine::Vector3& rayStart,
        const engine::Vector3& rayDirection,
        float maxDistance,
        uint32_t collisionMask,
        const DecalSpawnDesc& desc = DecalSpawnDesc{});

    /// <summary>指定法線へデカールの向きを合わせ、面からわずかに浮かせる。</summary>
    static void AlignToSurface(
        engine::Object3D& object,
        const engine::Vector3& surfacePoint,
        const engine::Vector3& surfaceNormal,
        float rotationRadians,
        float depthOffset);
};
