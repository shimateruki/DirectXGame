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

// 特殊材質の三つのテクスチャスロットに渡す SRV 番号。画像自体の所有者は TextureManager。
struct BakedShaderTextureSet {
    uint32_t slot0 = 0;
    uint32_t slot1 = 0;
    uint32_t slot2 = 0;
};

// 水・炎・ポータル用の参照セットと代替画像を保持し、同じキャッシュでの再読込を避ける。
struct BakedShaderTextureCache {
    BakedShaderTextureSet fallback;
    BakedShaderTextureSet water;
    BakedShaderTextureSet fire;
    BakedShaderTextureSet gatePortal;
    bool loaded = false;
};

std::string NormalizeSlashes(std::string text) {
    // リソースパスを Windows と JSON の両表記で一致させる。
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

BakedShaderTextureCache& GetBakedShaderTextureCache() {
    // テクスチャ管理側の SRV 番号を一度読み、全材質で再利用する。
    static BakedShaderTextureCache cache;
    return cache;
}

// 焼き込み画像が欠けている場合は白画像へ退避してSRVを必ず取得します。
uint32_t LoadTextureWithFallback(const char* texturePath, bool isLinearData = true) {
    // 焼き込み画像が欠けていても白画像へ退避し、SRV の未束縛を避ける。
    const char* fallbackPath = "Resources/sprite/common/white.dds";
    const char* pathToLoad = fs::exists(texturePath) ? texturePath : fallbackPath;
    return TextureManager::GetInstance()->Load(pathToLoad,
        isLinearData ? TextureManager::TextureColorSpace::Linear : TextureManager::TextureColorSpace::SRGB);
}

// 特殊材質が共有するマスク・色画像を色空間別に一度だけ読み込みます。
void EnsureBakedShaderTexturesLoaded() {
    // マスクは線形値、色画像は sRGB として読み込み、シェーダーへの解釈を揃える。
    BakedShaderTextureCache& cache = GetBakedShaderTextureCache();
    if (cache.loaded) {
        return;
    }

    const uint32_t fallback = LoadTextureWithFallback("Resources/sprite/common/white.dds");
    cache.fallback = { fallback, fallback, fallback };
    cache.water = {
        LoadTextureWithFallback("Resources/texture/BakedShader/water_foam_mask.dds"),
        LoadTextureWithFallback("Resources/texture/BakedShader/water_flow_noise.dds"),
        LoadTextureWithFallback("Resources/texture/BakedShader/water_surface_stylized.dds", false)
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

// 特殊材質用の三つのテクスチャSRVを共通ルートスロットへ束縛します。
void BindBakedShaderTextures(ID3D12GraphicsCommandList* commandList, const BakedShaderTextureSet& textures) {
    // 水系の共有ルートにある t2～t4 相当の三スロットを常に埋める。
    EnsureBakedShaderTexturesLoaded();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 5, textures.slot0);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 6, textures.slot1);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 7, textures.slot2);
}

std::vector<fs::path> MakeLodManifestCandidates(const std::string& modelName) {
    // モデルがファイル指定かフォルダ指定かによって、LOD 定義の候補を順に作る。
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
    // 読み込み失敗は例外を外へ出さず、呼び出し側の代替設定へ戻す。
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
    // 非有限値や極端な繰り返し値を材質定数に渡さない。
    if (!std::isfinite(value)) {
        return 1.0f;
    }
    return std::clamp(value, 0.01f, 1000.0f);
}

engine::Vector2 MakeAutoTilingFromScale(const engine::Vector3& scale) {
    // 物体の大きい二軸を使い、向きに依らず面積に合った繰り返し量を作る。
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
    // 作成と Map のどちらかに失敗したら部分的な資源を残さない。
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
    common_->SetMaterialPipelineState(blendMode_, GetMaterialType());
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
        isUIPreview_ && previewDirectionalLightResource_
            ? previewDirectionalLightResource_ : LightManager::GetInstance()->GetDirectionalLightResource(),
        cameraResource_.Get(),
        pointLightResource,
        spotLightResource,
        materialResource_.Get(), normalMapHandle_, ormMapHandle_, textureHandle_,
        1, 0, meshDrawIndex_
    );
}

