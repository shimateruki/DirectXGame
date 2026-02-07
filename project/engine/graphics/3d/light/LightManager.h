#pragma once
#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include <vector>
#include <wrl.h>
#include <string>

class LightManager {
public:
    static const int kMaxPointLights = 100;
    static const int kMaxSpotLights = 100;

    struct PointLightConstData {
        MeshRenderer::PointLight lights[kMaxPointLights];
        int activeCount;
        float padding[3];
    };

    struct SpotLightConstData {
        MeshRenderer::SpotLight lights[kMaxSpotLights];
        int activeCount;
        float padding[3];
    };

    static LightManager* GetInstance();
    void Initialize(DirectXCommon* dxCommon);
    void Update();

    ID3D12Resource* GetPointLightResource() { return pointLightResource_.Get(); }
    ID3D12Resource* GetSpotLightResource() { return spotLightResource_.Get(); }

    MeshRenderer::PointLight* AddPointLight();
    MeshRenderer::SpotLight* AddSpotLight();

    void ClearAllLights();

    std::vector<MeshRenderer::PointLight>& GetPointLights() { return pointLights_; }
    std::vector<MeshRenderer::SpotLight>& GetSpotLights() { return spotLights_; }

    void SaveState(const std::string& filename);
    void LoadState(const std::string& filename);

private:
    LightManager() = default;
    ~LightManager() = default;
    LightManager(const LightManager&) = delete;
    const LightManager& operator=(const LightManager&) = delete;

    DirectXCommon* dxCommon_ = nullptr;

    std::vector<MeshRenderer::PointLight> pointLights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLightConstData* pointLightConstData_ = nullptr;

    std::vector<MeshRenderer::SpotLight> spotLights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    SpotLightConstData* spotLightConstData_ = nullptr;
};