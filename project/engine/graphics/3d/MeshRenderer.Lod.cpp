#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include "TextureManager.h"
#include "DebugConsole.h"
#include <cassert>
#include <SrvManager.h>
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;

struct BakedShaderTextureSet {
    uint32_t slot0 = 0;
    uint32_t slot1 = 0;
    uint32_t slot2 = 0;
};

struct BakedShaderTextureCache {
    BakedShaderTextureSet fallback;
    BakedShaderTextureSet water;
    BakedShaderTextureSet fire;
    BakedShaderTextureSet gatePortal;
    bool loaded = false;
};

std::string NormalizeSlashes(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

BakedShaderTextureCache& GetBakedShaderTextureCache() {
    static BakedShaderTextureCache cache;
    return cache;
}

uint32_t LoadTextureWithFallback(const char* texturePath) {
    const char* fallbackPath = "Resources/sprite/common/white.dds";
    const char* pathToLoad = fs::exists(texturePath) ? texturePath : fallbackPath;
    return TextureManager::GetInstance()->Load(pathToLoad, true);
}

void EnsureBakedShaderTexturesLoaded() {
    BakedShaderTextureCache& cache = GetBakedShaderTextureCache();
    if (cache.loaded) {
        return;
    }

    const uint32_t fallback = LoadTextureWithFallback("Resources/sprite/common/white.dds");
    cache.fallback = { fallback, fallback, fallback };
    cache.water = {
        LoadTextureWithFallback("Resources/texture/BakedShader/water_foam_mask.dds"),
        LoadTextureWithFallback("Resources/texture/BakedShader/water_flow_noise.dds"),
        fallback
    };
    cache.fire = {
        LoadTextureWithFallback("Resources/texture/BakedShader/fire_flame_mask.dds"),
        LoadTextureWithFallback("Resources/texture/BakedShader/fire_orb_mask.dds"),
        fallback
    };
    cache.gatePortal = {
        LoadTextureWithFallback("Resources/texture/BakedShader/gate_swirl_mask.dds"),
        fallback,
        fallback
    };
    cache.loaded = true;
}

const BakedShaderTextureSet& GetDefaultBakedShaderTextures() {
    return GetBakedShaderTextureCache().fallback;
}

const BakedShaderTextureSet& GetWaterBakedShaderTextures() {
    return GetBakedShaderTextureCache().water;
}

const BakedShaderTextureSet& GetFireBakedShaderTextures() {
    return GetBakedShaderTextureCache().fire;
}

const BakedShaderTextureSet& GetGatePortalBakedShaderTextures() {
    return GetBakedShaderTextureCache().gatePortal;
}

void BindBakedShaderTextures(ID3D12GraphicsCommandList* commandList, const BakedShaderTextureSet& textures) {
    EnsureBakedShaderTexturesLoaded();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 5, textures.slot0);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 6, textures.slot1);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 7, textures.slot2);
}

std::vector<fs::path> MakeLodManifestCandidates(const std::string& modelName) {
    std::vector<fs::path> candidates;
    if (modelName.empty()) {
        return candidates;
    }

    const fs::path root = "Resources/3DModel";
    const fs::path modelPath = fs::path(modelName);
    const fs::path parent = modelPath.parent_path();
    const std::string stem = modelPath.stem().string();
    const std::string lodFileName = stem + "_lod.json";

    if (!modelPath.extension().empty()) {
        candidates.push_back(root / parent / lodFileName);
        candidates.push_back(root / parent / stem / lodFileName);
    } else {
        candidates.push_back(root / parent / stem / lodFileName);
        candidates.push_back(root / parent / lodFileName);
    }

    return candidates;
}

bool ReadJsonFile(const fs::path& path, nlohmann::json& outJson) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    try {
        file >> outJson;
        return true;
    }
    catch (...) {
        return false;
    }
}

float ClampTextureTiling(float value) {
    if (!std::isfinite(value)) {
        return 1.0f;
    }
    return std::clamp(value, 0.01f, 1000.0f);
}

Vector2 MakeAutoTilingFromScale(const Vector3& scale) {
    float a = std::abs(scale.x);
    float b = std::abs(scale.y);
    float c = std::abs(scale.z);

    if (a < b) std::swap(a, b);
    if (a < c) std::swap(a, c);
    if (b < c) std::swap(b, c);

    return { (std::max)(a, 0.01f), (std::max)(b, 0.01f) };
}

template <typename T>
bool CreateMappedBuffer(DirectXCommon* dxCommon, size_t size, Microsoft::WRL::ComPtr<ID3D12Resource>& resource, T*& data, const char* label) {
    data = nullptr;
    resource.Reset();
    if (!dxCommon) {
        return false;
    }

    resource = dxCommon->CreateBufferResource(size);
    if (!resource) {
        DebugConsole::GetInstance()->AddLog(std::string("MeshRenderer buffer creation failed: ") + label);
        return false;
    }

    HRESULT hr = resource->Map(0, nullptr, reinterpret_cast<void**>(&data));
    if (FAILED(hr) || !data) {
        DebugConsole::GetInstance()->AddLog(std::string("MeshRenderer buffer map failed: ") + label);
        resource.Reset();
        data = nullptr;
        return false;
    }

    return true;
}
}