void MeshRenderer::DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex) {
    // 別カメラ用の定数バッファを使い、通常画面の WVP と視点情報を上書きしない。
    Model* drawModel = ResolveDrawModel();
    ID3D12Resource* previewWvpResource = nullptr;
    ID3D12Resource* previewCameraResource = nullptr;
    if (!drawModel || !common_ || !HasRequiredBuffers() ||
        !PreparePreviewCameraData(camera, previewBufferIndex, previewWvpResource, previewCameraResource)) {
        return;
    }

    drawModel->PrepareComputeSkinning();
    common_->SetGraphicsCommand();
    common_->SetMaterialPipelineState(blendMode_, GetMaterialType());
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    if (shadowWvpResource_) {
        commandList->SetGraphicsRootConstantBufferView(11, shadowWvpResource_->GetGPUVirtualAddress());
    }

    const uint32_t shadowMapSrvHandle = common_->GetDxCommon()->GetShadowMapSrvHandle();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 12, shadowMapSrvHandle);

    drawModel->Draw(
        previewWvpResource,
        isUIPreview_ && previewDirectionalLightResource_
            ? previewDirectionalLightResource_ : LightManager::GetInstance()->GetDirectionalLightResource(),
        previewCameraResource,
        pointLightResource,
        spotLightResource,
        materialResource_.Get(), normalMapHandle_, ormMapHandle_, textureHandle_,
        1, 0, meshDrawIndex_
    );
}

bool MeshRenderer::PreparePreviewCameraData(Camera* camera, int previewBufferIndex, ID3D12Resource*& wvpResource, ID3D12Resource*& cameraResource) {
    // プレビュー番号に対応する独立バッファへ、そのカメラの行列と視点を記録する。
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

    engine::Math math;
    const engine::Matrix4x4 worldMatrix = BuildRenderWorldMatrix();
    previewWvpData->WVP = math.Multiply(worldMatrix, camera->GetViewProjectionMatrix());
    previewWvpData->world = worldMatrix;
    previewWvpData->WorldInverseTranspose = GetCachedWorldInverseTranspose(worldMatrix);
    previewCameraData->worldPosition = camera->GetEye();
    return true;
}

void MeshRenderer::DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 通常画面の材質選択を描画コンポーネントへ集約する。各描画関数は既存のPSOを使う。
    switch (GetMaterialType()) {
    case 8: DrawWater(depthSrvHandle, colorSrvHandle); break;
    case 9: DrawMagma(depthSrvHandle, colorSrvHandle); break;
    case 10: DrawIce(depthSrvHandle, colorSrvHandle); break;
    case 11: DrawFire(depthSrvHandle, colorSrvHandle); break;
    case 12: DrawLaser(depthSrvHandle, colorSrvHandle); break;
    case 13: DrawSlimeGel(depthSrvHandle, colorSrvHandle); break;
    case 14: DrawShockwave(depthSrvHandle, colorSrvHandle); break;
    case 15: DrawLiquidContact(depthSrvHandle, colorSrvHandle); break;
    case 16: DrawDamageCrack(depthSrvHandle, colorSrvHandle); break;
    case 17: DrawUpdraft(depthSrvHandle, colorSrvHandle); break;
    case 18: DrawStunBind(depthSrvHandle, colorSrvHandle); break;
    case 19: DrawCrownUnlock(depthSrvHandle, colorSrvHandle); break;
    case 20: DrawPoisonSpore(depthSrvHandle, colorSrvHandle); break;
    case 21: DrawCloud(depthSrvHandle, colorSrvHandle); break;
    case 22: DrawGatePortal(depthSrvHandle, colorSrvHandle); break;
    case 26: DrawWindOrb(depthSrvHandle, colorSrvHandle); break;
    default: break;
    }
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
    {
        // 別カメラの描画で通常画面の定数を上書きしないようにします。
        const int index = std::clamp(previewBufferIndex, 0, kPreviewBufferCount - 1);
        if (!waterParamData_ || !previewWaterData_[index]) return;
        *previewWaterData_[index] = *waterParamData_;
        previewWaterData_[index]->cameraWorldPosition = camera->GetEye();
        previewWaterData_[index]->waterInverseViewProjection = camera->GetInverseViewProjectionMatrix();
        DrawWaterWithWvp(previewWvpResource, depthSrvHandle, colorSrvHandle, previewWaterResources_[index].Get());
        break;
    }
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
    // 通常画面の WVP を使って水材質の共通描画経路へ渡す。
    DrawWaterWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawWaterWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, ID3D12Resource* waterResource) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !wvpResource) return;

    // 0/1は従来、2/3はイラスト調。それぞれ偶数が専用グリッドです。
    const bool useWaterGrid = !waterParamData_ || waterParamData_->effectType < 0.5f ||
        (waterParamData_->effectType >= 1.5f && waterParamData_->effectType < 2.5f);
    if (useWaterGrid && !waterProxyModel_) {
        InitializeWaterProxyModel();
    }

    common_->SetWaterGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, (waterResource ? waterResource : waterParamResource_.Get())->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);

    //  4番目にカラーテクスチャをセット
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    BindBakedShaderTextures(commandList, GetWaterBakedShaderTextures());

    drawModel = (useWaterGrid && waterProxyModel_) ? waterProxyModel_.get() : drawModel;
    drawModel->DrawMeshOnly((useWaterGrid && waterProxyModel_) ? -1 : meshDrawIndex_);
}

