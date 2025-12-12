#include "LightManager.h"
#include <cassert>

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

Object3d::PointLight* LightManager::AddPointLight() {
    if (pointLights_.size() >= kMaxPointLights) {
        return nullptr; // 満杯
    }

    // デフォルト値で作成
    Object3d::PointLight light;
    light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    light.position = { 0.0f, 2.0f, 0.0f };
    light.intensity = 1.0f;
    light.radius = 10.0f;
    light.decay = 1.0f;

    pointLights_.push_back(light);
    return &pointLights_.back();
}

Object3d::SpotLight* LightManager::AddSpotLight() {
    if (spotLights_.size() >= kMaxSpotLights) {
        return nullptr;
    }

    Object3d::SpotLight light;
    light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    light.position = { 0.0f, 5.0f, 0.0f };
    light.direction = { 0.0f, -1.0f, 0.0f }; // 真下
    light.intensity = 2.0f;
    light.distance = 20.0f;
    light.decay = 1.0f;
    light.cosAngle = 0.9f;       // 約25度
    light.cosFalloffStart = 0.95f; // 少し内側

    spotLights_.push_back(light);
    return &spotLights_.back();
}

void LightManager::ClearAllLights() {
    pointLights_.clear();
    spotLights_.clear();
}