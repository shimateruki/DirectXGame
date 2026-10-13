#include "MaterialInstance.h"

#include "Object3d.h"
#include "engine/graphics/3d/MeshRenderer.h"
#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

std::string NormalizePath(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

void SetError(std::string* destination, const std::string& message) {
    if (destination) {
        *destination = message;
    }
}

bool ReadVector2(const json& value, Vector2& destination) {
    if (!value.is_array() || value.size() < 2) return false;
    destination = { value[0].get<float>(), value[1].get<float>() };
    return true;
}

bool ReadVector4(const json& value, Vector4& destination) {
    if (!value.is_array() || value.size() < 4) return false;
    destination = {
        value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>()
    };
    return true;
}

template <typename T>
void ReadValue(const json& source, const char* key, T& destination) {
    if (source.contains(key) && !source[key].is_null()) {
        destination = source[key].get<T>();
    }
}

void OverlayMaterial(const json& source, MaterialInstanceData& material) {
    // 親から受け継いだ値は残し、子JSONに書かれた項目だけ上書きする。
    ReadValue(source, "displayName", material.displayName);
    if (source.contains("color")) ReadVector4(source["color"], material.color);
    ReadValue(source, "blendMode", material.blendMode);
    ReadValue(source, "materialType", material.materialType);
    ReadValue(source, "metallic", material.metallic);
    ReadValue(source, "roughness", material.roughness);
    ReadValue(source, "emissive", material.emissive);
    ReadValue(source, "enableLighting", material.enableLighting);
    ReadValue(source, "enableNormalMap", material.enableNormalMap);
    ReadValue(source, "enableEnvMap", material.enableEnvMap);
    ReadValue(source, "envIntensity", material.envIntensity);
    ReadValue(source, "texturePath", material.texturePath);
    ReadValue(source, "normalMapPath", material.normalMapPath);
    ReadValue(source, "ormMapPath", material.ormMapPath);
    if (source.contains("textureTiling")) ReadVector2(source["textureTiling"], material.textureTiling);
    ReadValue(source, "autoTextureTiling", material.autoTextureTiling);

    // 水面・特殊エフェクト固有の設定は通常のマテリアル項目とは別階層にある。
    if (!source.contains("special") || !source["special"].is_object()) return;
    const json& special = source["special"];
    ReadValue(special, "waveSpeed", material.waveSpeed);
    ReadValue(special, "waveHeight", material.waveHeight);
    ReadValue(special, "waveFrequency", material.waveFrequency);
    ReadValue(special, "flowSpeedX", material.flowSpeedX);
    ReadValue(special, "flowSpeedY", material.flowSpeedY);
    ReadValue(special, "effectType", material.effectType);
    ReadValue(special, "effectScale", material.effectScale);
    ReadValue(special, "effectSoftness", material.effectSoftness);
    ReadValue(special, "effectIntensity", material.effectIntensity);
    ReadValue(special, "billboardScale", material.billboardScale);
    ReadValue(special, "effectScaleX", material.effectScaleX);
    ReadValue(special, "effectScaleY", material.effectScaleY);
    ReadValue(special, "effectScaleZ", material.effectScaleZ);
}

fs::path ResolveBasePath(const fs::path& ownerPath, const std::string& baseAsset) {
    // 作業ディレクトリから参照できるパスを優先し、次に子アセットからの相対パスを試す。
    fs::path basePath(baseAsset);
    if (basePath.is_absolute() || fs::exists(basePath)) return basePath;
    const fs::path relativeToOwner = ownerPath.parent_path() / basePath;
    if (fs::exists(relativeToOwner)) return relativeToOwner;
    return basePath;
}

bool LoadResolvedInternal(
    const fs::path& assetPath,
    MaterialInstanceData& material,
    std::unordered_set<std::string>& loading,
    int depth,
    std::string* errorMessage) {
    // 継承の深さと再帰経路を制限し、循環または異常に長い参照を拒否する。
    if (depth > 16) {
        SetError(errorMessage, "Material Instanceの継承が16段を超えています: " + NormalizePath(assetPath));
        return false;
    }

    const std::string normalizedPath = NormalizePath(assetPath);
    // 現在の再帰経路だけを記録し、親子の循環参照を検出する。
    if (!loading.insert(normalizedPath).second) {
        SetError(errorMessage, "Material Instanceの継承が循環しています: " + normalizedPath);
        return false;
    }

    // 途中で失敗したときは経路集合から現在のパスを外し、次回の読み込みに影響させない。
    std::ifstream file(assetPath);
    if (!file) {
        loading.erase(normalizedPath);
        SetError(errorMessage, "Material Instanceを開けません: " + normalizedPath);
        return false;
    }

    json root;
    try {
        file >> root;
    }
    catch (const std::exception& exception) {
        loading.erase(normalizedPath);
        SetError(errorMessage, "Material Instance JSONが不正です: " + std::string(exception.what()));
        return false;
    }

    if (!root.is_object()) {
        loading.erase(normalizedPath);
        SetError(errorMessage, "Material InstanceのルートはObjectである必要があります: " + normalizedPath);
        return false;
    }

    const std::string baseAsset = root.value("baseAsset", "");
    // 親の解決済み値へ子の明示フィールドを重ねる。
    if (!baseAsset.empty()) {
        if (!LoadResolvedInternal(ResolveBasePath(assetPath, baseAsset), material, loading, depth + 1, errorMessage)) {
            loading.erase(normalizedPath);
            return false;
        }
    }

    // 親の解決を終えたあと、子の共通項目とpropertiesを重ねる。
    material.version = root.value("version", material.version);
    material.baseAsset = baseAsset;
    const json& properties = root.contains("properties") && root["properties"].is_object()
        ? root["properties"] : root;
    OverlayMaterial(properties, material);
    loading.erase(normalizedPath);
    return true;
}
}

