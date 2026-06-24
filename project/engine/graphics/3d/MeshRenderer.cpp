#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include "TextureManager.h"
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
}

MeshRenderer::MeshRenderer(Transform* transform) {
    assert(transform);
    transform_ = transform;
}

void MeshRenderer::Initialize(Object3dCommon* common) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // 1. WVPバッファ
    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = Math::MakeIdentity4x4();
    wvpData_->world = Math::MakeIdentity4x4();



    // 3. Cameraバッファ
    cameraResource_ = dxCommon->CreateBufferResource(sizeof(CameraForGPU));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
    cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f };

    // 4. Materialバッファ
    materialResource_ = dxCommon->CreateBufferResource(sizeof(MaterialData));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = 1;
    materialData_->uvTransform = Math::MakeIdentity4x4();
    materialData_->selectedLighting = 2;
    materialData_->shininess = 20.0f;
    materialData_->materialType = 0;
    materialData_->roughness = 0.5f; // 程よくザラザラ（光沢が広がる）
    materialData_->metallic = 0.0f;  // 非金属（景色を反射しない）
    materialData_->enableNormalMap = 0;
    materialData_->enableEnvMap = 0;     // デフォルトoff
    materialData_->envIntensity = 1.0f;  // デフォルト1.0倍
    materialData_->emissive = 1.0f;
    materialData_->time = 0.0f;
    shadowWvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    shadowWvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&shadowWvpData_));
    shadowWvpData_->WVP = Math::MakeIdentity4x4();
    shadowWvpData_->world = Math::MakeIdentity4x4();

    localFogResource_ = dxCommon->CreateBufferResource(sizeof(LocalFogData));
    localFogResource_->Map(0, nullptr, reinterpret_cast<void**>(&localFogData_));
    localFogData_->fogColor = { 0.2f, 0.8f, 0.5f, 1.0f }; // 毒沼カラー
    localFogData_->fogDensity = 0.5f;

    waterParamResource_ = dxCommon->CreateBufferResource(sizeof(WaterParamForGPU));
    waterParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&waterParamData_));
    // デフォルト値のセット
    waterParamData_->time = 0.0f;
    waterParamData_->waveSpeed = 2.0f;
    waterParamData_->waveHeight = 0.5f;
    waterParamData_->waveFrequency = 1.5f;
    waterParamData_->flowSpeedX = 0.1f; // 緩やかに流れる
    waterParamData_->flowSpeedY = 0.1f;
    waterParamData_->effectType = 0.0f;
    waterParamData_->effectScale = 1.0f;
    waterParamData_->effectSoftness = 0.55f;
    waterParamData_->effectIntensity = 1.0f;
    waterParamData_->cameraWorldPosition = { 0.0f, 0.0f, -1.0f };
    waterParamData_->billboardScale = 0.55f;
    waterParamData_->effectScaleX = 1.0f;
    waterParamData_->effectScaleY = 1.0f;
    EnsureBakedShaderTexturesLoaded();
    
}

