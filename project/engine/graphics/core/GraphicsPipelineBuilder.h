#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "Object3dCommon.h"

// PSO (Pipeline State Object) の設定を段階的に組み立てるヘルパークラス
class GraphicsPipelineBuilder {
public:
    GraphicsPipelineBuilder();
    ~GraphicsPipelineBuilder() = default;

    // 基本設定
    void SetRootSignature(ID3D12RootSignature* rootSignature);
    void SetInputLayout(const D3D12_INPUT_ELEMENT_DESC* inputElements, UINT count);
    void SetShaders(IDxcBlob* vsBlob, IDxcBlob* psBlob); // psBlob は影描画などで nullptr にできる。
    void SetTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType);

    // ラスタライザ設定
    void SetRasterizerState(D3D12_CULL_MODE cullMode, D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID);
    void SetDepthBias(INT depthBias, FLOAT depthBiasClamp, FLOAT slopeScaledDepthBias);

    // 深度設定
    void SetDepthStencilState(bool depthEnable, D3D12_DEPTH_WRITE_MASK writeMask = D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC func = D3D12_COMPARISON_FUNC_LESS_EQUAL);

    // ブレンド設定
    void SetBlendMode(BlendMode mode);

    // レンダーターゲット設定
    void SetRenderTargets(UINT numRenderTargets, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat);

    // PSO を生成する
    void Build(ID3D12Device* device, ID3D12PipelineState** outPipelineState);

private:
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc_{};
};
