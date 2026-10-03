#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <array>
class DirectXCommon;

/// <summary>CPUパーティクル描画で使う色合成方式。</summary>
enum class ParticleBlendMode {
    kAlpha,     // 通常 (Alpha Blend)
    kAdd,       // 加算 (Additive)
    kSubtract,  // 減算 (Subtract)
    kMultiply,  // 乗算 (Multiply)
    kScreen,    // スクリーン (Screen)
    kCount      // 最大数
};

/// <summary>CPUパーティクル描画用の共通ルートシグネチャと合成方式別パイプラインを管理する。</summary>
class ParticleCommon {
public:

    /// <summary>DirectX基盤を受け取り、CPUパーティクル描画パイプラインを作る。</summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>所有しないDirectX基盤への参照を返す。</summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

    /// <summary>指定した合成方式のパイプラインをコマンドリストに設定する。</summary>
    void SetPipeline(ID3D12GraphicsCommandList* commandList, ParticleBlendMode mode = ParticleBlendMode::kAlpha);
private:
    // CPUパーティクル描画で使うルートシグネチャと合成方式別PipelineStateを作る。
    void CreateRootSignature();
    void CreatePipeline();

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, (int)ParticleBlendMode::kCount> graphicsPipelines_;
};