void MeshRenderer::Update() {
	// 経過時間を更新してGPUに転送
    time_ += 1.0f / 60.0f;
    if (materialData_) {
        materialData_->time = time_;
    }
    UpdateUvTransform();

    if (waterParamData_) {
        waterParamData_->time = time_; 

        // ★流速に基づいてオフセットを蓄積）
        waterParamData_->uvOffsetX += waterParamData_->flowSpeedX * (1.0f / 60.0f);
        waterParamData_->uvOffsetY += waterParamData_->flowSpeedY * (1.0f / 60.0f);
    }
    if (localFogData_) {
        localFogData_->time = time_;
        auto& sun = LightManager::GetInstance()->GetDirectionalLight();
        localFogData_->lightDirection = sun.direction;

        // 光の色に「輝度(intensity)」を掛け合わせて、より強い光にする
        localFogData_->lightColor = {
            sun.color.x * sun.intensity,
            sun.color.y * sun.intensity,
            sun.color.z * sun.intensity
        };

    }
    // Transformの計算結果 (matWorld) をGPUに転送する
    if (wvpData_ && transform_) {
        Math math;
        Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

        if (camera) {
            const Matrix4x4& view = camera->GetViewMatrix();
            const Matrix4x4& proj = camera->GetProjectionMatrix();
            Matrix4x4 viewProj = math.Multiply(view, proj);

            // Transform側ですでに計算されたワールド行列を使う
            const Matrix4x4& worldMatrix = transform_->matWorld;

            wvpData_->WVP = math.Multiply(worldMatrix, viewProj);
            wvpData_->world = worldMatrix;
            wvpData_->WorldInverseTranspose = math.Transpose(math.Inverse(worldMatrix));
            cameraData_->worldPosition = camera->GetEye();
            if (waterParamData_) {
                waterParamData_->cameraWorldPosition = camera->GetEye();
            }
            localFogData_->cameraPos = camera->GetEye();
            
            // 軽量化: ViewProjの逆行列はカメラ共通なのでキャッシュする
            static Matrix4x4 lastVP;
            static Matrix4x4 cachedInvVP;
            if (std::memcmp(&lastVP, &viewProj, sizeof(Matrix4x4)) != 0) {
                lastVP = viewProj;
                cachedInvVP = math.Inverse(viewProj);
            }
            localFogData_->inverseViewProj = cachedInvVP;
        } else {
            wvpData_->WVP = Math::MakeIdentity4x4();
            wvpData_->world = Math::MakeIdentity4x4();
        }
        if (isUIPreview_) {
            if (shadowWvpData_) {
                // 影マップの影響を受けないように、行列を初期化しておく
                shadowWvpData_->WVP = Math::MakeIdentity4x4();
                shadowWvpData_->world = Math::MakeIdentity4x4();
            }
            return; 
        }
        // ライト更新
        if (directionalLightData_) {
            directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
        }

        if (shadowWvpData_ && transform_) {
            Math math;

            Vector3 lightDir = LightManager::GetInstance()->GetDirectionalLight().direction;
            // 0除算防止のための安全対策を追加
            if (math.Length(lightDir) > 0.0001f) {
                lightDir = math.Normalize(lightDir);
            } else {
                lightDir = { 0.0f, -1.0f, 0.0f }; // デフォルトの下向き
            }

            // 1. カメラの位置を取得して、影の箱の「中心（ターゲット）」にする
            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            Vector3 target = { 0.0f, 0.0f, 0.0f };
            if (camera) {
                target = camera->GetEye(); // カメラ（プレイヤー）の位置を基準にする
            }

            // 2. 太陽の位置を、カメラから光の逆方向へ離す
            Vector3 lightPos = {
                target.x - lightDir.x * 200.0f,
                target.y - lightDir.y * 200.0f,
                target.z - lightDir.z * 200.0f
            };

            Vector3 up = { 0.0f, 1.0f, 0.0f };

            if (std::abs(lightDir.x) < 0.001f && std::abs(lightDir.z) < 0.001f) {
                up = { 0.0f, 0.0f, 1.0f };
            }

            // 太陽目線のビュー行列
            Matrix4x4 lightView = math.MakeLookAtMatrix(lightPos, target, up);

            Matrix4x4 lightProj = math.MakeOrthographicMatrix(80.0f, 80.0f, 1.0f, 400.0f);

            Matrix4x4 lightVP = math.Multiply(lightView, lightProj);
            LightManager::GetInstance()->GetDirectionalLight().lightViewProj = lightVP;
            // 影用のWVP = モデルのワールド行列 * 太陽のビュープロジェクション
            shadowWvpData_->WVP = math.Multiply(transform_->matWorld, lightVP);
            shadowWvpData_->world = transform_->matWorld;
        }
    }
}

