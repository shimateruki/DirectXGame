#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

namespace engine { class Object3d; }

// MaterialInstanceDataは、Object3dの描画設定を共有Assetとして再利用するための解決済みデータです。
struct MaterialInstanceData {
    int version = 1;
    std::string displayName;
    std::string baseAsset;

    engine::Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    int blendMode = 1;
    int materialType = 0;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float emissive = 1.0f;
    bool enableLighting = true;
    bool enableNormalMap = false;
    bool enableEnvMap = false;
    float envIntensity = 1.0f;

    std::string texturePath;
    std::string normalMapPath;
    std::string ormMapPath;
    engine::Vector2 textureTiling = { 1.0f, 1.0f };
    bool autoTextureTiling = false;

    // 既存の特殊Materialが共有している調整値もInstanceへ含めます。
    float waveSpeed = 1.0f;
    float waveHeight = 0.2f;
    float waveFrequency = 2.0f;
    float flowSpeedX = 0.0f;
    float flowSpeedY = 0.0f;
    float effectType = 0.0f;
    float effectScale = 1.0f;
    float effectSoftness = 0.5f;
    float effectIntensity = 1.0f;
    float billboardScale = 1.0f;
    float effectScaleX = 1.0f;
    float effectScaleY = 1.0f;
    float effectScaleZ = 1.0f;
};

// MaterialInstanceAssetは、継承可能なMaterial Instance JSONの入出力とObject3dへの適用を担当します。
class MaterialInstanceAsset final {
public:
    /// <summary>オブジェクトの現在のマテリアル設定を共有アセット用データに取り出す。</summary>
    static MaterialInstanceData Capture(const engine::Object3d& object);

    /// <summary>共有アセットのマテリアル設定をオブジェクトへ適用する。</summary>
    static void Apply(const MaterialInstanceData& material, engine::Object3d& object);

    // baseAsset を親から先に読み、子 JSON にある値だけ上書きする。
    // 循環参照と 16 段を超える継承は失敗する。失敗時の material は部分的に更新され得る。
    static bool LoadResolved(
        const std::string& assetPath,
        MaterialInstanceData& material,
        std::string* errorMessage = nullptr);
    // 現在の値を全項目保存する。baseAsset 指定時も差分だけの保存にはならない。
    static bool Save(
        const std::string& assetPath,
        const MaterialInstanceData& material,
        std::string* errorMessage = nullptr);

    static std::vector<std::string> Discover(
        const std::string& rootDirectory = "Resources/json/material_instances");
};
