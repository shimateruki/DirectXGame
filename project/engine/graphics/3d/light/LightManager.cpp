#include "LightManager.h"
#include <cassert>
#include <fstream>
#include "json.hpp" // jsonライブラリ

using json = nlohmann::json;

LightManager* LightManager::GetInstance() {
    static LightManager instance;
    return &instance;
}

void LightManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;

    // --- 点光源バッファの作成 ---
    // サイズは「構造体配列 + 個数」
    pointLightResource_ = dxCommon_->CreateBufferResource(sizeof(PointLightConstData));
    pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightConstData_));
    pointLightConstData_->activeCount = 0;

    // --- スポットライトバッファの作成 ---
    spotLightResource_ = dxCommon_->CreateBufferResource(sizeof(SpotLightConstData));
    spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightConstData_));
    spotLightConstData_->activeCount = 0;

    // 初期状態は空にしておく
    pointLights_.clear();
    spotLights_.clear();
}

void LightManager::Update() {
    // 1. 点光源のデータをGPUバッファにコピー
    pointLightConstData_->activeCount = static_cast<int>(pointLights_.size());
    // 最大数を超えないように制限
    if (pointLightConstData_->activeCount > kMaxPointLights) {
        pointLightConstData_->activeCount = kMaxPointLights;
    }

    for (int i = 0; i < pointLightConstData_->activeCount; ++i) {
        pointLightConstData_->lights[i] = pointLights_[i];
    }

    // 2. スポットライトのデータをGPUバッファにコピー
    spotLightConstData_->activeCount = static_cast<int>(spotLights_.size());
    if (spotLightConstData_->activeCount > kMaxSpotLights) {
        spotLightConstData_->activeCount = kMaxSpotLights;
    }

    for (int i = 0; i < spotLightConstData_->activeCount; ++i) {
        spotLightConstData_->lights[i] = spotLights_[i];
    }
}


MeshRenderer::PointLight* LightManager::AddPointLight() {
    if (pointLights_.size() >= kMaxPointLights) {
        return nullptr; // 満杯
    }

    MeshRenderer::PointLight light;
    light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    light.position = { 0.0f, 2.0f, 0.0f };
    light.intensity = 1.0f;
    light.radius = 10.0f;
    light.decay = 1.0f;

    pointLights_.push_back(light);
    return &pointLights_.back();
}

// ★修正: 返り値の型を MeshRenderer::SpotLight* に変更
MeshRenderer::SpotLight* LightManager::AddSpotLight() {
    if (spotLights_.size() >= kMaxSpotLights) {
        return nullptr;
    }

    MeshRenderer::SpotLight light;
    light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    light.position = { 0.0f, 5.0f, 0.0f };
    light.direction = { 0.0f, -1.0f, 0.0f }; // 真下
    light.intensity = 2.0f;
    light.distance = 20.0f;
    light.decay = 1.0f;
    light.cosAngle = 0.9f;        // 約25度
    light.cosFalloffStart = 0.95f; // 少し内側

    spotLights_.push_back(light);
    return &spotLights_.back();
}

void LightManager::ClearAllLights() {
    pointLights_.clear();
    spotLights_.clear();
}

// 保存機能
void LightManager::SaveState(const std::string& filename) {
    json root;

    // --- 点光源 ---
    json pArray = json::array();
    for (const auto& l : pointLights_) {
        json j;
        j["position"] = { l.position.x, l.position.y, l.position.z };
        j["color"] = { l.color.x, l.color.y, l.color.z, l.color.w };
        j["intensity"] = l.intensity;
        j["radius"] = l.radius;
        j["decay"] = l.decay;
        pArray.push_back(j);
    }
    root["pointLights"] = pArray;

    // --- スポットライト ---
    json sArray = json::array();
    for (const auto& l : spotLights_) {
        json j;
        j["position"] = { l.position.x, l.position.y, l.position.z };
        j["direction"] = { l.direction.x, l.direction.y, l.direction.z };
        j["color"] = { l.color.x, l.color.y, l.color.z, l.color.w };
        j["intensity"] = l.intensity;
        j["distance"] = l.distance;
        j["decay"] = l.decay;
        j["cosAngle"] = l.cosAngle;
        j["cosFalloffStart"] = l.cosFalloffStart;
        sArray.push_back(j);
    }
    root["spotLights"] = sArray;

    // 書き出し
    std::ofstream file(filename);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
    }
}

//  読み込み機能
void LightManager::LoadState(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return; // ファイルがなければ何もしない

    json root;
    try {
        file >> root;
    }
    catch (...) {
        return;
    }

    //  シーン切り替え用に、読み込む前に現在のライトを全消去する
    ClearAllLights();

    // --- 点光源読み込み ---
    if (root.contains("pointLights") && root["pointLights"].is_array()) {
        for (const auto& j : root["pointLights"]) {
            auto l = AddPointLight();
            if (l) {
                if (j.contains("position")) { l->position.x = j["position"][0]; l->position.y = j["position"][1]; l->position.z = j["position"][2]; }
                if (j.contains("color")) { l->color.x = j["color"][0]; l->color.y = j["color"][1]; l->color.z = j["color"][2]; l->color.w = j["color"][3]; }
                if (j.contains("intensity")) l->intensity = j["intensity"];
                if (j.contains("radius")) l->radius = j["radius"];
                if (j.contains("decay")) l->decay = j["decay"];
            }
        }
    }

    // --- スポットライト読み込み ---
    if (root.contains("spotLights") && root["spotLights"].is_array()) {
        for (const auto& j : root["spotLights"]) {
            auto l = AddSpotLight();
            if (l) {
                if (j.contains("position")) { l->position.x = j["position"][0]; l->position.y = j["position"][1]; l->position.z = j["position"][2]; }
                if (j.contains("direction")) { l->direction.x = j["direction"][0]; l->direction.y = j["direction"][1]; l->direction.z = j["direction"][2]; }
                if (j.contains("color")) { l->color.x = j["color"][0]; l->color.y = j["color"][1]; l->color.z = j["color"][2]; l->color.w = j["color"][3]; }
                if (j.contains("intensity")) l->intensity = j["intensity"];
                if (j.contains("distance")) l->distance = j["distance"];
                if (j.contains("decay")) l->decay = j["decay"];
                if (j.contains("cosAngle")) l->cosAngle = j["cosAngle"];
                if (j.contains("cosFalloffStart")) l->cosFalloffStart = j["cosFalloffStart"];
            }
        }
    }
}