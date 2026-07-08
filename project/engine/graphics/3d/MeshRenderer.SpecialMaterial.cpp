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
// MeshRenderer 特殊マテリアル描画
// ------------------------------------------------------------------------
// 水、マグマ、炎、レーザー、ポータルなど専用パイプラインの描画を担当する。
// 焼き込みテクスチャと代理モデルもここに集約し、特殊表現の追加場所を明確にする。
// ========================================================================
void MeshRenderer::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers()) return;
    common_->SetGraphicsCommand();
    common_->SetPipelineState(blendMode_);
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // [11] 影用のWVP行列 (b1) をセット
    if (shadowWvpResource_) {
        commandList->SetGraphicsRootConstantBufferView(11, shadowWvpResource_->GetGPUVirtualAddress());
    }

    // [12] シャドウマップのテクスチャ (t5) をセット
    uint32_t shadowMapSrvHandle = common_->GetDxCommon()->GetShadowMapSrvHandle();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 12, shadowMapSrvHandle);
    // ModelのDrawを呼ぶ
    drawModel->Draw(
        wvpResource_.Get(),
        LightManager::GetInstance()->GetDirectionalLightResource(),
        cameraResource_.Get(),
        pointLightResource,
        spotLightResource,
        materialResource_.Get(), normalMapHandle_, ormMapHandle_, textureHandle_,
        1, 0, meshDrawIndex_
    );
}

void MeshRenderer::DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex) {
    Model* drawModel = ResolveDrawModel();
    const int safePreviewIndex = std::clamp(previewBufferIndex, 0, kPreviewBufferCount - 1);
    ID3D12Resource* previewWvpResource = previewWvpResources_[safePreviewIndex].Get();
    TransformationMatrix* previewWvpData = previewWvpData_[safePreviewIndex];
    ID3D12Resource* previewCameraResource = previewCameraResources_[safePreviewIndex].Get();
    CameraForGPU* previewCameraData = previewCameraData_[safePreviewIndex];

    if (!camera || !drawModel || !common_ || !HasRequiredBuffers() ||
        !previewWvpResource || !previewWvpData || !previewCameraResource || !previewCameraData) {
        return;
    }

    Math math;
    const Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    const Matrix4x4& worldMatrix = transform_->matWorld;
    previewWvpData->WVP = math.Multiply(worldMatrix, viewProj);
    previewWvpData->world = worldMatrix;
    previewWvpData->WorldInverseTranspose = math.Transpose(math.Inverse(worldMatrix));
    previewCameraData->worldPosition = camera->GetEye();

    common_->SetGraphicsCommand();
    common_->SetPipelineState(blendMode_);
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    if (shadowWvpResource_) {
        commandList->SetGraphicsRootConstantBufferView(11, shadowWvpResource_->GetGPUVirtualAddress());
    }

    const uint32_t shadowMapSrvHandle = common_->GetDxCommon()->GetShadowMapSrvHandle();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 12, shadowMapSrvHandle);

    drawModel->Draw(
        previewWvpResource,
        LightManager::GetInstance()->GetDirectionalLightResource(),
        previewCameraResource,
        pointLightResource,
        spotLightResource,
        materialResource_.Get(), normalMapHandle_, ormMapHandle_, textureHandle_,
        1, 0, meshDrawIndex_
    );
}

void MeshRenderer::DrawWater(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_) return;

    common_->SetWaterGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);

    //  4番目にカラーテクスチャをセット
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetWaterBakedShaderTextures());

    drawModel->DrawMeshOnly(meshDrawIndex_);
}
void MeshRenderer::DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_) return;

    common_->SetMagmaGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetDefaultBakedShaderTextures());

    drawModel->DrawMeshOnly(meshDrawIndex_);
}

void MeshRenderer::DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_) return;

    common_->SetIceGraphicsCommand(); 
    // (以下、DrawMagmaと全く同じ)
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetDefaultBakedShaderTextures());
    drawModel->DrawMeshOnly(meshDrawIndex_);
}
void MeshRenderer::DrawFire(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_) return;

    if (!fireProxyModel_) {
        InitializeFireProxyModel();
    }

    common_->SetFireGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetFireBakedShaderTextures());
    drawModel = fireProxyModel_ ? fireProxyModel_.get() : drawModel;
    drawModel->DrawMeshOnly(fireProxyModel_ ? -1 : meshDrawIndex_);
}

void MeshRenderer::DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel, int bakedTextureMode) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !setGraphicsCommand) return;

    Model* proxyModel = nullptr;
    if (useProxyModel) {
        if (bakedTextureMode == 1) {
            if (!gatePortalProxyModel_) {
                InitializeGatePortalProxyModel();
            }
            proxyModel = gatePortalProxyModel_.get();
        } else {
            if (!fireProxyModel_) {
                InitializeFireProxyModel();
            }
            proxyModel = fireProxyModel_.get();
        }
    }

    (common_->*setGraphicsCommand)();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    const BakedShaderTextureSet& bakedTextures =
        (bakedTextureMode == 1) ? GetGatePortalBakedShaderTextures() : GetDefaultBakedShaderTextures();
    BindBakedShaderTextures(commandList, bakedTextures);
    drawModel = (useProxyModel && proxyModel) ? proxyModel : drawModel;
    drawModel->DrawMeshOnly((useProxyModel && proxyModel) ? -1 : meshDrawIndex_);
}

