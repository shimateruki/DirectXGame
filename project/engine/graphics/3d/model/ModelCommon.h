#pragma once

#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

// 担当：モデルで共有するDirectX基盤とCompute Skinning用パイプラインを保持する。
// 関連：ModelManager（共有）、Model（スキニング実行）、DirectXCommon
// 主な入口：Initialize / IsComputeSkinningAvailable
class ModelCommon {
public:
    /// <summary>
    /// DirectX基盤とCompute Skinning用パイプラインを初期化します。
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    DirectXCommon* GetDxCommon() const { return dxCommon_; }

    /// <summary>Compute Shaderによるスキニングが使えるか返す。</summary>
    bool IsComputeSkinningAvailable() const {
        return computeSkinningRootSignature_ && computeSkinningPipelineState_;
    }

    /// <summary>Compute Skinning用ルートシグネチャを返す。</summary>
    ID3D12RootSignature* GetComputeSkinningRootSignature() const {
        return computeSkinningRootSignature_.Get();
    }

    /// <summary>Compute Skinning用パイプラインを返す。</summary>
    ID3D12PipelineState* GetComputeSkinningPipelineState() const {
        return computeSkinningPipelineState_.Get();
    }

private:
    /// <summary>Compute Skinning用のルートシグネチャとパイプラインを作る。</summary>
    void CreateComputeSkinningPipeline();

    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeSkinningRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computeSkinningPipelineState_;
};
