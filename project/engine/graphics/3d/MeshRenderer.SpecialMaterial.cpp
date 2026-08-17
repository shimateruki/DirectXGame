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
    drawModel->PrepareComputeSkinning();
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
    ID3D12Resource* previewWvpResource = nullptr;
    ID3D12Resource* previewCameraResource = nullptr;
    if (!drawModel || !common_ || !HasRequiredBuffers() ||
        !PreparePreviewCameraData(camera, previewBufferIndex, previewWvpResource, previewCameraResource)) {
        return;
    }

    drawModel->PrepareComputeSkinning();
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

bool MeshRenderer::PreparePreviewCameraData(Camera* camera, int previewBufferIndex, ID3D12Resource*& wvpResource, ID3D12Resource*& cameraResource) {
    wvpResource = nullptr;
    cameraResource = nullptr;
    if (!camera) {
        return false;
    }

    const int safePreviewIndex = std::clamp(previewBufferIndex, 0, kPreviewBufferCount - 1);
    wvpResource = previewWvpResources_[safePreviewIndex].Get();
    cameraResource = previewCameraResources_[safePreviewIndex].Get();
    TransformationMatrix* previewWvpData = previewWvpData_[safePreviewIndex];
    CameraForGPU* previewCameraData = previewCameraData_[safePreviewIndex];
    if (!wvpResource || !cameraResource || !previewWvpData || !previewCameraData) {
        return false;
    }

    Math math;
    const Matrix4x4 worldMatrix = BuildRenderWorldMatrix();
    previewWvpData->WVP = math.Multiply(worldMatrix, camera->GetViewProjectionMatrix());
    previewWvpData->world = worldMatrix;
    previewWvpData->WorldInverseTranspose = GetCachedWorldInverseTranspose(worldMatrix);
    previewCameraData->worldPosition = camera->GetEye();
    return true;
}

void MeshRenderer::DrawSpecialMaterialForCamera(int materialType, Camera* camera, uint32_t depthSrvHandle, uint32_t colorSrvHandle, int previewBufferIndex) {
    ID3D12Resource* previewWvpResource = nullptr;
    ID3D12Resource* previewCameraResource = nullptr;
    if (!common_ || !HasRequiredBuffers() ||
        !PreparePreviewCameraData(camera, previewBufferIndex, previewWvpResource, previewCameraResource)) {
        return;
    }

    switch (materialType) {
    case 8:
        DrawWaterWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle);
        break;
    case 9:
        DrawMagmaWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle);
        break;
    case 10:
        DrawIceWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle);
        break;
    case 11:
        DrawFireWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle);
        break;
    case 12:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLaserGraphicsCommand);
        break;
    case 13:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetSlimeGelGraphicsCommand);
        break;
    case 14:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetShockwaveGraphicsCommand);
        break;
    case 15:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLiquidContactGraphicsCommand);
        break;
    case 16:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetDamageCrackGraphicsCommand);
        break;
    case 17:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetUpdraftGraphicsCommand, true);
        break;
    case 18:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetStunBindGraphicsCommand, true);
        break;
    case 19:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCrownUnlockGraphicsCommand, true);
        break;
    case 20:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetPoisonSporeGraphicsCommand, true);
        break;
    case 21:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCloudGraphicsCommand, true);
        break;
    case 22:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetGatePortalGraphicsCommand, true, 1);
        break;
    case 26:
        DrawSpecialMaterialWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, &Object3dCommon::SetWindOrbGraphicsCommand);
        break;
    default:
        break;
    }
}

void MeshRenderer::DrawWater(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawWaterWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawWaterWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !wvpResource) return;

    const bool useWaterGrid = !waterParamData_ || waterParamData_->effectType < 0.5f;
    if (useWaterGrid && !waterProxyModel_) {
        InitializeWaterProxyModel();
    }

    common_->SetWaterGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);

    //  4番目にカラーテクスチャをセット
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetWaterBakedShaderTextures());

    drawModel = (useWaterGrid && waterProxyModel_) ? waterProxyModel_.get() : drawModel;
    drawModel->DrawMeshOnly((useWaterGrid && waterProxyModel_) ? -1 : meshDrawIndex_);
}
void MeshRenderer::DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawMagmaWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawMagmaWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !wvpResource) return;

    common_->SetMagmaGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetDefaultBakedShaderTextures());

    drawModel->DrawMeshOnly(meshDrawIndex_);
}

