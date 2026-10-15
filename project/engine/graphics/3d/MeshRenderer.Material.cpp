#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include "TextureManager.h"
#include "engine/graphics/core/ColorSpace.h"
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
    // モデルの配置形式に応じて、LOD定義を探索する二つのパスを用意する。
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
    // マテリアル設定の読み込み可否を返し、不正なJSONを適用しない。
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
    // 材質設定のUV倍率はスケールの絶対値から大きい二軸を採用する。
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
    // 定数バッファの確保とMapを一組として扱い、失敗時は参照を残さない。
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
// MeshRenderer マテリアル設定
// ------------------------------------------------------------------------
// 色、PBR値、通常/ORM/アルベドテクスチャ、UVタイリング、環境反射を担当する。
// Inspectorから編集される見た目の値は、基本的にこのファイルへ集める。
// ========================================================================
void MeshRenderer::SetColor(const engine::Vector4& color) {
    materialColor_ = color;
    if (materialData_) materialData_->color = engine::ColorSpace::AuthoringToWorking(materialColor_);
}

void MeshRenderer::SetMaterialType(int32_t type) {
    if (materialData_) materialData_->materialType = type;
}

void MeshRenderer::SetIntensity(float intensity) {
    if (directionalLightData_) directionalLightData_->intensity = intensity;
}

engine::Vector4 MeshRenderer::GetColor() const {
    return materialColor_;
}

int32_t MeshRenderer::GetMaterialType() const {
    return materialData_ ? materialData_->materialType : 0;
}

void MeshRenderer::SetMetallic(float metallic) {
    if (materialData_) materialData_->metallic = metallic;
}

void MeshRenderer::SetRoughness(float roughness) {
    if (materialData_) materialData_->roughness = roughness;
}

float MeshRenderer::GetMetallic() const {
    return materialData_ ? materialData_->metallic : 0.0f;
}

float MeshRenderer::GetRoughness() const {
    return materialData_ ? materialData_->roughness : 0.3f;
}

void MeshRenderer::SetEnableNormalMap(bool enable) {
    if (materialData_) materialData_->enableNormalMap = enable ? 1 : 0;
}

bool MeshRenderer::GetEnableNormalMap() const {
    return materialData_ ? (materialData_->enableNormalMap == 1) : false;
}

void MeshRenderer::SetNormalMap(const std::string& texturePath) {
    // 法線テクスチャは色変換せず線形空間で読み、解除時は旧ハンドルを使わない。
    normalMapPath_ = texturePath;
    if (!texturePath.empty()) {
        normalMapHandle_ = TextureManager::GetInstance()->Load(texturePath, TextureManager::TextureColorSpace::Linear);
    } else {
        normalMapHandle_ = 0;
    }
}

void MeshRenderer::SetOrmMap(const std::string& texturePath) {
    // ORM の各成分は色ではなく係数なので線形空間で読み込む。
    ormMapPath_ = texturePath;
    if (!texturePath.empty()) {
        ormMapHandle_ = TextureManager::GetInstance()->Load(texturePath, TextureManager::TextureColorSpace::Linear);
    } else {
        ormMapHandle_ = 0;
    }
}


void MeshRenderer::SetTexture(const std::string& texturePath) {
    // ベースカラーだけは sRGB として読み込み、空パスで参照を解除する。
    texturePath_ = texturePath;
    if (!texturePath.empty()) {
        textureHandle_ = TextureManager::GetInstance()->Load(texturePath, TextureManager::TextureColorSpace::SRGB);
    } else {
        textureHandle_ = 0;
    }
}

void MeshRenderer::SetTextureTiling(const engine::Vector2& tiling) {
    // 無効な繰り返し率を補正してから UV 行列へ反映する。
    textureTiling_ = {
        ClampTextureTiling(tiling.x),
        ClampTextureTiling(tiling.y)
    };
    UpdateUvTransform();
}

void MeshRenderer::SetAutoTextureTiling(bool enabled) {
    autoTextureTiling_ = enabled;
    UpdateUvTransform();
}

void MeshRenderer::UpdateUvTransform() {
    // 手動タイリングへ必要に応じてオブジェクトの大きさを掛け、見た目の密度を保つ。
    if (!materialData_) {
        return;
    }

    engine::Vector2 effectiveTiling = {
        ClampTextureTiling(textureTiling_.x),
        ClampTextureTiling(textureTiling_.y)
    };

    if (autoTextureTiling_ && transform_) {
        engine::Vector2 scaleTiling = MakeAutoTilingFromScale(transform_->scale);
        effectiveTiling.x *= scaleTiling.x;
        effectiveTiling.y *= scaleTiling.y;
    }

    materialData_->uvTransform = engine::Math::MakeScaleMatrix({ effectiveTiling.x, effectiveTiling.y, 1.0f });
}

void MeshRenderer::SetEnableEnvMap(bool enable) {
    if (materialData_) materialData_->enableEnvMap = enable ? 1 : 0;
}

bool MeshRenderer::GetEnableEnvMap() const {
    return materialData_ ? (materialData_->enableEnvMap == 1) : false;
}

void MeshRenderer::SetEnvIntensity(float intensity) {
    if (materialData_) materialData_->envIntensity = intensity;
}

float MeshRenderer::GetEnvIntensity() const {
    return materialData_ ? materialData_->envIntensity : 1.0f;
}

void MeshRenderer::SetEmissive(float emissive) {
    if (materialData_) materialData_->emissive = emissive;
}

float MeshRenderer::GetEmissive() const {
    return materialData_ ? materialData_->emissive : 1.0f;
}
