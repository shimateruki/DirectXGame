#include "LightManager.h"
#include "Object3d.h" // 追従対象の座標を取得するために必要
#include <cassert>
#include <fstream>
#include <cmath>      // sin, cos
#include "json.hpp"
#include <Camera.h>
#include <CameraManager.h>

using json = nlohmann::json;

LightManager* LightManager::GetInstance() {
    static LightManager instance;
    return &instance;
}

void LightManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    uint32_t envHandle = TextureManager::GetInstance()->Load("Resources/rostock_laage_airport_4k.dds");
    LightManager::GetInstance()->SetEnvironmentMapHandle(envHandle);

    // --- バッファ作成 (サイズはConstData構造体に合わせる) ---
    pointLightResource_ = dxCommon_->CreateBufferResource(sizeof(PointLightConstData));
    pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightConstData_));
    pointLightConstData_->activeCount = 0;

    spotLightResource_ = dxCommon_->CreateBufferResource(sizeof(SpotLightConstData));
    spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightConstData_));
    spotLightConstData_->activeCount = 0;

    directionalLightResource_ = dxCommon_->CreateBufferResource(sizeof(DirectionalLight));

    // 平行光源の初期設定
    directionalLightData_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_.direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_.intensity = 1.0f;
    directionalLightData_.ambientColor = { 0.1f, 0.1f, 0.1f };
    directionalLightData_.fogStart = 10.0f;
    directionalLightData_.fogEnd = 1000.0f;
    directionalLightData_.fogColor = { 0.5f, 0.5f, 0.5f };

    pointLights_.clear();
    spotLights_.clear();
}


void LightManager::Update() {

    // ---------------------------------------------------
      // 1. 平行光源 (Directional Light)
      // ---------------------------------------------------
    if (directionalLightResource_) {
        DirectionalLight* dirMap = nullptr;

        // 1. Mapを実行し、かつ成功(SUCCEEDED)したか、ポインタがNULLでないかを確認
        if (SUCCEEDED(directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&dirMap))) && dirMap) {

            // 2. Normalizeする前に、ベクトルの長さが0でないかチェック(0除算防止)
            if (Math::Length(directionalLightData_.direction) > 0.0001f) {
                directionalLightData_.direction = Math::Normalize(directionalLightData_.direction);
            }
            else {
                // 長さが0ならデフォルトの向きをセット
                directionalLightData_.direction = { 0.0f, -1.0f, 0.0f };
            }

            // =======================================================
            //  シャドウマップ用のライトVP行列計算
            // =======================================================
            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            if (camera) {
                Vector3 target = camera->GetEye();

                // 太陽の位置を、カメラから光の逆方向へ離す
                Vector3 lightPos = {
                    target.x - directionalLightData_.direction.x * 200.0f,
                    target.y - directionalLightData_.direction.y * 200.0f,
                    target.z - directionalLightData_.direction.z * 200.0f
                };

                Vector3 up = { 0.0f, 1.0f, 0.0f };
                // 真上・真下を向いている時の特異点対策
                if (std::abs(directionalLightData_.direction.x) < 0.001f && std::abs(directionalLightData_.direction.z) < 0.001f) {
                    up = { 0.0f, 0.0f, 1.0f };
                }

                // 太陽目線のビュー行列と正投影行列を計算（Math::を付けて静的関数として呼び出し）
                Matrix4x4 lightView = Math::MakeLookAtMatrix(lightPos, target, up);
                Matrix4x4 lightProj = Math::MakeOrthographicMatrix(80.0f, 80.0f, 1.0f, 400.0f);

                // 計算結果を構造体に保存（この後すぐ下の行でGPUへ送られます）
                directionalLightData_.lightViewProj = Math::Multiply(lightView, lightProj);
            }
            // =======================================================

            // 3. 安全が確認できたので書き込む
            *dirMap = directionalLightData_;

            // 4. アンマップ
            directionalLightResource_->Unmap(0, nullptr);
        }
        else {
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

        // --- GPUバッファへコピー ---
        int index = pointLightConstData_->activeCount;
        pointLightConstData_->lights[index] = instance.data;
        pointLightConstData_->activeCount++;
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

        // --- GPUバッファへコピー ---
        int index = spotLightConstData_->activeCount;
        spotLightConstData_->lights[index] = instance.data;
        spotLightConstData_->activeCount++;
    }
}

// 戻り値を Instance* に変更
LightManager::PointLightInstance* LightManager::AddPointLight() {
    if (pointLights_.size() >= kMaxPointLights) return nullptr;

    PointLightInstance instance;
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
}

void LightManager::SaveState(const std::string& filename) {
    json root;

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

    std::ofstream file(filename);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
    }
}

void LightManager::LoadState(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    json root;
    try { file >> root; }
    catch (...) { return; }

    ClearAllLights();

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
}