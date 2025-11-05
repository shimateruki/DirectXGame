#include "LightManager.h"
#include <cassert>

LightManager* LightManager::GetInstance() {
    static LightManager instance;
    return &instance;
}

void LightManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);

    // ライト用の定数バッファを作成
    resource_ = dxCommon->CreateBufferResource(sizeof(LightGroup));

    // バッファをマップして、CPUからアクセスできるようにする
    HRESULT hr = resource_->Map(0, nullptr, reinterpret_cast<void**>(&lightData_));
    assert(SUCCEEDED(hr));

    // --- デフォルトのライト設定 ---

    // 平行光源
    lightData_->directionalLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    lightData_->directionalLight.direction = { 0.0f, -1.0f, 0.0f }; // 真上から
    lightData_->directionalLight.intensity = 1.0f;

    // 点光源 (すべて無効化)
    for (int i = 0; i < kMaxPointLights; ++i) {
        lightData_->pointLights[i].isActive = 0; // 無効
        lightData_->pointLights[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        lightData_->pointLights[i].position = { 0.0f, 0.0f, 0.0f };
        lightData_->pointLights[i].intensity = 1.0f;
        lightData_->pointLights[i].radius = 10.0f;
        lightData_->pointLights[i].decay = 1.0f;
    }
}

void LightManager::Update() {

}

void LightManager::SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex) {
    // 定数バッファのGPU仮想アドレスを取得
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = resource_->GetGPUVirtualAddress();
    // ルートパラメータにCBVを設定
    commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, gpuAddress);
}