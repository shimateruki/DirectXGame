#pragma once
#include "Object3d.h" // PointLight, SpotLightの構造体定義を使うため
#include "DirectXCommon.h"
#include <vector>
#include <wrl.h>

class LightManager {
public:
    // 定数: ライトの最大数
    static const int kMaxPointLights = 100;
    static const int kMaxSpotLights = 100;

    // GPUに送るための構造体 (配列版)
    struct PointLightConstData {
        Object3d::PointLight lights[kMaxPointLights];
        int activeCount; // 現在有効なライトの数
        float padding[3];
    };

    struct SpotLightConstData {
        Object3d::SpotLight lights[kMaxSpotLights];
        int activeCount;
        float padding[3];
    };

    static LightManager* GetInstance();
    void Initialize(DirectXCommon* dxCommon);

    // 毎フレーム呼ぶ
    void Update();

    // 描画時にリソースを取得する
    ID3D12Resource* GetPointLightResource() { return pointLightResource_.Get(); }
    ID3D12Resource* GetSpotLightResource() { return spotLightResource_.Get(); }

    // --- ライト操作用 ---

    // 点光源を追加して、その参照を返す (設定変更用)
    Object3d::PointLight* AddPointLight();
    // スポットライトを追加
    Object3d::SpotLight* AddSpotLight();

    // 全削除 (シーン切り替え時など)
    void ClearAllLights();

    // リストへの直接アクセス (エディタ用)
    std::vector<Object3d::PointLight>& GetPointLights() { return pointLights_; }
    std::vector<Object3d::SpotLight>& GetSpotLights() { return spotLights_; }

private:
    LightManager() = default;
    ~LightManager() = default;
    LightManager(const LightManager&) = delete;
    const LightManager& operator=(const LightManager&) = delete;

    DirectXCommon* dxCommon_ = nullptr;

    // --- 点光源 ---
    std::vector<Object3d::PointLight> pointLights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLightConstData* pointLightConstData_ = nullptr;

    // --- スポットライト ---
    std::vector<Object3d::SpotLight> spotLights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    SpotLightConstData* spotLightConstData_ = nullptr;
};