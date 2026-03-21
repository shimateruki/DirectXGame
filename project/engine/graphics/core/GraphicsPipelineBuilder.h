#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "Object3dCommon.h" // BlendModeの定義を使うため

// PSO (パイプラインステートオブジェクト) の構築を劇的に簡単にするクラス
class GraphicsPipelineBuilder {
public:
    GraphicsPipelineBuilder();
    ~GraphicsPipelineBuilder() = default;

    // 基本設定
    void SetRootSignature(ID3D12RootSignature* rootSignature);
    void SetInputLayout(const D3D12_INPUT_ELEMENT_DESC* inputElements, UINT count);
    void SetShaders(IDxcBlob* vsBlob, IDxcBlob* psBlob); // PSは nullptr にすると影用(深度のみ)になります
    void SetTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType);

    // ラスタライザ（カリングと塗りつぶし）
    void SetRasterizerState(D3D12_CULL_MODE cullMode, D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID);
    void SetDepthBias(INT depthBias, FLOAT depthBiasClamp, FLOAT slopeScaledDepthBias); // 影(シャドウアクネ対策)用

    // 深度(Zバッファ)設定
    void SetDepthStencilState(bool depthEnable, D3D12_DEPTH_WRITE_MASK writeMask = D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC func = D3D12_COMPARISON_FUNC_LESS_EQUAL);

    // ブレンド(半透明・加算など)設定
    void SetBlendMode(BlendMode mode);

    // レンダーターゲット(書き込み先)設定
    void SetRenderTargets(UINT numRenderTargets, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat);

    // ビルド実行
    void Build(ID3D12Device* device, ID3D12PipelineState** outPipelineState);

private:
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc_{};
};