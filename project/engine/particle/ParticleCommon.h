#pragma once
#include <wrl.h>
#include <d3d12.h>

class DirectXCommon;

/// <summary>
/// パーティクル描画の共通処理
/// </summary>
class ParticleCommon {
public:
    void Initialize(DirectXCommon* dxCommon);
    void SetPipeline(ID3D12GraphicsCommandList* commandList);
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    void CreateRootSignature();
    void CreatePipeline();

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
};