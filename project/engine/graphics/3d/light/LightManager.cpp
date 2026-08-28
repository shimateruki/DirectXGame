#include "LightManager.h"
#include "Object3d.h" // 追従対象の座標を取得するために必要
#include "Camera.h"
#include "CameraManager.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cmath>      // sin, cos
#include <algorithm>
#include "json.hpp"
#include "TextureManager.h"
#include "engine/graphics/core/ColorSpace.h"
#include "PostEffect.h"

using json = nlohmann::json;

namespace {
bool IntersectsFrustumSphere(const Frustum& frustum, const Vector3& center, float radius) {
    const float safeRadius = (std::max)(radius, 0.0f);
    for (const Plane& plane : frustum.planes) {
        const float signedDistance =
            plane.normal.x * center.x +
            plane.normal.y * center.y +
            plane.normal.z * center.z +
            plane.distance;
        if (signedDistance < -safeRadius) {
            return false;
        }
    }
    return true;
}
}

LightManager* LightManager::GetInstance() {
    static LightManager instance;
    return &instance;
}

void LightManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    uint32_t envHandle = TextureManager::GetInstance()->Load(
        "Resources/output_skybox.dds",
        TextureManager::TextureColorSpace::SRGB);
    LightManager::GetInstance()->SetEnvironmentMapHandle(envHandle);
    skyboxTexturePath_ = "Resources/output_skybox.dds";
    skyboxTextureHandle_ = envHandle;

    // --- バッファ作成 (サイズはConstData構造体に合わせる) ---
    pointLightResource_ = dxCommon_->CreateBufferResource(sizeof(PointLightConstData));
    pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightConstData_));
    pointLightConstData_->activeCount = 0;

    spotLightResource_ = dxCommon_->CreateBufferResource(sizeof(SpotLightConstData));
    spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightConstData_));
    spotLightConstData_->activeCount = 0;

    directionalLightResource_ = dxCommon_->CreateBufferResource(sizeof(DirectionalLight));

    // 平行光源の初期設定
    directionalLightData_.color = { 1.0f, 0.96f, 0.88f, 1.0f };
    directionalLightData_.direction = { -0.35f, -0.82f, 0.45f };
    directionalLightData_.intensity = 0.86f;
    directionalLightData_.ambientColor = { 0.34f, 0.38f, 0.42f };
    directionalLightData_.fogStart = 10.0f;
    directionalLightData_.fogEnd = 1000.0f;
    directionalLightData_.fogColor = { 0.66f, 0.76f, 0.86f };
    sceneClearColor_ = { 0.52f, 0.68f, 0.84f, 1.0f };
    ApplySceneClearColor();

    pointLights_.clear();
    spotLights_.clear();
}

