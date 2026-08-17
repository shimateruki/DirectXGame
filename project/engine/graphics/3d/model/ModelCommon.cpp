#include "ModelCommon.h"

#include "DirectXCommon.h"
#include "RootSignatureBuilder.h"

#include <cassert>

void ModelCommon::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    CreateComputeSkinningPipeline();
}

void ModelCommon::CreateComputeSkinningPipeline() {
    computeSkinningRootSignature_.Reset();
    computeSkinningPipelineState_.Reset();

    RootSignatureBuilder rootSignatureBuilder;
    rootSignatureBuilder.AddSRV(0);
    rootSignatureBuilder.AddSRV(1);
    rootSignatureBuilder.AddUAV(0);
    // Root Descriptorは要素数を保持しないため、頂点数とボーン数を明示的に渡す。
    rootSignatureBuilder.AddConstants(0, 2);
    rootSignatureBuilder.Build(
        dxCommon_->GetDevice(),
        computeSkinningRootSignature_.GetAddressOf(),
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    auto computeShader = dxCommon_->CompileShader(
        L"Resources/shader/object3d/Skinning.CS.hlsl",
        L"cs_6_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = computeSkinningRootSignature_.Get();
    pipelineDesc.CS = {
        computeShader->GetBufferPointer(),
        computeShader->GetBufferSize()
    };

    const HRESULT result = dxCommon_->GetDevice()->CreateComputePipelineState(
        &pipelineDesc,
        IID_PPV_ARGS(computeSkinningPipelineState_.GetAddressOf()));
    assert(SUCCEEDED(result));
}
