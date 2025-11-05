#pragma once
#include "Light.h"
#include "engine/base/DirectXCommon.h"
#include <wrl.h>
#include <memory>

class LightManager {
public:
    static LightManager* GetInstance();

    void Initialize(DirectXCommon* dxCommon);
    void Update(); // 定数バッファを更新

    /// <summary>
    /// 描画コマンド（ルートパラメータ）を設定
    /// </summary>
    void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex);

    // --- ライトアクセサ ---
    DirectionalLight* GetDirectionalLight() { return &lightData_->directionalLight; }

    PointLight* GetPointLight(int index) {
        if (index < 0 || index >= kMaxPointLights) return nullptr;
        return &lightData_->pointLights[index];
    }

private:
    LightManager() = default;
    ~LightManager() = default;
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    // ★ LightGroup ではなく LightGroup* (ポインタ) に修正
    LightGroup* lightData_ = nullptr;
};