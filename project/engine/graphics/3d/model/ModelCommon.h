#pragma once

#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// 3Dモデル描画で共有するDirectX基盤と共通パイプラインを保持します。
/// </summary>
class ModelCommon {
public:
    /// <summary>
    /// DirectX基盤とCompute Skinning用パイプラインを初期化します。
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    DirectXCommon* GetDxCommon() const { return dxCommon_; }

    bool IsComputeSkinningAvailable() const {
        return computeSkinningRootSignature_ && computeSkinningPipelineState_;
    }
    ID3D12RootSignature* GetComputeSkinningRootSignature() const {
        return computeSkinningRootSignature_.Get();
    }
    ID3D12PipelineState* GetComputeSkinningPipelineState() const {
        return computeSkinningPipelineState_.Get();
    }

private:
    void CreateComputeSkinningPipeline();

    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeSkinningRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computeSkinningPipelineState_;
};
