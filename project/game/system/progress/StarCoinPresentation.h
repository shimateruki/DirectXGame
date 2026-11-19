#pragma once

// GameDataManagerの取得履歴をスターコインの見た目へ反映する。取得判定や保存の確定はここでは行わない。
#include "Object3D.h"

// 銀スターのモデルと粒子名を共有し、取得時も同じ配色の演出を選ぶ。
namespace StarCoinPresentation {
inline constexpr const char* kGoldModel = "Gimmicks/star";
inline constexpr const char* kSilverModel = "Gimmicks/star/star_silver.gltf";
inline constexpr const char* kSilverIdle = "shared/star_sparkle_silver";
inline constexpr const char* kSilverGet = "shared/star_sparkleGet_silver";

inline bool IsSilver(const engine::Object3D& object) {
    return object.GetModelName() == kSilverModel;
}

inline void ApplyPreviouslyCollected(engine::Object3D& object) {
    // SetModelの自動フィットでは配置時に調整した取得範囲を変えない。
    const auto collider = object.GetColliderConfig();
    object.SetModel(kSilverModel);
    object.SetColliderConfig(collider);
    // 銀モデルへの変更と併せて、元の金用の反射・粒子設定を掃き出し、取得済み用の輝きにそろえます。
    object.SetTexture("Resources/sprite/common/white.png");
    object.SetOrmMap("");
    object.SetMaterialType(0);
    object.SetEnableEnvMap(true);
    object.SetEnvIntensity(0.65f);
    object.SetEmissive(1.25f);
    object.SetParticleName("");
    object.SetMeshEffect1Name("");
    object.SetMeshEffect2Name("");
    object.SetGpuParticleName(kSilverIdle);
}
// 編集時に銀モデルで保存された配置も、未取得のセーブでは金色へ戻す。
inline void RestoreUncollectedIfSilver(engine::Object3D& object) {
    if (!IsSilver(object))
        return;
    // 見た目だけを未取得の金へ戻し、配置時の取得範囲は残します。
    const auto collider = object.GetColliderConfig();
    object.SetModel(kGoldModel);
    object.SetColliderConfig(collider);
    object.SetTexture("");
    object.SetOrmMap("");
    object.SetMaterialType(0);
    object.SetColor({1.0f, 0.74f, 0.08f, 1.0f});
    object.SetMetallic(0.18f);
    object.SetRoughness(0.32f);
    object.SetEnableEnvMap(false);
    object.SetEmissive(1.65f);
    object.SetGpuParticleName("shared/star_sparkle");
}
} // namespace StarCoinPresentation