void MeshRenderer::DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawIceWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawIceWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !wvpResource) return;

    common_->SetIceGraphicsCommand(); 
    // (以下、DrawMagmaと全く同じ)
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetDefaultBakedShaderTextures());
    drawModel->DrawMeshOnly(meshDrawIndex_);
}
void MeshRenderer::DrawFire(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawFireWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawFireWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !wvpResource) return;

    // かがり火だけは交差する複数面で厚みを作り、それ以外の炎は従来のビルボードを維持します。
    const bool useVolumetricProxy = waterParamData_ &&
        waterParamData_->effectType >= 3.5f && waterParamData_->effectType < 4.5f;
    if (useVolumetricProxy) {
        if (!volumetricFireProxyModel_) {
            InitializeVolumetricFireProxyModel();
        }
    } else if (!fireProxyModel_) {
        InitializeFireProxyModel();
    }

    common_->SetFireGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetFireBakedShaderTextures());
    Model* proxyModel = useVolumetricProxy ? volumetricFireProxyModel_.get() : fireProxyModel_.get();
    drawModel = proxyModel ? proxyModel : drawModel;
    drawModel->DrawMeshOnly(proxyModel ? -1 : meshDrawIndex_);
}

void MeshRenderer::DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel, int bakedTextureMode) {
    DrawSpecialMaterialWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle, setGraphicsCommand, useProxyModel, bakedTextureMode);
}

void MeshRenderer::DrawSpecialMaterialWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel, int bakedTextureMode) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !setGraphicsCommand || !wvpResource) return;

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
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());
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

void MeshRenderer::DrawWindOrb(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetWindOrbGraphicsCommand);
}

void MeshRenderer::InitializeWaterProxyModel() {
    ModelCommon* modelCommon = ModelManager::GetInstance()->GetModelCommon();
    if (!modelCommon) {
        return;
    }

    waterProxyModel_ = std::make_unique<Model>();

    constexpr int kSegments = 64;
    constexpr int kRow = kSegments + 1;
    std::vector<Model::VertexData> vertices;
    vertices.reserve(kRow * kRow);

    for (int iz = 0; iz <= kSegments; ++iz) {
        const float v = static_cast<float>(iz) / static_cast<float>(kSegments);
        const float z = -1.0f + v * 2.0f;
        for (int ix = 0; ix <= kSegments; ++ix) {
            const float u = static_cast<float>(ix) / static_cast<float>(kSegments);
            const float x = -1.0f + u * 2.0f;

            Model::VertexData vertex{};
            vertex.position = { x, 0.0f, z, 1.0f };
            vertex.texcoord = { u, v };
            vertex.normal = { 0.0f, 1.0f, 0.0f };
            vertex.tangent = { 1.0f, 0.0f, 0.0f };
            vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
            vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
            vertices.push_back(vertex);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(kSegments * kSegments * 6);
    for (int iz = 0; iz < kSegments; ++iz) {
        for (int ix = 0; ix < kSegments; ++ix) {
            const uint32_t i0 = static_cast<uint32_t>(iz * kRow + ix);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(kRow);
            const uint32_t i3 = i2 + 1;
            indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }

    waterProxyModel_->CreateFromVertices(modelCommon, vertices, indices);
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

void MeshRenderer::InitializeVolumetricFireProxyModel() {
    ModelCommon* modelCommon = ModelManager::GetInstance()->GetModelCommon();
    if (!modelCommon) {
        return;
    }

    volumetricFireProxyModel_ = std::make_unique<Model>();

    constexpr int kCardCount = 3;
    constexpr float kPi = 3.14159265358979323846f;
    std::vector<Model::VertexData> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(kCardCount * 4);
    indices.reserve(kCardCount * 6);

    // 三方向の炎面を交差させます。UVの2単位ごとの帯はシェーダーで面番号を復元するために使います。
    for (int cardIndex = 0; cardIndex < kCardCount; ++cardIndex) {
        const float angle = static_cast<float>(cardIndex) * (kPi / 3.0f);
        const float directionX = std::cos(angle);
        const float directionZ = std::sin(angle);
        const Vector3 normal = { -directionZ, 0.0f, directionX };
        const Vector3 tangent = { directionX, 0.0f, directionZ };
        const float encodedU = static_cast<float>(cardIndex * 2);
        const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());

        auto addVertex = [&](float horizontal, float y, float u, float v) {
            Model::VertexData vertex{};
            vertex.position = { directionX * horizontal, y, directionZ * horizontal, 1.0f };
            vertex.texcoord = { encodedU + u, v };
            vertex.normal = normal;
            vertex.tangent = tangent;
            vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
            vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
            vertices.push_back(vertex);
        };

        addVertex(-1.0f, -1.0f, 0.0f, 1.0f);
        addVertex(-1.0f,  1.0f, 0.0f, 0.0f);
        addVertex( 1.0f,  1.0f, 1.0f, 0.0f);
        addVertex( 1.0f, -1.0f, 1.0f, 1.0f);
        indices.insert(indices.end(), {
            firstVertex, firstVertex + 1, firstVertex + 2,
            firstVertex, firstVertex + 2, firstVertex + 3
        });
    }

    volumetricFireProxyModel_->CreateFromVertices(modelCommon, vertices, indices);
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

