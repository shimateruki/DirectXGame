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

std::string NormalizeSlashes(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::vector<fs::path> MakeLodManifestCandidates(const std::string& modelName) {
    // モデル名の拡張子有無を吸収してLOD定義の候補を列挙する。
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
    // LOD設定の構文が不正な場合は失敗を返し、呼び出し元で適用を止められるようにする。
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

// 非有限値と極端な繰り返し数を材質定数へ渡さないよう制限します。
float ClampTextureTiling(float value) {
    if (!std::isfinite(value)) {
        return 1.0f;
    }
    return std::clamp(value, 0.01f, 1000.0f);
}

engine::Vector2 MakeAutoTilingFromScale(const engine::Vector3& scale) {
    // LOD描画でも通常描画と同じ二軸を選び、距離切替で模様の密度を変えない。
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
    // Resource生成かMapに失敗した場合は参照を初期化し、LOD描画へ不正なGPU値を渡さない。
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
    // 名前付きLODとの対応がなくなるため、直接指定時は以前のLOD設定を破棄します。
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
    // 基本モデルの材質を適用してから、同名モデルに付随するLOD定義を読み込みます。
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
    // 読み込めなかったLODを候補から除き、描画時に有効なモデルだけを選べるようにします。
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
    // 現在の選択も基本モデルへ戻し、古いLOD番号がUIに残らないようにします。
    lodLevels_.clear();
    activeLodLevel_ = 0;
}

bool MeshRenderer::SetLodLevelDistance(int level, float distance) {
    // 距離を負にしないで、変更後は近い閾値から選択できる順序を保ちます。
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
    // モデルの候補パスを順に試し、見つからない場合は基本モデルのみで描画します。
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
    // カメラ位置が変わっている可能性があるため、参照時にも描画モデルを再評価します。
    ResolveDrawModel();
    return activeLodLevel_;
}

std::string MeshRenderer::GetActiveModelName() const {
    // 実際に選ばれたLODの名前を返し、選択がなければ基本モデル名に戻します。
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
    // ワールド行列の平行移動成分を使い、親Transformを含む実際の配置位置で測ります。
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera || !transform_) {
        return 0.0f;
    }

    const engine::Vector3 cameraPos = camera->GetEye();
    const engine::Vector3 objectPos = {
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
    // カメラ距離を満たした最後の閾値を選び、遠距離ほど低詳細モデルへ切り替えます。
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

