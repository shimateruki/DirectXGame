#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

class Object3d;

// MaterialInstanceDataは、Object3dの描画設定を共有Assetとして再利用するための解決済みデータです。
struct MaterialInstanceData {
    int version = 1;
    std::string displayName;
    std::string baseAsset;

    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
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
    Vector2 textureTiling = { 1.0f, 1.0f };
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
    static MaterialInstanceData Capture(const Object3d& object);
    static void Apply(const MaterialInstanceData& material, Object3d& object);

    // baseAssetを再帰的に解決します。循環参照と深すぎる継承はエラーにします。
    static bool LoadResolved(
        const std::string& assetPath,
        MaterialInstanceData& material,
        std::string* errorMessage = nullptr);
    static bool Save(
        const std::string& assetPath,
        const MaterialInstanceData& material,
        std::string* errorMessage = nullptr);

    static std::vector<std::string> Discover(
        const std::string& rootDirectory = "Resources/json/material_instances");
};
