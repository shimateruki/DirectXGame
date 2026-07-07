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
// MeshRenderer 基本更新
// ------------------------------------------------------------------------
// GPUバッファ初期化、カメラ依存データ更新、通常更新を担当する。
// 材質設定、LOD、特殊マテリアル、影描画は別ファイルへ分けて見通しを保つ。
// ========================================================================
MeshRenderer::MeshRenderer(Transform* transform) {
    assert(transform);
    transform_ = transform;
}

void MeshRenderer::Initialize(Object3dCommon* common) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // 1. WVPバッファ
    if (!CreateMappedBuffer(dxCommon, sizeof(TransformationMatrix), wvpResource_, wvpData_, "WVP")) {
        common_ = nullptr;
        return;
    }
    wvpData_->WVP = Math::MakeIdentity4x4();
    wvpData_->world = Math::MakeIdentity4x4();



    // 3. Cameraバッファ
    if (!CreateMappedBuffer(dxCommon, sizeof(CameraForGPU), cameraResource_, cameraData_, "Camera")) {
        common_ = nullptr;
        return;
    }
    cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f };

    // 4. Materialバッファ
    if (!CreateMappedBuffer(dxCommon, sizeof(MaterialData), materialResource_, materialData_, "Material")) {
        common_ = nullptr;
        return;
    }
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
    materialData_->portalClipEnabled = 0.0f;
    materialData_->portalClipProgress = 0.0f;
    materialData_->portalClipCenter = { 0.0f, 0.0f, 0.0f };
    materialData_->portalClipEdgeWidth = 0.12f;
    materialData_->portalClipNormal = { 0.0f, 0.0f, 1.0f };
    materialData_->portalClipDissolve = 0.18f;
    materialData_->portalClipColor = { 1.0f, 0.82f, 0.36f, 0.80f };
    if (!CreateMappedBuffer(dxCommon, sizeof(TransformationMatrix), shadowWvpResource_, shadowWvpData_, "ShadowWVP")) {
        common_ = nullptr;
        return;
    }
    shadowWvpData_->WVP = Math::MakeIdentity4x4();
    shadowWvpData_->world = Math::MakeIdentity4x4();

    if (!CreateMappedBuffer(dxCommon, sizeof(LocalFogData), localFogResource_, localFogData_, "LocalFog")) {
        common_ = nullptr;
        return;
    }
    localFogData_->fogColor = { 0.2f, 0.8f, 0.5f, 1.0f }; // 毒沼カラー
    localFogData_->fogDensity = 0.5f;

    if (!CreateMappedBuffer(dxCommon, sizeof(WaterParamForGPU), waterParamResource_, waterParamData_, "WaterParam")) {
        common_ = nullptr;
        return;
    }
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
    waterParamData_->effectScaleZ = 1.0f;
    waterParamData_->waterParamPadding0 = 0.0f;
    EnsureBakedShaderTexturesLoaded();
    
}

bool MeshRenderer::HasRequiredBuffers() const {
    return wvpResource_ && wvpData_ &&
        cameraResource_ && cameraData_ &&
        materialResource_ && materialData_;
}

void MeshRenderer::RefreshCameraDependentData() {
    if (!common_ || !HasRequiredBuffers() || !shadowWvpData_ || !localFogData_ || !waterParamData_ || !transform_) {
        return;
    }

    Math math;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

    if (camera) {
        const Matrix4x4& view = camera->GetViewMatrix();
        const Matrix4x4& proj = camera->GetProjectionMatrix();
        Matrix4x4 viewProj = math.Multiply(view, proj);
        const Matrix4x4& worldMatrix = transform_->matWorld;

        wvpData_->WVP = math.Multiply(worldMatrix, viewProj);
        wvpData_->world = worldMatrix;
        wvpData_->WorldInverseTranspose = math.Transpose(math.Inverse(worldMatrix));
        cameraData_->worldPosition = camera->GetEye();
        waterParamData_->cameraWorldPosition = camera->GetEye();
        localFogData_->cameraPos = camera->GetEye();

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
        shadowWvpData_->WVP = Math::MakeIdentity4x4();
        shadowWvpData_->world = Math::MakeIdentity4x4();
        return;
    }

    if (directionalLightData_) {
        directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
    }

    Vector3 lightDir = LightManager::GetInstance()->GetDirectionalLight().direction;
    if (math.Length(lightDir) > 0.0001f) {
        lightDir = math.Normalize(lightDir);
    } else {
        lightDir = { 0.0f, -1.0f, 0.0f };
    }

    Vector3 target = { 0.0f, 0.0f, 0.0f };
    if (camera) {
        target = camera->GetEye();
    }

    Vector3 lightPos = {
        target.x - lightDir.x * 200.0f,
        target.y - lightDir.y * 200.0f,
        target.z - lightDir.z * 200.0f
    };

    Vector3 up = { 0.0f, 1.0f, 0.0f };
    if (std::abs(lightDir.x) < 0.001f && std::abs(lightDir.z) < 0.001f) {
        up = { 0.0f, 0.0f, 1.0f };
    }

    Matrix4x4 lightView = math.MakeLookAtMatrix(lightPos, target, up);
    Matrix4x4 lightProj = math.MakeOrthographicMatrix(80.0f, 80.0f, 1.0f, 400.0f);
    Matrix4x4 lightVP = math.Multiply(lightView, lightProj);
    LightManager::GetInstance()->GetDirectionalLight().lightViewProj = lightVP;

    shadowWvpData_->WVP = math.Multiply(transform_->matWorld, lightVP);
    shadowWvpData_->world = transform_->matWorld;
}

void MeshRenderer::Update() {
    if (!common_ || !HasRequiredBuffers() || !shadowWvpData_ || !localFogData_ || !waterParamData_) {
        return;
    }
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

