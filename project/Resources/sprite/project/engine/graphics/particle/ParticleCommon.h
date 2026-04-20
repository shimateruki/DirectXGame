#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <array>
class DirectXCommon;

enum class ParticleBlendMode {
    kAlpha,     // 通常 (Alpha Blend)
    kAdd,       // 加算 (Additive)
    kSubtract,  // 減算 (Subtract)
    kMultiply,  // 乗算 (Multiply)
    kScreen,    // スクリーン (Screen)
    kCount      // 最大数
};
/// <summary>
/// パーティクル描画の共通処理
/// </summary>
class ParticleCommon {
public:

    void Initialize(DirectXCommon* dxCommon);
    DirectXCommon* GetDxCommon() const { return dxCommon_; }
    void SetPipeline(ID3D12GraphicsCommandList* commandList, ParticleBlendMode mode = ParticleBlendMode::kAlpha);
private:
    void CreateRootSignature();
    void CreatePipeline();

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, (int)ParticleBlendMode::kCount> graphicsPipelines_;
};
