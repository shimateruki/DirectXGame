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
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;

std::string NormalizeSlashes(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::vector<fs::path> MakeLodManifestCandidates(const std::string& modelName) {
    // モデル名がファイル指定かフォルダ指定かによって、LOD定義の候補順を切り替える。
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
    // 設定JSONが読めない場合は呼び出し元へ失敗を返し、描画設定の適用を止める。
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
    // 壁と床のどちらでも面積の大きい二軸をUV繰り返し量に使う。
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
    // 作成・Mapの片方でも失敗した場合は両方を空へ戻し、不完全なGPU定数を使わない。
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
    wvpData_->WVP = engine::Math::MakeIdentity4x4();
    wvpData_->world = engine::Math::MakeIdentity4x4();

    // 3. Cameraバッファ
    if (!CreateMappedBuffer(dxCommon, sizeof(CameraForGPU), cameraResource_, cameraData_, "Camera")) {
        common_ = nullptr;
        return;
    }
    cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f };

    // Camera Previewでは通常描画用のWVP/Camera定数を上書きしないよう、専用バッファを使います。
    // 右パネルPreviewと演出用Previewを同じフレームで描くため、GPU実行前の上書きを避ける目的で2面分確保します。
    for (int i = 0; i < kPreviewBufferCount; ++i) {
        if (!CreateMappedBuffer(dxCommon, sizeof(TransformationMatrix), previewWvpResources_[i], previewWvpData_[i], "PreviewWVP")) {
            common_ = nullptr;
            return;
        }
        previewWvpData_[i]->WVP = engine::Math::MakeIdentity4x4();
        previewWvpData_[i]->world = engine::Math::MakeIdentity4x4();
        previewWvpData_[i]->WorldInverseTranspose = engine::Math::MakeIdentity4x4();

        if (!CreateMappedBuffer(dxCommon, sizeof(CameraForGPU), previewCameraResources_[i], previewCameraData_[i], "PreviewCamera")) {
            common_ = nullptr;
            return;
        }
        previewCameraData_[i]->worldPosition = { 0.0f, 0.0f, 0.0f };
    }

    // 4. Materialバッファ
    if (!CreateMappedBuffer(dxCommon, sizeof(MaterialData), materialResource_, materialData_, "Material")) {
        common_ = nullptr;
        return;
    }
    materialColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->color = engine::ColorSpace::AuthoringToWorking(materialColor_);
    materialData_->enableLighting = 1;
    materialData_->uvTransform = engine::Math::MakeIdentity4x4();
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
    shadowWvpData_->WVP = engine::Math::MakeIdentity4x4();
    shadowWvpData_->world = engine::Math::MakeIdentity4x4();
    shadowWvpData_->WorldInverseTranspose = engine::Math::MakeIdentity4x4();

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
    waterParamData_->uvOffsetX = 0.0f;
    waterParamData_->uvOffsetY = 0.0f;
    waterParamData_->effectType = 0.0f;
    waterParamData_->effectScale = 1.0f;
    waterParamData_->effectSoftness = 0.55f;
    waterParamData_->effectIntensity = 1.0f;
    waterParamData_->cameraWorldPosition = { 0.0f, 0.0f, -1.0f };
    waterParamData_->billboardScale = 0.55f;
    waterParamData_->effectScaleX = 1.0f;
    waterParamData_->effectScaleY = 1.0f;
    waterParamData_->effectScaleZ = 1.0f;
    waterParamData_->topDownDetail = 1.0f;
    waterParamData_->waterLightDirection = { -0.35f, -0.82f, 0.45f };
    waterParamData_->waterLightIntensity = 1.0f;
    waterParamData_->waterLightColor = { 1.0f, 0.96f, 0.88f };
    waterParamData_->waterParamPadding1 = engine::ColorSpace::WorkflowSettings::GetInstance().IsLinearWorkflowEnabled() ? 1.0f : 0.0f;
    waterParamData_->waterInverseViewProjection = engine::Math::MakeIdentity4x4();
    waterParamData_->waterDebugView = 0.0f;
    for (float& padding : waterParamData_->waterDebugPadding) padding = 0.0f;
    for (int i = 0; i < kPreviewBufferCount; ++i) {
        if (!CreateMappedBuffer(dxCommon, sizeof(WaterParamForGPU),
            previewWaterResources_[i], previewWaterData_[i], "PreviewWater")) {
            common_ = nullptr;
            return;
        }
        *previewWaterData_[i] = *waterParamData_;
    }
    
}

bool MeshRenderer::HasRequiredBuffers() const {
    return wvpResource_ && wvpData_ &&
        cameraResource_ && cameraData_ &&
        materialResource_ && materialData_;
}

