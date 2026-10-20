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
namespace engine {

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
    // 初期化時に受け取る描画基盤への非所有参照。
    DirectXCommon* dxCommon_ = nullptr;

    // 全ブレンド方式で共有するリソース結合定義。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

    // 旧単一PSOの保持枠。実際のブレンド切替は下の配列で行う。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;

    // ParticleBlendModeの値を添字として選ぶ描画PSO。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, (int)ParticleBlendMode::kCount> graphicsPipelines_;
};

} // namespace engine
