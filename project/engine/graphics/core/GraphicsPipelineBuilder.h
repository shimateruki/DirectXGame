#pragma once

#include <d3d12.h>
#include <vector>
#include <wrl.h>
#include "Object3dCommon.h"

/// <summary>D3D12 の描画パイプライン設定を段階的に組み立てます。</summary>
class GraphicsPipelineBuilder {
public:
    /// <summary>共通の初期値でパイプライン記述を作ります。</summary>
    GraphicsPipelineBuilder();
    ~GraphicsPipelineBuilder() = default;

    /// <summary>このパイプラインで使用するルートシグネチャを設定します。</summary>
    void SetRootSignature(ID3D12RootSignature* rootSignature);

    /// <summary>頂点入力レイアウトを設定します。</summary>
    /// <param name="inputElements">Build まで有効な入力要素配列。</param>
    /// <param name="count">入力要素数。</param>
    void SetInputLayout(const D3D12_INPUT_ELEMENT_DESC* inputElements, UINT count);

    /// <summary>コンパイル済みシェーダーのバイナリを設定します。</summary>
    /// <param name="vsBlob">頂点シェーダー。Build まで保持してください。</param>
    /// <param name="psBlob">ピクセルシェーダー。深度専用描画では nullptr を指定できます。</param>
    void SetShaders(IDxcBlob* vsBlob, IDxcBlob* psBlob);

    /// <summary>描画するプリミティブの種類を設定します。</summary>
    void SetTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType);

    /// <summary>面のカリング方式と塗りつぶし方式を設定します。</summary>
    void SetRasterizerState(D3D12_CULL_MODE cullMode, D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID);

    /// <summary>影描画などの深度バイアスを設定します。</summary>
    void SetDepthBias(INT depthBias, FLOAT depthBiasClamp, FLOAT slopeScaledDepthBias);

    /// <summary>深度テストと書き込み条件を設定します。</summary>
    void SetDepthStencilState(bool depthEnable, D3D12_DEPTH_WRITE_MASK writeMask = D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC func = D3D12_COMPARISON_FUNC_LESS_EQUAL);

    /// <summary>RTV のブレンド方式と、それに伴う深度書き込み設定を適用します。</summary>
    void SetBlendMode(BlendMode mode);

    /// <summary>描画先 RTV と深度 DSV のフォーマットを設定します。</summary>
    /// <param name="numRenderTargets">RTV の個数。</param>
    /// <param name="rtvFormats">個数分のフォーマット配列。</param>
    /// <param name="dsvFormat">深度バッファのフォーマット。</param>
    void SetRenderTargets(UINT numRenderTargets, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat);

    /// <summary>現在の設定からパイプラインステートを生成します。</summary>
    /// <param name="device">作成に使用するデバイス。</param>
    /// <param name="outPipelineState">生成したステートの受け取り先。</param>
    void Build(ID3D12Device* device, ID3D12PipelineState** outPipelineState);

private:
    // 入力レイアウトとシェーダーバイナリは非所有のポインタを保持します。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc_{};
};