void MeshRenderer::SetVisualTransform(const engine::Vector3& scale, const engine::Vector3& rotation, const engine::Vector3& offset) {
    visualScale_ = scale;
    visualRotation_ = rotation;
    visualOffset_ = offset;
    worldInverseTransposeCacheValid_ = false;
}

void MeshRenderer::SetVisualShear(const engine::Vector3& horizontalShear, float pivotY) {
    visualShear_ = { horizontalShear.x, 0.0f, horizontalShear.z };
    visualShearPivotY_ = pivotY;
    worldInverseTransposeCacheValid_ = false;
}

void MeshRenderer::ResetVisualTransform() {
    SetVisualTransform({ 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
    SetVisualShear({ 0.0f, 0.0f, 0.0f }, 0.0f);
}

engine::Matrix4x4 MeshRenderer::BuildRenderWorldMatrix() const {
    // 表示専用の傾きと姿勢をローカルで合成してから、通常のワールド変換へ重ねる。
    if (!transform_) {
        return engine::Math::MakeIdentity4x4();
    }

    engine::Matrix4x4 shearLocal = engine::Math::MakeIdentity4x4();
    shearLocal.m[1][0] = visualShear_.x;
    shearLocal.m[1][2] = visualShear_.z;
    shearLocal.m[3][0] = -visualShearPivotY_ * visualShear_.x;
    shearLocal.m[3][2] = -visualShearPivotY_ * visualShear_.z;

    const engine::Matrix4x4 visualAffine = engine::Math::MakeAffineMatrix(visualScale_, visualRotation_, visualOffset_);
    engine::Matrix4x4 visualLocal = engine::Math::Multiply(shearLocal, visualAffine);
    if (const auto timelinePose = timelineVisualPose_.lock()) visualLocal = engine::Math::Multiply(*timelinePose, visualLocal);
    return engine::Math::Multiply(visualLocal, transform_->matWorld);
}

const engine::Matrix4x4& MeshRenderer::GetCachedWorldInverseTranspose(const engine::Matrix4x4& worldMatrix) {
    // 法線変換行列はワールド行列が変わったフレームだけ計算し直す。
    if (!worldInverseTransposeCacheValid_ ||
        std::memcmp(&cachedWorldMatrix_, &worldMatrix, sizeof(engine::Matrix4x4)) != 0) {
        cachedWorldMatrix_ = worldMatrix;
        cachedWorldInverseTranspose_ = engine::Math::Transpose(engine::Math::Inverse(worldMatrix));
        worldInverseTransposeCacheValid_ = true;
    }
    return cachedWorldInverseTranspose_;
}

void MeshRenderer::RefreshCameraDependentData() {
    // カメラ切り替え後はWVPだけでなく、水面・霧の逆ViewProjectionも同じ視点へ揃える。
    if (!common_ || !HasRequiredBuffers() || !shadowWvpData_ || !localFogData_ || !waterParamData_ || !transform_) {
        return;
    }

    engine::Math math;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

    if (camera) {
        const engine::Matrix4x4& viewProj = camera->GetViewProjectionMatrix();
        const engine::Matrix4x4 worldMatrix = BuildRenderWorldMatrix();

        wvpData_->WVP = math.Multiply(worldMatrix, viewProj);
        wvpData_->world = worldMatrix;
        wvpData_->WorldInverseTranspose = GetCachedWorldInverseTranspose(worldMatrix);
        cameraData_->worldPosition = camera->GetEye();
        waterParamData_->cameraWorldPosition = camera->GetEye();
        waterParamData_->waterInverseViewProjection = camera->GetInverseViewProjectionMatrix();
        localFogData_->cameraPos = camera->GetEye();
        localFogData_->inverseViewProj = camera->GetInverseViewProjectionMatrix();
    } else {
        wvpData_->WVP = engine::Math::MakeIdentity4x4();
        wvpData_->world = engine::Math::MakeIdentity4x4();
    }

    if (isUIPreview_) {
        // UIプレビューにはワールド影を投影しない。
        shadowWvpData_->WVP = engine::Math::MakeIdentity4x4();
        shadowWvpData_->world = engine::Math::MakeIdentity4x4();
        shadowWvpData_->WorldInverseTranspose = engine::Math::MakeIdentity4x4();
        return;
    }

    const engine::Matrix4x4& lightVP =
        LightManager::GetInstance()->GetDirectionalShadowViewProjection(camera);
    const engine::Matrix4x4 shadowWorld = BuildRenderWorldMatrix();
    shadowWvpData_->WVP = math.Multiply(shadowWorld, lightVP);
    shadowWvpData_->world = shadowWorld;
    shadowWvpData_->WorldInverseTranspose = GetCachedWorldInverseTranspose(shadowWorld);
}

void MeshRenderer::AdvanceWaterAnimation(float deltaTime) {
    if (!waterParamData_ || !std::isfinite(deltaTime) || deltaTime <= 0.0f) return;
    waterParamData_->time += deltaTime;
    waterParamData_->uvOffsetX += waterParamData_->flowSpeedX * deltaTime;
    waterParamData_->uvOffsetY += waterParamData_->flowSpeedY * deltaTime;
}

void MeshRenderer::Update() {
    if (!common_ || !HasRequiredBuffers() || !shadowWvpData_ || !localFogData_ || !waterParamData_) {
        return;
    }
	// 経過時間を更新してGPUに転送
    time_ += 1.0f / 60.0f;
    if (materialData_) {
        // 草木は実時間で更新する。行列更新やEditor操作で風の時刻を戻さない。
        if (materialData_->materialType != 30 && materialData_->materialType != 31) {
            materialData_->time = time_;
        }
        materialData_->color = engine::ColorSpace::AuthoringToWorking(materialColor_);
    }
    UpdateUvTransform();

    if (waterParamData_) {
        // 水以外の既存エフェクトは従来の時間更新を維持します。
        if (!materialData_ || materialData_->materialType != 8) {
            waterParamData_->time = time_;
            waterParamData_->uvOffsetX += waterParamData_->flowSpeedX * (1.0f / 60.0f);
            waterParamData_->uvOffsetY += waterParamData_->flowSpeedY * (1.0f / 60.0f);
        }

        auto& sun = LightManager::GetInstance()->GetDirectionalLight();
        waterParamData_->waterParamPadding1 = engine::ColorSpace::WorkflowSettings::GetInstance().IsLinearWorkflowEnabled() ? 1.0f : 0.0f;
        const engine::Vector4 sunColor = engine::ColorSpace::AuthoringToWorking(sun.color);
        waterParamData_->waterLightDirection = sun.direction;
        waterParamData_->waterLightIntensity = sun.intensity;
        waterParamData_->waterLightColor = { sunColor.x, sunColor.y, sunColor.z };
    }
    if (localFogData_) {
        localFogData_->time = time_;
        auto& sun = LightManager::GetInstance()->GetDirectionalLight();
        localFogData_->lightDirection = sun.direction;

        // 光の色に「輝度(intensity)」を掛け合わせて、より強い光にする
        const engine::Vector4 sunColor = engine::ColorSpace::AuthoringToWorking(sun.color);
        localFogData_->lightColor = {
            sunColor.x * sun.intensity,
            sunColor.y * sun.intensity,
            sunColor.z * sun.intensity
        };

    }
    // Transformの計算結果 (matWorld) をGPUに転送する
    if (wvpData_ && transform_) {
        engine::Math math;
        Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

        if (camera) {
            const engine::Matrix4x4& viewProj = camera->GetViewProjectionMatrix();

            // Transform側ですでに計算されたワールド行列を使う
            const engine::Matrix4x4 worldMatrix = BuildRenderWorldMatrix();

            wvpData_->WVP = math.Multiply(worldMatrix, viewProj);
            wvpData_->world = worldMatrix;
            wvpData_->WorldInverseTranspose = GetCachedWorldInverseTranspose(worldMatrix);
            cameraData_->worldPosition = camera->GetEye();
            if (waterParamData_) {
                waterParamData_->cameraWorldPosition = camera->GetEye();
                waterParamData_->waterInverseViewProjection = camera->GetInverseViewProjectionMatrix();
            }
            localFogData_->cameraPos = camera->GetEye();
            localFogData_->inverseViewProj = camera->GetInverseViewProjectionMatrix();
        } else {
            wvpData_->WVP = engine::Math::MakeIdentity4x4();
            wvpData_->world = engine::Math::MakeIdentity4x4();
        }
        if (isUIPreview_) {
            if (shadowWvpData_) {
                // 影マップの影響を受けないように、行列を初期化しておく
                shadowWvpData_->WVP = engine::Math::MakeIdentity4x4();
                shadowWvpData_->world = engine::Math::MakeIdentity4x4();
                shadowWvpData_->WorldInverseTranspose = engine::Math::MakeIdentity4x4();
            }
            return; 
        }
        if (shadowWvpData_ && transform_) {
            const engine::Matrix4x4& lightVP =
                LightManager::GetInstance()->GetDirectionalShadowViewProjection(camera);
            const engine::Matrix4x4 shadowWorld = BuildRenderWorldMatrix();
            shadowWvpData_->WVP = math.Multiply(shadowWorld, lightVP);
            shadowWvpData_->world = shadowWorld;
            shadowWvpData_->WorldInverseTranspose = GetCachedWorldInverseTranspose(shadowWorld);
        }
    }
}