void LightManager::Update() {

    Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
    const Frustum* activeFrustum = activeCamera ? &activeCamera->GetFrustum() : nullptr;

    // ---------------------------------------------------
    // 1. 平行光源 (Directional Light)
    // ---------------------------------------------------
    GetDirectionalShadowViewProjection(CameraManager::GetInstance()->GetActiveCamera());
    if (directionalLightResource_) {
        DirectionalLight* dirMap = nullptr;

        // 1. Mapを実行し、かつ成功(SUCCEEDED)したか、ポインタがNULLでないかを確認
        if (SUCCEEDED(directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&dirMap))) && dirMap) {

            // 2. Normalizeする前に、ベクトルの長さが0でないかチェック(0除算防止)
            if (Math::Length(directionalLightData_.direction) > 0.0001f) {
                directionalLightData_.direction = Math::Normalize(directionalLightData_.direction);
            } else {
                // 長さが0ならデフォルトの向きをセット
                directionalLightData_.direction = { 0.0f, -1.0f, 0.0f };
            }

            // 3. 安全が確認できたので書き込む
            DirectionalLight gpuLight = directionalLightData_;
            gpuLight.color = ColorSpace::AuthoringToWorking(gpuLight.color);
            gpuLight.ambientColor = ColorSpace::AuthoringToWorking(gpuLight.ambientColor);
            gpuLight.fogColor = ColorSpace::AuthoringToWorking(gpuLight.fogColor);
            *dirMap = gpuLight;

            // 4. アンマップ
            directionalLightResource_->Unmap(0, nullptr);
        } else {
            // ここに来る場合は、GPUデバイスが失われている可能性があります
            // OutputDebugStringA("Warning: DirectionalLight Map failed.\n");
        }
    }

    // ---------------------------------------------------
    // 2. 点光源 (Point Lights)
    // ---------------------------------------------------
    pointLightConstData_->activeCount = 0;

    for (auto& instance : pointLights_) {
        // 最大数チェック
        if (pointLightConstData_->activeCount >= kMaxPointLights) break;

        // --- ロジック更新 (CPU側) ---

        // A. 追従処理
        if (instance.target) {
            Vector3 targetPos = instance.target->GetTranslate(); // または GetWorldPosition()
            instance.data.position = {
                targetPos.x + instance.offset.x,
                targetPos.y + instance.offset.y,
                targetPos.z + instance.offset.z
            };
        }

        // B. アニメーション処理 (点滅・明滅)
        if (instance.mode == LightMode::Flicker) {
            instance.timer += 1.0f / 60.0f * instance.speed;
            // ランダムなノイズ (故障したライト風)
            float noise = (float)(rand() % 100) / 100.0f;
            instance.data.intensity = instance.baseIntensity * (0.5f + noise * 0.5f);
        } else if (instance.mode == LightMode::SineWave) {
            instance.timer += 1.0f / 60.0f * instance.speed;
            // サイン波 (呼吸のような明滅)
            float wave = (std::sin(instance.timer) + 1.0f) * 0.5f;
            instance.data.intensity = instance.baseIntensity * (0.5f + wave * 0.5f);
        } else {
            // 通常時は基準値に戻す (モード切替時に暗くならないように)
            instance.data.intensity = instance.baseIntensity;
        }

        if (instance.data.intensity <= 0.0001f || instance.data.radius <= 0.0001f) {
            continue;
        }
        if (activeFrustum && !IntersectsFrustumSphere(*activeFrustum, instance.data.position, instance.data.radius)) {
            continue;
        }

        // --- GPUバッファへコピー ---
        int index = pointLightConstData_->activeCount;
        MeshRenderer::PointLight gpuLight = instance.data;
        gpuLight.color = ColorSpace::AuthoringToWorking(gpuLight.color);
        pointLightConstData_->lights[index] = gpuLight;
        pointLightConstData_->activeCount++;
    }

    for (size_t index = 0; index < transientPointLights_.size();) {
        TransientPointLight& pulse = transientPointLights_[index];
        pulse.age += 1.0f / 60.0f;
        const float duration = (std::max)(pulse.duration, 0.001f);
        const float progress = std::clamp(pulse.age / duration, 0.0f, 1.0f);
        const float envelope = (1.0f - progress) * (1.0f - progress);
        pulse.data.intensity = pulse.baseIntensity * envelope;

        if (progress >= 1.0f) {
            transientPointLights_[index] = transientPointLights_.back();
            transientPointLights_.pop_back();
            continue;
        }

        if (pulse.data.intensity <= 0.0001f || pulse.data.radius <= 0.0001f ||
            (activeFrustum && !IntersectsFrustumSphere(*activeFrustum, pulse.data.position, pulse.data.radius))) {
            ++index;
            continue;
        }

        if (pointLightConstData_->activeCount < kMaxPointLights) {
            int transientIndex = pointLightConstData_->activeCount;
            MeshRenderer::PointLight gpuLight = pulse.data;
            gpuLight.color = ColorSpace::AuthoringToWorking(gpuLight.color);
            pointLightConstData_->lights[transientIndex] = gpuLight;
            pointLightConstData_->activeCount++;
        }
        ++index;
    }

    // ---------------------------------------------------
    // 3. スポットライト (Spot Lights)
    // ---------------------------------------------------
    spotLightConstData_->activeCount = 0;

    for (auto& instance : spotLights_) {
        if (spotLightConstData_->activeCount >= kMaxSpotLights) break;

        // --- ロジック更新 ---

        // A. 追従処理 & 向き同期 (懐中電灯)
        if (instance.target) {
            Vector3 targetPos = instance.target->GetTranslate();
            instance.data.position = {
                targetPos.x + instance.offset.x,
                targetPos.y + instance.offset.y,
                targetPos.z + instance.offset.z
            };

            // ターゲットの正面方向を取得してライトの向きにする
            // 行列のZ軸ベクトル(前方)を取得するのが最も確実
            Matrix4x4 worldMat = instance.target->GetWorldMatrix();
            Vector3 forward = { worldMat.m[0][2], worldMat.m[1][2], worldMat.m[2][2] };

            // 正規化
            float len = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
            if (len > 0.0001f) {
                instance.data.direction = { forward.x / len, forward.y / len, forward.z / len };
            }
        }

        // B. アニメーション
        if (instance.mode == LightMode::Flicker) {
            instance.timer += 1.0f / 60.0f * instance.speed;
            float noise = (float)(rand() % 100) / 100.0f;
            instance.data.intensity = instance.baseIntensity * (0.5f + noise * 0.5f);
        } else if (instance.mode == LightMode::SineWave) {
            instance.timer += 1.0f / 60.0f * instance.speed;
            float wave = (std::sin(instance.timer) + 1.0f) * 0.5f;
            instance.data.intensity = instance.baseIntensity * (0.5f + wave * 0.5f);
        } else {
            instance.data.intensity = instance.baseIntensity;
        }

        if (instance.data.intensity <= 0.0001f || instance.data.distance <= 0.0001f) {
            continue;
        }
        if (activeFrustum && !IntersectsFrustumSphere(*activeFrustum, instance.data.position, instance.data.distance)) {
            continue;
        }

        // --- GPUバッファへコピー ---
        int index = spotLightConstData_->activeCount;
        MeshRenderer::SpotLight gpuLight = instance.data;
        gpuLight.color = ColorSpace::AuthoringToWorking(gpuLight.color);
        spotLightConstData_->lights[index] = gpuLight;
        spotLightConstData_->activeCount++;
    }
}