MaterialInstanceData MaterialInstanceAsset::Capture(const Object3d& object) {
    MaterialInstanceData material;
    // まずObject3dの共通マテリアル値を取り出す。
    material.color = object.GetColor();
    material.blendMode = static_cast<int>(object.GetBlendMode());
    material.materialType = object.GetMaterialType();
    material.metallic = object.GetMetallic();
    material.roughness = object.GetRoughness();
    material.emissive = object.GetEmissive();
    material.enableLighting = object.GetEnableLighting();
    material.enableNormalMap = object.GetEnableNormalMap();
    material.enableEnvMap = object.GetEnableEnvMap();
    material.envIntensity = object.GetEnvIntensity();
    material.texturePath = object.GetTexturePath();
    material.normalMapPath = object.GetNormalMapPath();
    material.ormMapPath = object.GetOrmMapPath();
    material.textureTiling = object.GetTextureTiling();
    material.autoTextureTiling = object.GetAutoTextureTiling();

    // Rendererが特殊GPUパラメータを持つ場合だけ追加項目を取得する。
    if (const MeshRenderer* renderer = object.GetMeshRenderer()) {
        if (const MeshRenderer::WaterParamForGPU* special = renderer->GetWaterParamData()) {
            material.waveSpeed = special->waveSpeed;
            material.waveHeight = special->waveHeight;
            material.waveFrequency = special->waveFrequency;
            material.flowSpeedX = special->flowSpeedX;
            material.flowSpeedY = special->flowSpeedY;
            material.effectType = special->effectType;
            material.effectScale = special->effectScale;
            material.effectSoftness = special->effectSoftness;
            material.effectIntensity = special->effectIntensity;
            material.billboardScale = special->billboardScale;
            material.effectScaleX = special->effectScaleX;
            material.effectScaleY = special->effectScaleY;
            material.effectScaleZ = special->effectScaleZ;
        }
    }
    return material;
}

void MaterialInstanceAsset::Apply(const MaterialInstanceData& material, Object3d& object) {
    // Setterを通してObject3d側のマテリアルとテクスチャ状態を更新する。
    object.SetColor(material.color);
    object.SetBlendMode(static_cast<BlendMode>(std::clamp(material.blendMode, 0, 5)));
    object.SetMaterialType(material.materialType);
    object.SetMetallic(material.metallic);
    object.SetRoughness(material.roughness);
    object.SetEmissive(material.emissive);
    object.SetEnableLighting(material.enableLighting);
    object.SetEnableNormalMap(material.enableNormalMap);
    object.SetEnableEnvMap(material.enableEnvMap);
    object.SetEnvIntensity(material.envIntensity);
    object.SetTexture(material.texturePath);
    object.SetNormalMap(material.normalMapPath);
    object.SetOrmMap(material.ormMapPath);
    object.SetTextureTiling(material.textureTiling);
    object.SetAutoTextureTiling(material.autoTextureTiling);

    // 特殊パラメータはRendererのGPU用データに対応するものだけ反映する。
    if (MeshRenderer* renderer = object.GetMeshRenderer()) {
        if (MeshRenderer::WaterParamForGPU* special = renderer->GetWaterParamData()) {
            special->waveSpeed = material.waveSpeed;
            special->waveHeight = material.waveHeight;
            special->waveFrequency = material.waveFrequency;
            special->flowSpeedX = material.flowSpeedX;
            special->flowSpeedY = material.flowSpeedY;
            special->effectType = material.effectType;
            special->effectScale = material.effectScale;
            special->effectSoftness = material.effectSoftness;
            special->effectIntensity = material.effectIntensity;
            special->billboardScale = material.billboardScale;
            special->effectScaleX = material.effectScaleX;
            special->effectScaleY = material.effectScaleY;
            special->effectScaleZ = material.effectScaleZ;
        }
    }
}