void MeshRenderer::DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 通常画面の WVP を使って不透明マグマの共通描画経路へ渡す。
    DrawMagmaWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawMagmaWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 水と同じ定数配置でマグマ専用 PSO を使い、深度と背景色を渡す。
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
    // 通常画面の WVP を使って氷材質の共通描画経路へ渡す。
    DrawIceWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawIceWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 半透明の氷も水系ルートの定数とテクスチャ配置を共用する。
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
    // 通常画面の WVP を使って炎材質の共通描画経路へ渡す。
    DrawFireWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle);
}

void MeshRenderer::DrawFireWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !wvpResource) return;

    // かがり火、まとい炎、火炎放射、炎柱は閉じた立体内の密度を積分します。
    const bool useVolumetricProxy = waterParamData_ &&
        ((waterParamData_->effectType >= 3.5f && waterParamData_->effectType < 7.5f) ||
         (waterParamData_->effectType >= 1.5f && waterParamData_->effectType < 2.5f));
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
    // 通常画面用の WVP を添えて各特殊材質の共有描画処理へ渡す。
    DrawSpecialMaterialWithWvp(wvpResource_.Get(), depthSrvHandle, colorSrvHandle, setGraphicsCommand, useProxyModel, bakedTextureMode);
}

void MeshRenderer::DrawSpecialMaterialWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel, int bakedTextureMode) {
    // 代理形状が必要な演出だけ差し替え、定数と SRV のルート番号は全材質で統一する。
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !waterParamResource_ || !setGraphicsCommand || !wvpResource) return;

    Model* proxyModel = nullptr;
    if (useProxyModel) {
        if (bakedTextureMode == 1) {
            const bool cylindricalBarrier = waterParamData_ && waterParamData_->effectType >= 3.5f;
            if (cylindricalBarrier) {
                if (!gateBarrierCylinderProxyModel_) {
                    InitializeGateBarrierCylinderProxyModel();
                }
                proxyModel = gateBarrierCylinderProxyModel_.get();
            } else {
                if (!gatePortalProxyModel_) {
                    InitializeGatePortalProxyModel();
                }
                proxyModel = gatePortalProxyModel_.get();
            }
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
    // 加算ビーム用 PSO を共有描画経路へ指定する。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLaserGraphicsCommand);
}

void MeshRenderer::DrawSlimeGel(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // ゼリー材質用 PSO を共有描画経路へ指定する。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetSlimeGelGraphicsCommand);
}

void MeshRenderer::DrawShockwave(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 地面衝撃波用 PSO を共有描画経路へ指定する。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetShockwaveGraphicsCommand);
}

void MeshRenderer::DrawLiquidContact(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 液面接触用 PSO を共有描画経路へ指定する。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLiquidContactGraphicsCommand);
}

void MeshRenderer::DrawDamageCrack(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // ひび割れ用 PSO を共有描画経路へ指定する。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetDamageCrackGraphicsCommand);
}

void MeshRenderer::DrawUpdraft(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 上昇気流は代理メッシュを使ってボリュームの見た目を作る。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetUpdraftGraphicsCommand, true);
}

void MeshRenderer::DrawStunBind(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 拘束光は代理メッシュで視点から形が読めるようにする。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetStunBindGraphicsCommand, true);
}

