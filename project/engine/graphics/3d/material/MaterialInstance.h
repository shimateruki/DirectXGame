#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

namespace engine { class Object3D; }

// MaterialInstanceDataは、Object3dの描画設定を共有Assetとして再利用するための解決済みデータです。
// 関連：MaterialInstanceAsset（読込・適用）、Object3D（材質設定）
struct MaterialInstanceData {
    int version = 1;
    std::string displayName;
    std::string baseAsset;

    engine::Vector4 color = engine::Colors::White;
    int blendMode = 1;
    int materialType = 0;
    std::string shaderMaterialId;
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

// 担当：継承可能な材質JSONを読み書きし、Object3dへ適用する。
// 関連：MaterialInstanceData（解決済み設定）、Object3D / MeshRenderer（適用先）
// 主な入口：Capture / Apply
class MaterialInstanceAsset final {
public:
    /// <summary>オブジェクトの現在のマテリアル設定を共有アセット用データに取り出す。</summary>
    static MaterialInstanceData Capture(const engine::Object3D& object);

    /// <summary>共有アセットのマテリアル設定をオブジェクトへ適用する。</summary>
    static void Apply(const MaterialInstanceData& material, engine::Object3D& object);

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