bool MaterialInstanceAsset::LoadResolved(
    const std::string& assetPath,
    MaterialInstanceData& material,
    std::string* errorMessage) {
    if (assetPath.empty()) {
        SetError(errorMessage, "Material Instanceのパスが空です。");
        return false;
    }
    material = MaterialInstanceData{};
    std::unordered_set<std::string> loading;
    return LoadResolvedInternal(fs::path(assetPath), material, loading, 0, errorMessage);
}

bool MaterialInstanceAsset::Save(
    const std::string& assetPath,
    const MaterialInstanceData& material,
    std::string* errorMessage) {
    if (assetPath.empty()) {
        SetError(errorMessage, "Material Instanceの保存先が空です。");
        return false;
    }

    // 保存先の階層を作ってから、通常項目と特殊項目をJSONにまとめる。
    const fs::path path(assetPath);
    std::error_code error;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), error);
    }
    if (error) {
        SetError(errorMessage, "Material Instanceの保存フォルダーを作れません: " + error.message());
        return false;
    }

    json properties;
    properties["displayName"] = material.displayName;
    properties["color"] = { material.color.x, material.color.y, material.color.z, material.color.w };
    properties["blendMode"] = material.blendMode;
    properties["materialType"] = material.materialType;
    properties["metallic"] = material.metallic;
    properties["roughness"] = material.roughness;
    properties["emissive"] = material.emissive;
    properties["enableLighting"] = material.enableLighting;
    properties["enableNormalMap"] = material.enableNormalMap;
    properties["enableEnvMap"] = material.enableEnvMap;
    properties["envIntensity"] = material.envIntensity;
    properties["texturePath"] = material.texturePath;
    properties["normalMapPath"] = material.normalMapPath;
    properties["ormMapPath"] = material.ormMapPath;
    properties["textureTiling"] = { material.textureTiling.x, material.textureTiling.y };
    properties["autoTextureTiling"] = material.autoTextureTiling;
    properties["special"] = {
        { "waveSpeed", material.waveSpeed },
        { "waveHeight", material.waveHeight },
        { "waveFrequency", material.waveFrequency },
        { "flowSpeedX", material.flowSpeedX },
        { "flowSpeedY", material.flowSpeedY },
        { "effectType", material.effectType },
        { "effectScale", material.effectScale },
        { "effectSoftness", material.effectSoftness },
        { "effectIntensity", material.effectIntensity },
        { "billboardScale", material.billboardScale },
        { "effectScaleX", material.effectScaleX },
        { "effectScaleY", material.effectScaleY },
        { "effectScaleZ", material.effectScaleZ }
    };

    // 親アセットを指定した場合も、現在値はpropertiesとして保存する。
    json root;
    root["assetType"] = "MaterialInstance";
    root["version"] = material.version;
    if (!material.baseAsset.empty()) root["baseAsset"] = material.baseAsset;
    root["properties"] = std::move(properties);

    std::ofstream file(path);
    if (!file) {
        SetError(errorMessage, "Material Instanceを書き込めません: " + NormalizePath(path));
        return false;
    }
    file << root.dump(2) << '\n';
    return true;
}

std::vector<std::string> MaterialInstanceAsset::Discover(const std::string& rootDirectory) {
    std::vector<std::string> assets;
    std::error_code error;
    const fs::path root(rootDirectory);
    if (!fs::exists(root, error)) return assets;

    // 拡張子で候補だけを集め、内容がMaterialInstanceかどうかはここでは判定しない。
    for (fs::recursive_directory_iterator iterator(root, error), end; iterator != end && !error; iterator.increment(error)) {
        if (!iterator->is_regular_file() || iterator->path().extension() != ".json") continue;
        assets.push_back(NormalizePath(iterator->path()));
    }
    std::sort(assets.begin(), assets.end());
    return assets;
}

bool Object3d::ApplyMaterialInstance(const std::string& assetPath, std::string* errorMessage) {
    MaterialInstanceData material;
    if (!MaterialInstanceAsset::LoadResolved(assetPath, material, errorMessage)) {
        return false;
    }
    MaterialInstanceAsset::Apply(material, *this);
    materialInstancePath_ = std::filesystem::path(assetPath).lexically_normal().generic_string();
    return true;
}

bool Object3d::SaveMaterialInstance(const std::string& assetPath, std::string* errorMessage) {
    MaterialInstanceData material = MaterialInstanceAsset::Capture(*this);
    material.displayName = std::filesystem::path(assetPath).stem().string();
    if (!MaterialInstanceAsset::Save(assetPath, material, errorMessage)) {
        return false;
    }
    materialInstancePath_ = std::filesystem::path(assetPath).lexically_normal().generic_string();
    return true;
}