// ========================================================================
// MeshRenderer モデルとLOD
// ------------------------------------------------------------------------
// モデル設定、LODマニフェスト読み込み、距離による描画モデル解決を担当する。
// LOD関連の不具合調査時は、このファイルから追えるようにする。
// ========================================================================
void MeshRenderer::SetModel(Model* model) {
    model_ = model;
    modelName_.clear();
    ClearLodLevels();
    if (model_ && materialData_) {
        if (const Model::MaterialData* material = model_->GetPrimaryMaterialData()) {
            SetColor(material->baseColorFactor);
            materialData_->roughness = material->roughness;
            materialData_->metallic = material->metallic;
            materialData_->enableNormalMap = material->hasNormalMap ? 1 : 0;
        }
    }
}

void MeshRenderer::SetModel(const std::string& modelName) {
    modelName_ = modelName;
    model_ = ModelManager::GetInstance()->LoadModel(modelName);
    if (model_ && materialData_) {
        if (const Model::MaterialData* material = model_->GetPrimaryMaterialData()) {
            SetColor(material->baseColorFactor);
            materialData_->roughness = material->roughness;
            materialData_->metallic = material->metallic;
            materialData_->enableNormalMap = material->hasNormalMap ? 1 : 0;
        }
    }
    LoadLodManifestForModel(modelName);
}

void MeshRenderer::SetLodLevels(const std::vector<LodLevel>& levels) {
    lodLevels_.clear();

    for (LodLevel level : levels) {
        if (level.level <= 0 || level.modelName.empty()) {
            continue;
        }
        if (!level.model) {
            level.model = ModelManager::GetInstance()->LoadModel(level.modelName);
        }
        if (level.model) {
            lodLevels_.push_back(level);
        }
    }

    std::sort(lodLevels_.begin(), lodLevels_.end(), [](const LodLevel& lhs, const LodLevel& rhs) {
        return lhs.distance < rhs.distance;
    });
    activeLodLevel_ = 0;
}

void MeshRenderer::ClearLodLevels() {
    lodLevels_.clear();
    activeLodLevel_ = 0;
}

bool MeshRenderer::SetLodLevelDistance(int level, float distance) {
    bool changed = false;
    for (auto& lod : lodLevels_) {
        if (lod.level == level) {
            lod.distance = (std::max)(0.0f, distance);
            changed = true;
            break;
        }
    }

    if (changed) {
        std::sort(lodLevels_.begin(), lodLevels_.end(), [](const LodLevel& lhs, const LodLevel& rhs) {
            return lhs.distance < rhs.distance;
        });
    }
    return changed;
}

bool MeshRenderer::LoadLodManifestForModel(const std::string& modelName) {
    ClearLodLevels();

    nlohmann::json manifest;
    bool loaded = false;
    for (const fs::path& candidate : MakeLodManifestCandidates(modelName)) {
        if (ReadJsonFile(candidate, manifest)) {
            loaded = true;
            break;
        }
    }
    if (!loaded || !manifest.contains("lods") || !manifest["lods"].is_array()) {
        return false;
    }

    std::vector<LodLevel> levels;
    for (const auto& lodJson : manifest["lods"]) {
        if (!lodJson.is_object()) continue;
        const int level = lodJson.value("level", 0);
        if (level <= 0) continue;

        const std::string lodModelName = lodJson.value("modelName", "");
        if (lodModelName.empty()) continue;

        LodLevel levelData;
        levelData.level = level;
        levelData.modelName = NormalizeSlashes(lodModelName);
        levelData.distance = lodJson.value("distance", 0.0f);
        levelData.model = ModelManager::GetInstance()->LoadModel(levelData.modelName);
        if (levelData.model) {
            levels.push_back(levelData);
        }
    }

    SetLodLevels(levels);
    return !lodLevels_.empty();
}

int MeshRenderer::GetActiveLodLevel() const {
    ResolveDrawModel();
    return activeLodLevel_;
}

std::string MeshRenderer::GetActiveModelName() const {
    ResolveDrawModel();
    if (activeLodLevel_ == 0) {
        return modelName_;
    }

    for (const auto& lod : lodLevels_) {
        if (lod.level == activeLodLevel_) {
            return lod.modelName;
        }
    }
    return modelName_;
}

float MeshRenderer::GetCameraDistanceToObject() const {
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera || !transform_) {
        return 0.0f;
    }

    const Vector3 cameraPos = camera->GetEye();
    const Vector3 objectPos = {
        transform_->matWorld.m[3][0],
        transform_->matWorld.m[3][1],
        transform_->matWorld.m[3][2]
    };
    const float dx = cameraPos.x - objectPos.x;
    const float dy = cameraPos.y - objectPos.y;
    const float dz = cameraPos.z - objectPos.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Model* MeshRenderer::ResolveDrawModel() const {
    activeLodLevel_ = 0;
    if (!model_ || !lodEnabled_ || lodLevels_.empty()) {
        return model_;
    }

    const float distance = GetCameraDistanceToObject();
    Model* drawModel = model_;
    for (const auto& lod : lodLevels_) {
        if (distance >= lod.distance && lod.model) {
            drawModel = lod.model;
            activeLodLevel_ = lod.level;
        }
    }

    return drawModel;
}

