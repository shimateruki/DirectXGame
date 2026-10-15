#include "MeshRenderer.h"
#include "FoliageInteraction.h"
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
    // モデル名の形式ごとに、隣接ファイルとモデル別フォルダのLOD定義を探す。
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
    // 影の設定ファイルを検証し、失敗時は呼び出し元に読み込み失敗を伝える。
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
    // 影用の材質定数も通常描画と同じUV倍率に揃える。
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
    // Map失敗時にリソースとポインタを破棄し、影用の不完全な定数バッファを残さない。
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
// MeshRenderer 影とローカルフォグ描画
// ------------------------------------------------------------------------
// シャドウマップ用描画と、深度を使ったローカルフォグ描画を担当する。
// 通常描画や特殊マテリアルとはパスが違うため、専用ファイルとして分ける。
// ========================================================================
void MeshRenderer::DrawShadow() {
    DrawShadowOnly();
}

void MeshRenderer::SetShadowCommonState() {
    if (!common_) return;
    common_->SetShadowGraphicsCommand();
    common_->SetShadowPipelineState();
}

void MeshRenderer::DrawShadowOnly() {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !shadowWvpResource_ || !materialResource_ || !materialData_) return;
    drawModel->PrepareComputeSkinning();
    // Compute実行でルートシグネチャとPSOが変わるため、影描画の状態を復元します。
    common_->SetShadowGraphicsCommand();
    const bool foliage = materialData_->materialType == 30 || materialData_->materialType == 31;
    common_->SetShadowPipelineState(foliage);
    auto* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRoot32BitConstants(3, 32, FoliageInteraction::samples.data(), 0);
    drawModel->DrawShadow(shadowWvpResource_.Get(), meshDrawIndex_);
}

void MeshRenderer::DrawLocalFog(uint32_t depthSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !localFogResource_) return;

    common_->SetLocalFogGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();


    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, depthSrvHandle);


    commandList->SetGraphicsRootConstantBufferView(3, localFogResource_->GetGPUVirtualAddress());

    // [0] にWVP、[1] にボーンが自動セットされる
    drawModel->DrawShadow(wvpResource_.Get(), meshDrawIndex_);
}