const Matrix4x4& LightManager::GetDirectionalShadowViewProjection(const Camera* camera) {
    Math math;
    Vector3 lightDirection = directionalLightData_.direction;
    if (Math::Length(lightDirection) > 0.0001f) {
        lightDirection = Math::Normalize(lightDirection);
    }
    else {
        lightDirection = { 0.0f, -1.0f, 0.0f };
    }
    directionalLightData_.direction = lightDirection;

    const Vector3 target = camera ? camera->GetEye() : Vector3{};
    const float areaSize = std::clamp(shadowAreaSize_, 20.0f, 240.0f);
    const auto changed = [](float lhs, float rhs) {
        return std::abs(lhs - rhs) > 0.0001f;
    };
    const bool eyeChanged =
        changed(target.x, cachedShadowEye_.x) ||
        changed(target.y, cachedShadowEye_.y) ||
        changed(target.z, cachedShadowEye_.z);
    const bool directionChanged =
        changed(lightDirection.x, cachedShadowDirection_.x) ||
        changed(lightDirection.y, cachedShadowDirection_.y) ||
        changed(lightDirection.z, cachedShadowDirection_.z);

    if (!shadowMatrixCacheValid_ || eyeChanged || directionChanged || changed(areaSize, cachedShadowAreaSize_)) {
        const Vector3 lightPosition = {
            target.x - lightDirection.x * 200.0f,
            target.y - lightDirection.y * 200.0f,
            target.z - lightDirection.z * 200.0f,
        };
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        if (std::abs(lightDirection.x) < 0.001f && std::abs(lightDirection.z) < 0.001f) {
            up = { 0.0f, 0.0f, 1.0f };
        }

        const Matrix4x4 lightView = math.MakeLookAtMatrix(lightPosition, target, up);
        const Matrix4x4 lightProjection = math.MakeOrthographicMatrix(areaSize, areaSize, 1.0f, 400.0f);
        directionalLightData_.lightViewProj = math.Multiply(lightView, lightProjection);
        cachedShadowEye_ = target;
        cachedShadowDirection_ = lightDirection;
        cachedShadowAreaSize_ = areaSize;
        shadowMatrixCacheValid_ = true;
    }

    return directionalLightData_.lightViewProj;
}

int LightManager::GetShadowMapResolution() const {
    return dxCommon_ ? dxCommon_->GetShadowMapResolution() : 2048;
}

void LightManager::SetShadowMapResolution(int resolution) {
    if (dxCommon_) {
        dxCommon_->SetShadowMapResolution(resolution);
    }
}

void LightManager::SetShadowAreaSize(float size) {
    shadowAreaSize_ = std::clamp(size, 20.0f, 240.0f);
    shadowMatrixCacheValid_ = false;
}

void LightManager::ApplySceneClearColor() {
    if (!dxCommon_) {
        return;
    }
    dxCommon_->SetRenderClearColor(
        sceneClearColor_.x,
        sceneClearColor_.y,
        sceneClearColor_.z,
        sceneClearColor_.w);
}

// 戻り値を Instance* に変更
LightManager::PointLightInstance* LightManager::AddPointLight() {
    if (pointLights_.size() >= kMaxPointLights) return nullptr;

    PointLightInstance instance;
    instance.name = "PointLight " + std::to_string(pointLights_.size());
    // デフォルト値
    instance.data.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    instance.data.position = { 0.0f, 2.0f, 0.0f };
    instance.data.intensity = 1.0f;
    instance.data.radius = 10.0f;
    instance.data.decay = 1.0f;

    // 制御用データの初期化
    instance.baseIntensity = 1.0f;
    instance.mode = LightMode::None;
    instance.target = nullptr;

    pointLights_.push_back(instance);
    return &pointLights_.back();
}

// 戻り値を Instance* に変更
LightManager::SpotLightInstance* LightManager::AddSpotLight() {
    if (spotLights_.size() >= kMaxSpotLights) return nullptr;

    SpotLightInstance instance;
    instance.name = "SpotLight " + std::to_string(spotLights_.size());
    instance.data.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    instance.data.position = { 0.0f, 5.0f, 0.0f };
    instance.data.direction = { 0.0f, -1.0f, 0.0f };
    instance.data.intensity = 2.0f;
    instance.data.distance = 20.0f;
    instance.data.decay = 1.0f;
    instance.data.cosAngle = 0.9f;
    instance.data.cosFalloffStart = 0.95f;

    // 制御用データ
    instance.baseIntensity = 2.0f;
    instance.mode = LightMode::None;
    instance.target = nullptr;

    spotLights_.push_back(instance);
    return &spotLights_.back();
}

void LightManager::ClearAllLights() {
    pointLights_.clear();
    spotLights_.clear();
    transientPointLights_.clear();
}

void LightManager::PlayPointLightPulse(
    const Vector3& position,
    const Vector4& color,
    float intensity,
    float radius,
    float duration,
    float decay) {
    TransientPointLight pulse;
    pulse.data.color = color;
    pulse.data.position = position;
    pulse.data.intensity = intensity;
    pulse.data.radius = radius;
    pulse.data.decay = decay;
    pulse.baseIntensity = intensity;
    pulse.duration = (std::max)(duration, 0.001f);
    transientPointLights_.push_back(pulse);
}

bool LightManager::SetSkyboxTexturePath(const std::string& texturePath) {
    if (texturePath.empty()) {
        return false;
    }

    try {
        if (!std::filesystem::exists(texturePath)) {
            return false;
        }
        skyboxTexturePath_ = texturePath;
        skyboxTextureHandle_ = TextureManager::GetInstance()->Load(
            texturePath,
            TextureManager::TextureColorSpace::SRGB);
        return true;
    } catch (...) {
        return false;
    }
}