void MeshRenderer::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_) return;
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
void MeshRenderer::DrawWater(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !waterParamResource_) return;

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
    if (!drawModel || !common_ || !waterParamResource_) return;

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
    if (!drawModel || !common_ || !waterParamResource_) return;

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
    if (!drawModel || !common_ || !waterParamResource_) return;

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
    if (!drawModel || !common_ || !waterParamResource_ || !setGraphicsCommand) return;

    if (useProxyModel && !fireProxyModel_) {
        InitializeFireProxyModel();
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
    drawModel = (useProxyModel && fireProxyModel_) ? fireProxyModel_.get() : drawModel;
    drawModel->DrawMeshOnly((useProxyModel && fireProxyModel_) ? -1 : meshDrawIndex_);
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

void MeshRenderer::SetModel(Model* model) {
    model_ = model;
    modelName_.clear();
    ClearLodLevels();
    if (model_ && materialData_) {
        if (const Model::MaterialData* material = model_->GetPrimaryMaterialData()) {
            materialData_->color = material->baseColorFactor;
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
            materialData_->color = material->baseColorFactor;
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

void MeshRenderer::SetColor(const Vector4& color) {
    if (materialData_) materialData_->color = color;
}

void MeshRenderer::SetMaterialType(int32_t type) {
    if (materialData_) materialData_->materialType = type;
}

void MeshRenderer::SetIntensity(float intensity) {
    if (directionalLightData_) directionalLightData_->intensity = intensity;
}

Vector4 MeshRenderer::GetColor() const {
    return materialData_ ? materialData_->color : Vector4{ 1,1,1,1 };
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
    normalMapPath_ = texturePath;
    if (!texturePath.empty()) {
        normalMapHandle_ = TextureManager::GetInstance()->Load(texturePath, true);
    } else {
        normalMapHandle_ = 0;
    }
}

void MeshRenderer::SetOrmMap(const std::string& texturePath) {
    ormMapPath_ = texturePath;
    if (!texturePath.empty()) {
        ormMapHandle_ = TextureManager::GetInstance()->Load(texturePath, true);
    } else {
        ormMapHandle_ = 0;
    }
}


void MeshRenderer::SetTexture(const std::string& texturePath) {
    texturePath_ = texturePath;
    if (!texturePath.empty()) {
        textureHandle_ = TextureManager::GetInstance()->Load(texturePath);
    } else {
        textureHandle_ = 0;
    }
}

void MeshRenderer::SetTextureTiling(const Vector2& tiling) {
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
    if (!materialData_) {
        return;
    }

    Vector2 effectiveTiling = {
        ClampTextureTiling(textureTiling_.x),
        ClampTextureTiling(textureTiling_.y)
    };

    if (autoTextureTiling_ && transform_) {
        Vector2 scaleTiling = MakeAutoTilingFromScale(transform_->scale);
        effectiveTiling.x *= scaleTiling.x;
        effectiveTiling.y *= scaleTiling.y;
    }

    materialData_->uvTransform = Math::MakeScaleMatrix({ effectiveTiling.x, effectiveTiling.y, 1.0f });
}


void MeshRenderer::DrawShadow() {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !shadowWvpResource_) return;
    common_->SetShadowGraphicsCommand();
    // 影用のパイプラインに変更
    common_->SetShadowPipelineState();

    // 軽量版のドローコールを呼ぶ
    drawModel->DrawShadow(shadowWvpResource_.Get(), meshDrawIndex_);
}

void MeshRenderer::SetShadowCommonState() {
    if (!common_) return;
    common_->SetShadowGraphicsCommand();
    common_->SetShadowPipelineState();
}

void MeshRenderer::DrawShadowOnly() {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !shadowWvpResource_) return;
    drawModel->DrawShadow(shadowWvpResource_.Get(), meshDrawIndex_);
}

void MeshRenderer::DrawLocalFog(uint32_t depthSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !localFogResource_) return;

    common_->SetLocalFogGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();


    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, depthSrvHandle);


    commandList->SetGraphicsRootConstantBufferView(3, localFogResource_->GetGPUVirtualAddress());

    // [0] にWVP、[1] にボーンが自動セットされる
    drawModel->DrawShadow(wvpResource_.Get(), meshDrawIndex_);
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