void MeshRenderer::DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 王冠解除の光は代理メッシュを使う。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCrownUnlockGraphicsCommand, true);
}

void MeshRenderer::DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 胞子は代理メッシュで薄い面を重ねる。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetPoisonSporeGraphicsCommand, true);
}

void MeshRenderer::DrawCloud(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 雲は代理メッシュを使用して体積感を出す。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCloudGraphicsCommand, true);
}

void MeshRenderer::DrawGatePortal(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // ポータルは専用焼き込みマスクと代理形状を組み合わせる。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetGatePortalGraphicsCommand, true, 1);
}

void MeshRenderer::DrawWindOrb(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    // 風の球は元モデルの形状を維持したまま専用 PSO へ切り替える。
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetWindOrbGraphicsCommand);
}

void MeshRenderer::InitializeWaterProxyModel() {
    // 水面シェーダー用に細分化した格子を作り、波の頂点変位を表現できるようにする。
    ModelCommon* modelCommon = ModelManager::GetInstance()->GetModelCommon();
    if (!modelCommon) {
        return;
    }

    waterProxyModel_ = std::make_unique<Model>();

    constexpr int kSegments = 128;
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
    // 平面型の炎で使う代理メッシュを必要時に生成する。
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

    std::vector<Model::VertexData> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(24);
    indices.reserve(36);

    // 箱の面は表示せず、内側の炎だけを描きます。面法線で二重描画を防ぎます。
    for (int face = 0; face < 6; ++face) {
        const int axis = face / 2;
        const float sign = (face % 2 == 0) ? -1.0f : 1.0f;
        const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
        for (int corner = 0; corner < 4; ++corner) {
            const float u = (corner == 2 || corner == 3) ? 1.0f : -1.0f;
            const float v = (corner == 1 || corner == 2) ? 1.0f : -1.0f;
            float p[3]{};
            float n[3]{};
            p[axis] = sign;
            p[(axis + 1) % 3] = u;
            p[(axis + 2) % 3] = v;
            n[axis] = sign;
            Model::VertexData vertex{};
            vertex.position = { p[0], p[1], p[2], 1.0f };
            vertex.texcoord = { u * 0.5f + 0.5f, v * 0.5f + 0.5f };
            vertex.normal = { n[0], n[1], n[2] };
            vertex.tangent = { 1.0f, 0.0f, 0.0f };
            vertices.push_back(vertex);
        }
        indices.insert(indices.end(), {
            firstVertex, firstVertex + 1, firstVertex + 2,
            firstVertex, firstVertex + 2, firstVertex + 3
        });
    }

    volumetricFireProxyModel_->CreateFromVertices(modelCommon, vertices, indices);
}

void MeshRenderer::InitializeGatePortalProxyModel() {
    // ポータルを描くための代理形状を元モデルとは別に保持する。
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

void MeshRenderer::InitializeGateBarrierCylinderProxyModel() {
    // 結界の設定では平面ではなく円筒の代理形状を使う。
    ModelCommon* modelCommon = ModelManager::GetInstance()->GetModelCommon();
    if (!modelCommon) {
        return;
    }

    constexpr int kSegments = 96;
    std::vector<Model::VertexData> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve((kSegments + 1) * 2);
    indices.reserve(kSegments * 6);
    for (int row = 0; row <= 1; ++row) {
        const float y = row == 0 ? -1.0f : 1.0f;
        for (int segment = 0; segment <= kSegments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float angle = u * 6.2831853f;
            const float x = std::sin(angle);
            const float z = std::cos(angle);
            Model::VertexData vertex{};
            vertex.position = {x, y, z, 1.0f};
            vertex.texcoord = {u, static_cast<float>(row)};
            vertex.normal = {x, 0.0f, z};
            vertex.tangent = {z, 0.0f, -x};
            vertices.push_back(vertex);
        }
    }
    for (int segment = 0; segment < kSegments; ++segment) {
        const uint32_t lower = static_cast<uint32_t>(segment);
        const uint32_t upper = lower + kSegments + 1;
        indices.insert(indices.end(), {lower, upper, lower + 1, lower + 1, upper, upper + 1});
    }

    gateBarrierCylinderProxyModel_ = std::make_unique<Model>();
    gateBarrierCylinderProxyModel_->CreateFromVertices(modelCommon, vertices, indices);
}