bool LightManager::SaveState(const std::string& filename) {
    json root;

    root["clearColor"] = { sceneClearColor_.x, sceneClearColor_.y, sceneClearColor_.z, sceneClearColor_.w };
    root["skybox"]["enabled"] = skyboxEnabled_;
    root["skybox"]["texture"] = skyboxTexturePath_;
    root["shadow"]["resolution"] = GetShadowMapResolution();
    root["shadow"]["areaSize"] = shadowAreaSize_;

    // --- 平行光源 ---
    root["directionalLight"]["color"] = { directionalLightData_.color.x, directionalLightData_.color.y, directionalLightData_.color.z, directionalLightData_.color.w };
    root["directionalLight"]["direction"] = { directionalLightData_.direction.x, directionalLightData_.direction.y, directionalLightData_.direction.z };
    root["directionalLight"]["intensity"] = directionalLightData_.intensity;
    root["directionalLight"]["ambientColor"] = { directionalLightData_.ambientColor.x, directionalLightData_.ambientColor.y, directionalLightData_.ambientColor.z };
    root["directionalLight"]["fogStart"] = directionalLightData_.fogStart;
    root["directionalLight"]["fogEnd"] = directionalLightData_.fogEnd;
    root["directionalLight"]["fogColor"] = { directionalLightData_.fogColor.x, directionalLightData_.fogColor.y, directionalLightData_.fogColor.z };
    root["directionalLight"]["fogHeightMin"] = directionalLightData_.fogHeightMin;
    root["directionalLight"]["fogHeightMax"] = directionalLightData_.fogHeightMax;
    root["directionalLight"]["volumetricIntensity"] = directionalLightData_.volumetricIntensity;
    root["directionalLight"]["volumetricSteps"] = directionalLightData_.volumetricSteps;
    root["directionalLight"]["enableFog"] = directionalLightData_.enableFog;

    // --- 点光源 ---
    json pArray = json::array();
    for (const auto& instance : pointLights_) {
        json j;
        j["name"] = instance.name;
        // GPUデータ
        const auto& l = instance.data;
        j["position"] = { l.position.x, l.position.y, l.position.z };
        j["color"] = { l.color.x, l.color.y, l.color.z, l.color.w };
        j["intensity"] = l.intensity; // ここは現在の値を保存
        j["radius"] = l.radius;
        j["decay"] = l.decay;

        // 制御データ (拡張)
        j["baseIntensity"] = instance.baseIntensity;
        j["mode"] = static_cast<int>(instance.mode);
        j["speed"] = instance.speed;
        j["offset"] = { instance.offset.x, instance.offset.y, instance.offset.z };

        pArray.push_back(j);
    }
    root["pointLights"] = pArray;

    // --- スポットライト ---
    json sArray = json::array();
    for (const auto& instance : spotLights_) {
        json j;
        j["name"] = instance.name;
        const auto& l = instance.data;
        j["position"] = { l.position.x, l.position.y, l.position.z };
        j["direction"] = { l.direction.x, l.direction.y, l.direction.z };
        j["color"] = { l.color.x, l.color.y, l.color.z, l.color.w };
        j["intensity"] = l.intensity;
        j["distance"] = l.distance;
        j["decay"] = l.decay;
        j["cosAngle"] = l.cosAngle;
        j["cosFalloffStart"] = l.cosFalloffStart;

        // 制御データ
        j["baseIntensity"] = instance.baseIntensity;
        j["mode"] = static_cast<int>(instance.mode);
        j["speed"] = instance.speed;
        j["offset"] = { instance.offset.x, instance.offset.y, instance.offset.z };

        sArray.push_back(j);
    }
    root["spotLights"] = sArray;

    try {
        const std::filesystem::path path(filename);
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
    } catch (...) {
        return false;
    }

    std::ofstream file(filename, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << root.dump(4);
    const bool result = file.good();
    if (result) {
        currentStateFile_ = filename;
        lastLoadSucceeded_ = true;
    }
    return result;
}

bool LightManager::LoadState(const std::string& filename) {
    currentStateFile_ = filename;
    lastLoadSucceeded_ = false;

    std::ifstream file(filename);
    if (!file.is_open()) return false;

    json root;
    try { file >> root; }
    catch (...) { return false; }

    ClearAllLights();

    if (root.contains("clearColor") && root["clearColor"].is_array() && root["clearColor"].size() >= 4) {
        sceneClearColor_.x = root["clearColor"][0];
        sceneClearColor_.y = root["clearColor"][1];
        sceneClearColor_.z = root["clearColor"][2];
        sceneClearColor_.w = root["clearColor"][3];
    } else {
        sceneClearColor_ = { 0.1f, 0.25f, 0.5f, 1.0f };
    }
    ApplySceneClearColor();

    skyboxEnabled_ = true;
    if (root.contains("skybox") && root["skybox"].is_object()) {
        const json& skybox = root["skybox"];
        if (skybox.contains("enabled")) {
            skyboxEnabled_ = skybox["enabled"].get<bool>();
        }
        if (skybox.contains("texture") && skybox["texture"].is_string()) {
            const std::string texturePath = skybox["texture"].get<std::string>();
            if (!SetSkyboxTexturePath(texturePath)) {
                SetSkyboxTexturePath("Resources/output_skybox.dds");
            }
        }
    } else {
        SetSkyboxTexturePath("Resources/output_skybox.dds");
    }

    int shadowResolution = 2048;
    float shadowAreaSize = 80.0f;
    if (root.contains("shadow") && root["shadow"].is_object()) {
        const json& shadow = root["shadow"];
        shadowResolution = shadow.value("resolution", shadowResolution);
        shadowAreaSize = shadow.value("areaSize", shadowAreaSize);
    }
    SetShadowMapResolution(shadowResolution);
    SetShadowAreaSize(shadowAreaSize);

    // --- 平行光源 ---
    if (root.contains("directionalLight")) {
        json& d = root["directionalLight"];
        if (d.contains("color")) {
            directionalLightData_.color.x = d["color"][0]; directionalLightData_.color.y = d["color"][1];
            directionalLightData_.color.z = d["color"][2]; directionalLightData_.color.w = d["color"][3];
        }
        if (d.contains("direction")) {
            directionalLightData_.direction.x = d["direction"][0]; directionalLightData_.direction.y = d["direction"][1];
            directionalLightData_.direction.z = d["direction"][2];
        }
        if (d.contains("intensity")) directionalLightData_.intensity = d["intensity"];
        if (d.contains("ambientColor")) {
            directionalLightData_.ambientColor.x = d["ambientColor"][0]; directionalLightData_.ambientColor.y = d["ambientColor"][1];
            directionalLightData_.ambientColor.z = d["ambientColor"][2];
        }

        // フォグの設定
        if (d.contains("fogStart")) directionalLightData_.fogStart = d["fogStart"];
        if (d.contains("fogEnd")) directionalLightData_.fogEnd = d["fogEnd"];
        if (d.contains("fogColor")) {
            directionalLightData_.fogColor.x = d["fogColor"][0]; directionalLightData_.fogColor.y = d["fogColor"][1];
            directionalLightData_.fogColor.z = d["fogColor"][2];
        }
        if (d.contains("fogHeightMin")) directionalLightData_.fogHeightMin = d["fogHeightMin"];
        if (d.contains("fogHeightMax")) directionalLightData_.fogHeightMax = d["fogHeightMax"];

        if (d.contains("volumetricIntensity")) directionalLightData_.volumetricIntensity = d["volumetricIntensity"];
        if (d.contains("volumetricSteps")) directionalLightData_.volumetricSteps = d["volumetricSteps"];
        if (d.contains("enableFog")) directionalLightData_.enableFog = d["enableFog"];
    }

    // --- 点光源 ---
    if (root.contains("pointLights") && root["pointLights"].is_array()) {
        for (const auto& j : root["pointLights"]) {
            auto instance = AddPointLight(); // Instance* が返る
            if (instance) {
                auto& l = instance->data;
                if (j.contains("name")) instance->name = j["name"];
                if (j.contains("position")) { l.position.x = j["position"][0]; l.position.y = j["position"][1]; l.position.z = j["position"][2]; }
                if (j.contains("color")) { l.color.x = j["color"][0]; l.color.y = j["color"][1]; l.color.z = j["color"][2]; l.color.w = j["color"][3]; }
                if (j.contains("intensity")) l.intensity = j["intensity"];
                if (j.contains("radius")) l.radius = j["radius"];
                if (j.contains("decay")) l.decay = j["decay"];

                // 制御データ読み込み
                if (j.contains("baseIntensity")) instance->baseIntensity = j["baseIntensity"];
                else instance->baseIntensity = l.intensity; // 古いデータ互換

                if (j.contains("mode")) instance->mode = (LightMode)j["mode"];
                if (j.contains("speed")) instance->speed = j["speed"];
                if (j.contains("offset")) { instance->offset.x = j["offset"][0]; instance->offset.y = j["offset"][1]; instance->offset.z = j["offset"][2]; }
            }
        }
    }

    // --- スポットライト ---
    if (root.contains("spotLights") && root["spotLights"].is_array()) {
        for (const auto& j : root["spotLights"]) {
            auto instance = AddSpotLight();
            if (instance) {
                auto& l = instance->data;
                if (j.contains("name")) instance->name = j["name"];
                if (j.contains("position")) { l.position.x = j["position"][0]; l.position.y = j["position"][1]; l.position.z = j["position"][2]; }
                if (j.contains("direction")) { l.direction.x = j["direction"][0]; l.direction.y = j["direction"][1]; l.direction.z = j["direction"][2]; }
                if (j.contains("color")) { l.color.x = j["color"][0]; l.color.y = j["color"][1]; l.color.z = j["color"][2]; l.color.w = j["color"][3]; }
                if (j.contains("intensity")) l.intensity = j["intensity"];
                if (j.contains("distance")) l.distance = j["distance"];
                if (j.contains("decay")) l.decay = j["decay"];
                if (j.contains("cosAngle")) l.cosAngle = j["cosAngle"];
                if (j.contains("cosFalloffStart")) l.cosFalloffStart = j["cosFalloffStart"];

                // 制御データ
                if (j.contains("baseIntensity")) instance->baseIntensity = j["baseIntensity"];
                else instance->baseIntensity = l.intensity;

                if (j.contains("mode")) instance->mode = (LightMode)j["mode"];
                if (j.contains("speed")) instance->speed = j["speed"];
                if (j.contains("offset")) { instance->offset.x = j["offset"][0]; instance->offset.y = j["offset"][1]; instance->offset.z = j["offset"][2]; }
            }
        }
    }
    lastLoadSucceeded_ = true;
    return true;
}
LightManager::EnvironmentProfileState LightManager::CaptureEnvironmentProfile() const {
    EnvironmentProfileState profile;
    profile.directionalLight = directionalLightData_;
    profile.clearColor = sceneClearColor_;
    profile.shadowMapResolution = GetShadowMapResolution();
    profile.shadowAreaSize = shadowAreaSize_;
    profile.skyboxEnabled = skyboxEnabled_;
    profile.skyboxTexturePath = skyboxTexturePath_;

    PostEffect* postEffect = PostEffect::GetInstance();
    if (const PostEffect::Params* params = postEffect->GetParams()) {
        profile.post.bloomEnabled = postEffect->IsBloomEnabled();
        profile.post.bloomQuality = static_cast<int>(postEffect->GetBloomQuality());
        profile.post.threshold = params->threshold;
        profile.post.bloomIntensity = params->bloomIntensity;
        profile.post.spread = params->spread;
        profile.post.enableToneMapping = params->enableToneMapping;
        profile.post.vignetteIntensity = params->vignetteIntensity;
        profile.post.vignettePower = params->vignettePower;
        profile.post.chromaticAberration = params->chromaticAberration;
        profile.post.filmGrainIntensity = params->filmGrainIntensity;
        profile.post.lutIntensity = params->lutIntensity;
        profile.post.lutTexturePath = postEffect->GetLUTTexturePath();
        profile.post.colorExposure = params->colorExposure;
        profile.post.colorContrast = params->colorContrast;
        profile.post.colorSaturation = params->colorSaturation;
        profile.post.colorTemperature = params->colorTemperature;
        profile.post.colorTint = params->colorTint;
    }
    return profile;
}

bool LightManager::SaveEnvironmentProfile(const std::string& filename) const {
    if (filename.empty()) {
        return false;
    }

    const EnvironmentProfileState profile = CaptureEnvironmentProfile();
    const DirectionalLight& light = profile.directionalLight;
    const EnvironmentPostSettings& post = profile.post;

    json root;
    root["schemaVersion"] = 1;
    root["directionalLight"] = {
        { "color", { light.color.x, light.color.y, light.color.z, light.color.w } },
        { "direction", { light.direction.x, light.direction.y, light.direction.z } },
        { "intensity", light.intensity },
        { "ambientColor", { light.ambientColor.x, light.ambientColor.y, light.ambientColor.z } },
        { "fogStart", light.fogStart },
        { "fogEnd", light.fogEnd },
        { "fogColor", { light.fogColor.x, light.fogColor.y, light.fogColor.z } },
        { "fogHeightMin", light.fogHeightMin },
        { "fogHeightMax", light.fogHeightMax },
        { "volumetricIntensity", light.volumetricIntensity },
        { "volumetricSteps", light.volumetricSteps },
        { "enableFog", light.enableFog }
    };
    root["clearColor"] = {
        profile.clearColor.x, profile.clearColor.y,
        profile.clearColor.z, profile.clearColor.w
    };
    root["shadow"] = {
        { "resolution", profile.shadowMapResolution },
        { "areaSize", profile.shadowAreaSize }
    };
    root["skybox"] = {
        { "enabled", profile.skyboxEnabled },
        { "texture", profile.skyboxTexturePath }
    };
    root["postEffect"] = {
        { "bloomEnabled", post.bloomEnabled },
        { "bloomQuality", post.bloomQuality },
        { "threshold", post.threshold },
        { "bloomIntensity", post.bloomIntensity },
        { "spread", post.spread },
        { "enableToneMapping", post.enableToneMapping },
        { "vignetteIntensity", post.vignetteIntensity },
        { "vignettePower", post.vignettePower },
        { "chromaticAberration", post.chromaticAberration },
        { "filmGrainIntensity", post.filmGrainIntensity },
        { "lutIntensity", post.lutIntensity },
        { "lutTexture", post.lutTexturePath },
        { "colorExposure", post.colorExposure },
        { "colorContrast", post.colorContrast },
        { "colorSaturation", post.colorSaturation },
        { "colorTemperature", post.colorTemperature },
        { "colorTint", post.colorTint }
    };

    const std::filesystem::path path(filename);
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file << root.dump(2);
    return file.good();
}

bool LightManager::LoadEnvironmentProfile(
    const std::string& filename,
    EnvironmentProfileState& profile) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return false;
    }

    try {
        json root;
        file >> root;
        profile = CaptureEnvironmentProfile();

        const auto readVector3 = [](const json& value, Vector3& destination) {
            if (value.is_array() && value.size() >= 3) {
                destination = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            }
        };
        const auto readVector4 = [](const json& value, Vector4& destination) {
            if (value.is_array() && value.size() >= 4) {
                destination = {
                    value[0].get<float>(), value[1].get<float>(),
                    value[2].get<float>(), value[3].get<float>()
                };
            }
        };

        if (root.contains("directionalLight") && root["directionalLight"].is_object()) {
            const json& source = root["directionalLight"];
            DirectionalLight& light = profile.directionalLight;
            if (source.contains("color")) readVector4(source["color"], light.color);
            if (source.contains("direction")) readVector3(source["direction"], light.direction);
            light.intensity = source.value("intensity", light.intensity);
            if (source.contains("ambientColor")) readVector3(source["ambientColor"], light.ambientColor);
            light.fogStart = source.value("fogStart", light.fogStart);
            light.fogEnd = source.value("fogEnd", light.fogEnd);
            if (source.contains("fogColor")) readVector3(source["fogColor"], light.fogColor);
            light.fogHeightMin = source.value("fogHeightMin", light.fogHeightMin);
            light.fogHeightMax = source.value("fogHeightMax", light.fogHeightMax);
            light.volumetricIntensity = source.value("volumetricIntensity", light.volumetricIntensity);
            light.volumetricSteps = source.value("volumetricSteps", light.volumetricSteps);
            light.enableFog = source.value("enableFog", light.enableFog);
        }
        if (root.contains("clearColor")) {
            readVector4(root["clearColor"], profile.clearColor);
        }
        if (root.contains("shadow") && root["shadow"].is_object()) {
            const json& source = root["shadow"];
            profile.shadowMapResolution = source.value("resolution", profile.shadowMapResolution);
            profile.shadowAreaSize = source.value("areaSize", profile.shadowAreaSize);
        }
        if (root.contains("skybox") && root["skybox"].is_object()) {
            const json& source = root["skybox"];
            profile.skyboxEnabled = source.value("enabled", profile.skyboxEnabled);
            profile.skyboxTexturePath = source.value("texture", profile.skyboxTexturePath);
        }
        if (root.contains("postEffect") && root["postEffect"].is_object()) {
            const json& source = root["postEffect"];
            EnvironmentPostSettings& post = profile.post;
            post.bloomEnabled = source.value("bloomEnabled", post.bloomEnabled);
            post.bloomQuality = source.value("bloomQuality", post.bloomQuality);
            post.threshold = source.value("threshold", post.threshold);
            post.bloomIntensity = source.value("bloomIntensity", post.bloomIntensity);
            post.spread = source.value("spread", post.spread);
            post.enableToneMapping = source.value("enableToneMapping", post.enableToneMapping);
            post.vignetteIntensity = source.value("vignetteIntensity", post.vignetteIntensity);
            post.vignettePower = source.value("vignettePower", post.vignettePower);
            post.chromaticAberration = source.value("chromaticAberration", post.chromaticAberration);
            post.filmGrainIntensity = source.value("filmGrainIntensity", post.filmGrainIntensity);
            post.lutIntensity = source.value("lutIntensity", post.lutIntensity);
            post.lutTexturePath = source.value("lutTexture", post.lutTexturePath);
            post.colorExposure = source.value("colorExposure", post.colorExposure);
            post.colorContrast = source.value("colorContrast", post.colorContrast);
            post.colorSaturation = source.value("colorSaturation", post.colorSaturation);
            post.colorTemperature = source.value("colorTemperature", post.colorTemperature);
            post.colorTint = source.value("colorTint", post.colorTint);
        }

        profile.shadowMapResolution =
            profile.shadowMapResolution <= 1024 ? 1024 :
            (profile.shadowMapResolution <= 2048 ? 2048 : 4096);
        profile.shadowAreaSize = std::clamp(profile.shadowAreaSize, 20.0f, 240.0f);
        profile.post.bloomQuality = std::clamp(profile.post.bloomQuality, 0, 2);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool LightManager::ApplyEnvironmentProfile(const std::string& filename, float blendDuration) {
    EnvironmentProfileState profile;
    if (!LoadEnvironmentProfile(filename, profile)) {
        return false;
    }
    ApplyEnvironmentProfile(profile, blendDuration);
    currentEnvironmentProfileFile_ = filename;
    return true;
}

void LightManager::ApplyEnvironmentProfile(
    const EnvironmentProfileState& profile,
    float blendDuration) {
    currentEnvironmentProfileFile_.clear();
    environmentBlendFrom_ = CaptureEnvironmentProfile();
    environmentBlendTo_ = profile;
    environmentBlendElapsed_ = 0.0f;
    environmentBlendDuration_ = (std::max)(0.0f, blendDuration);
    environmentBlendActive_ = environmentBlendDuration_ > 0.0001f;
    environmentBlendDiscreteApplied_ = false;

    if (!environmentBlendActive_) {
        directionalLightData_ = profile.directionalLight;
        sceneClearColor_ = profile.clearColor;
        SetShadowMapResolution(profile.shadowMapResolution);
        SetShadowAreaSize(profile.shadowAreaSize);
        skyboxEnabled_ = profile.skyboxEnabled;
        if (!profile.skyboxTexturePath.empty()) {
            SetSkyboxTexturePath(profile.skyboxTexturePath);
        }
        ApplySceneClearColor();

        PostEffect* postEffect = PostEffect::GetInstance();
        if (PostEffect::Params* params = postEffect->GetParams()) {
            const EnvironmentPostSettings& post = profile.post;
            postEffect->SetBloomEnabled(post.bloomEnabled);
            postEffect->SetBloomQuality(static_cast<PostEffect::BloomQuality>(post.bloomQuality));
            if (!post.lutTexturePath.empty()) {
                postEffect->SetLUTTexturePath(post.lutTexturePath);
            }
            params->threshold = post.threshold;
            params->bloomIntensity = post.bloomIntensity;
            params->spread = post.spread;
            params->enableToneMapping = post.enableToneMapping;
            params->vignetteIntensity = post.vignetteIntensity;
            params->vignettePower = post.vignettePower;
            params->chromaticAberration = post.chromaticAberration;
            params->filmGrainIntensity = post.filmGrainIntensity;
            params->lutIntensity = post.lutIntensity;
            params->colorExposure = post.colorExposure;
            params->colorContrast = post.colorContrast;
            params->colorSaturation = post.colorSaturation;
            params->colorTemperature = post.colorTemperature;
            params->colorTint = post.colorTint;
        }
    }
}

void LightManager::UpdateEnvironmentProfile(float deltaTime) {
    if (!environmentBlendActive_) {
        return;
    }

    environmentBlendElapsed_ += (std::max)(0.0f, deltaTime);
    const float linearT = std::clamp(
        environmentBlendElapsed_ / (std::max)(environmentBlendDuration_, 0.0001f),
        0.0f,
        1.0f);
    const float t = linearT * linearT * (3.0f - 2.0f * linearT);
    const auto lerp = [t](float from, float to) { return from + (to - from) * t; };
    const auto lerp3 = [&](const Vector3& from, const Vector3& to) {
        return Vector3{ lerp(from.x, to.x), lerp(from.y, to.y), lerp(from.z, to.z) };
    };
    const auto lerp4 = [&](const Vector4& from, const Vector4& to) {
        return Vector4{ lerp(from.x, to.x), lerp(from.y, to.y), lerp(from.z, to.z), lerp(from.w, to.w) };
    };

    const DirectionalLight& fromLight = environmentBlendFrom_.directionalLight;
    const DirectionalLight& toLight = environmentBlendTo_.directionalLight;
    directionalLightData_.color = lerp4(fromLight.color, toLight.color);
    directionalLightData_.direction = lerp3(fromLight.direction, toLight.direction);
    if (Math::Length(directionalLightData_.direction) > 0.0001f) {
        directionalLightData_.direction = Math::Normalize(directionalLightData_.direction);
    }
    directionalLightData_.intensity = lerp(fromLight.intensity, toLight.intensity);
    directionalLightData_.ambientColor = lerp3(fromLight.ambientColor, toLight.ambientColor);
    directionalLightData_.fogStart = lerp(fromLight.fogStart, toLight.fogStart);
    directionalLightData_.fogEnd = lerp(fromLight.fogEnd, toLight.fogEnd);
    directionalLightData_.fogColor = lerp3(fromLight.fogColor, toLight.fogColor);
    directionalLightData_.fogHeightMin = lerp(fromLight.fogHeightMin, toLight.fogHeightMin);
    directionalLightData_.fogHeightMax = lerp(fromLight.fogHeightMax, toLight.fogHeightMax);
    directionalLightData_.volumetricIntensity =
        lerp(fromLight.volumetricIntensity, toLight.volumetricIntensity);
    directionalLightData_.volumetricSteps = linearT < 0.5f
        ? fromLight.volumetricSteps
        : toLight.volumetricSteps;
    directionalLightData_.enableFog = linearT < 0.5f ? fromLight.enableFog : toLight.enableFog;

    sceneClearColor_ = lerp4(environmentBlendFrom_.clearColor, environmentBlendTo_.clearColor);
    shadowAreaSize_ = lerp(environmentBlendFrom_.shadowAreaSize, environmentBlendTo_.shadowAreaSize);
    ApplySceneClearColor();

    PostEffect* postEffect = PostEffect::GetInstance();
    if (PostEffect::Params* params = postEffect->GetParams()) {
        const EnvironmentPostSettings& from = environmentBlendFrom_.post;
        const EnvironmentPostSettings& to = environmentBlendTo_.post;
        params->threshold = lerp(from.threshold, to.threshold);
        params->bloomIntensity = lerp(from.bloomIntensity, to.bloomIntensity);
        params->spread = lerp(from.spread, to.spread);
        params->vignetteIntensity = lerp(from.vignetteIntensity, to.vignetteIntensity);
        params->vignettePower = lerp(from.vignettePower, to.vignettePower);
        params->chromaticAberration = lerp(from.chromaticAberration, to.chromaticAberration);
        params->filmGrainIntensity = lerp(from.filmGrainIntensity, to.filmGrainIntensity);
        params->lutIntensity = lerp(from.lutIntensity, to.lutIntensity);
        params->colorExposure = lerp(from.colorExposure, to.colorExposure);
        params->colorContrast = lerp(from.colorContrast, to.colorContrast);
        params->colorSaturation = lerp(from.colorSaturation, to.colorSaturation);
        params->colorTemperature = lerp(from.colorTemperature, to.colorTemperature);
        params->colorTint = lerp(from.colorTint, to.colorTint);
    }

    if (!environmentBlendDiscreteApplied_ && linearT >= 0.5f) {
        environmentBlendDiscreteApplied_ = true;
        skyboxEnabled_ = environmentBlendTo_.skyboxEnabled;
        if (!environmentBlendTo_.skyboxTexturePath.empty()) {
            SetSkyboxTexturePath(environmentBlendTo_.skyboxTexturePath);
        }
        SetShadowMapResolution(environmentBlendTo_.shadowMapResolution);
        if (!environmentBlendTo_.post.lutTexturePath.empty()) {
            postEffect->SetLUTTexturePath(environmentBlendTo_.post.lutTexturePath);
        }
        postEffect->SetBloomEnabled(environmentBlendTo_.post.bloomEnabled);
        postEffect->SetBloomQuality(
            static_cast<PostEffect::BloomQuality>(environmentBlendTo_.post.bloomQuality));
        if (PostEffect::Params* params = postEffect->GetParams()) {
            params->enableToneMapping = environmentBlendTo_.post.enableToneMapping;
        }
    }

    if (linearT >= 1.0f) {
        const std::string completedProfileFile =
            currentEnvironmentProfileFile_;
        ApplyEnvironmentProfile(environmentBlendTo_, 0.0f);
        currentEnvironmentProfileFile_ = completedProfileFile;
    }
}

float LightManager::GetEnvironmentProfileBlendProgress() const {
    if (!environmentBlendActive_) {
        return 1.0f;
    }
    return std::clamp(
        environmentBlendElapsed_ / (std::max)(environmentBlendDuration_, 0.0001f),
        0.0f,
        1.0f);
}
