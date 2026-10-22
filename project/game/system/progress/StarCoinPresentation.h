#pragma once
#include "Object3d.h"

// 銀スターのモデルと粒子名を共有し、取得時も同じ配色の演出を選ぶ。
namespace StarCoinPresentation {
inline constexpr const char* kGoldModel = "Gimmicks/star";
inline constexpr const char* kSilverModel = "Gimmicks/star/star_silver.gltf";
inline constexpr const char* kSilverIdle = "star_sparkle_silver";
inline constexpr const char* kSilverGet = "star_sparkleGet_silver";

inline bool IsSilver(const engine::Object3d& object) {
    return object.GetModelName() == kSilverModel;
}

inline void ApplyPreviouslyCollected(engine::Object3d& object) {
    // SetModelの自動フィットでは配置時に調整した取得範囲を変えない。
    const auto collider = object.GetColliderConfig();
    object.SetModel(kSilverModel);
    object.SetColliderConfig(collider);
    object.SetLodEnabled(false);
    object.SetTexture("Resources/sprite/common/white.png");
    object.SetOrmMap("");
    object.SetMaterialType(0);
    object.SetEnableEnvMap(true);
    object.SetEnvIntensity(0.65f);
    object.SetEmissive(1.25f);
    object.SetParticleName("");
    object.SetMeshEffect1Name("");
    object.SetMeshEffect2Name("");
    object.SetGPUParticleName(kSilverIdle);
}
// 編集時に銀モデルで保存された配置も、未取得のセーブでは金色へ戻す。
inline void RestoreUncollectedIfSilver(engine::Object3d& object) {
    if (!IsSilver(object))
        return;
    const auto collider = object.GetColliderConfig();
    object.SetModel(kGoldModel);
    object.SetColliderConfig(collider);
    object.SetLodEnabled(false);
    object.SetTexture("");
    object.SetOrmMap("");
    object.SetMaterialType(0);
    object.SetColor({1.0f, 0.74f, 0.08f, 1.0f});
    object.SetMetallic(0.18f);
    object.SetRoughness(0.32f);
    object.SetEnableEnvMap(false);
    object.SetEmissive(1.65f);
    object.SetGPUParticleName("star_sparkle");
}
} // namespace StarCoinPresentation