void MeshRenderer::DrawLaser(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLaserGraphicsCommand);
}

void MeshRenderer::DrawSlimeGel(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetSlimeGelGraphicsCommand);
}

void MeshRenderer::DrawShockwave(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetShockwaveGraphicsCommand);
}

void MeshRenderer::DrawLiquidContact(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLiquidContactGraphicsCommand);
}

void MeshRenderer::DrawDamageCrack(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetDamageCrackGraphicsCommand);
}

void MeshRenderer::DrawUpdraft(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetUpdraftGraphicsCommand, true);
}

void MeshRenderer::DrawStunBind(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetStunBindGraphicsCommand, true);
}

void MeshRenderer::DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCrownUnlockGraphicsCommand, true);
}

void MeshRenderer::DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetPoisonSporeGraphicsCommand, true);
}

void MeshRenderer::DrawCloud(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCloudGraphicsCommand, true);
}

void MeshRenderer::DrawGatePortal(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetGatePortalGraphicsCommand, true, 1);
}

void MeshRenderer::InitializeFireProxyModel() {
    ModelCommon* modelCommon = ModelManager::GetInstance()->GetModelCommon();
    if (!modelCommon) {
        return;
    }

    fireProxyModel_ = std::make_unique<Model>();

    std::vector<Model::VertexData> vertices(4);
    auto setVertex = [](Model::VertexData& vertex, float x, float y, float u, float v) {
        vertex.position = { x, y, 0.0f, 1.0f };
        vertex.texcoord = { u, v };
        vertex.normal = { 0.0f, 0.0f, 1.0f };
        vertex.tangent = { 1.0f, 0.0f, 0.0f };
        vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
        vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    setVertex(vertices[0], -1.0f, -1.0f, 0.0f, 1.0f);
    setVertex(vertices[1], -1.0f, 1.0f, 0.0f, 0.0f);
    setVertex(vertices[2], 1.0f, 1.0f, 1.0f, 0.0f);
    setVertex(vertices[3], 1.0f, -1.0f, 1.0f, 1.0f);

    const std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };
    fireProxyModel_->CreateFromVertices(modelCommon, vertices, indices);
}

void MeshRenderer::InitializeGatePortalProxyModel() {
    ModelCommon* modelCommon = ModelManager::GetInstance()->GetModelCommon();
    if (!modelCommon) {
        return;
    }

    gatePortalProxyModel_ = std::make_unique<Model>();

    constexpr int kSegments = 24;
    constexpr float kHalfDepth = 1.0f;
    const int row = kSegments + 1;

    std::vector<Model::VertexData> vertices;
    vertices.reserve(row * row * 2);

    auto addVertex = [&vertices](float x, float y, float z, float u, float v) {
        Model::VertexData vertex{};
        vertex.position = { x, y, z, 1.0f };
        vertex.texcoord = { u, v };
        vertex.normal = { 0.0f, 0.0f, z >= 0.0f ? 1.0f : -1.0f };
        vertex.tangent = { 1.0f, 0.0f, 0.0f };
        vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
        vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
        vertices.push_back(vertex);
    };

    for (int layer = 0; layer < 2; ++layer) {
        const float z = (layer == 0) ? -kHalfDepth : kHalfDepth;
        for (int iy = 0; iy <= kSegments; ++iy) {
            const float v = static_cast<float>(iy) / static_cast<float>(kSegments);
            const float y = -1.0f + v * 2.0f;
            for (int ix = 0; ix <= kSegments; ++ix) {
                const float u = static_cast<float>(ix) / static_cast<float>(kSegments);
                const float x = -1.0f + u * 2.0f;
                addVertex(x, y, z, u, v);
            }
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(kSegments * kSegments * 6 * 2);
    for (int layer = 0; layer < 2; ++layer) {
        const uint32_t base = static_cast<uint32_t>(layer * row * row);
        for (int iy = 0; iy < kSegments; ++iy) {
            for (int ix = 0; ix < kSegments; ++ix) {
                const uint32_t i0 = base + static_cast<uint32_t>(iy * row + ix);
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + static_cast<uint32_t>(row);
                const uint32_t i3 = i2 + 1;
                if (layer == 0) {
                    indices.insert(indices.end(), { i0, i3, i1, i0, i2, i3 });
                } else {
                    indices.insert(indices.end(), { i0, i1, i3, i0, i3, i2 });
                }
            }
        }
    }

    gatePortalProxyModel_->CreateFromVertices(modelCommon, vertices, indices);
}

