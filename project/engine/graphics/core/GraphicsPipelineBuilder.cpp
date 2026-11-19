#include "GraphicsPipelineBuilder.h"
#include <cassert>
// PSO作成時に共通で使う安全な初期値を設定する。

GraphicsPipelineBuilder::GraphicsPipelineBuilder() {
    // どのPSOでも共通する「暗黙のデフォルト値」をセットしておく
    desc_.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc_.SampleDesc.Count = 1;
    desc_.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc_.RasterizerState.DepthClipEnable = TRUE;
}

void GraphicsPipelineBuilder::SetRootSignature(ID3D12RootSignature* rootSignature) {
    // このPSOで使用するルートシグネチャを設定する。
    desc_.pRootSignature = rootSignature;
}

void GraphicsPipelineBuilder::SetInputLayout(const D3D12_INPUT_ELEMENT_DESC* inputElements, UINT count) {
    // 頂点入力レイアウトを設定し、シェーダー入力と頂点バッファの対応を決める。
    desc_.InputLayout = { inputElements, count };
}

// シェーダーのバイト列は借用するため、呼び出し元はPSOの生成が終わるまでBlobを保持する。
void GraphicsPipelineBuilder::SetShaders(IDxcBlob* vsBlob, IDxcBlob* psBlob) {
    // 頂点シェーダーとピクセルシェーダーのバイナリをPSOへ設定する。
    if (vsBlob)
        desc_.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    else desc_.VS = { nullptr, 0 };

    if (psBlob)
        desc_.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    else desc_.PS = { nullptr, 0 };
}

void GraphicsPipelineBuilder::SetTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType) {
    // 三角形や線など、このPSOで扱うプリミティブ種別を設定する。
    desc_.PrimitiveTopologyType = topologyType;
}

void GraphicsPipelineBuilder::SetRasterizerState(D3D12_CULL_MODE cullMode, D3D12_FILL_MODE fillMode) {
    // カリングと塗りつぶし方式を設定し、ラスタライズ時の面の扱いを決める。
    desc_.RasterizerState.CullMode = cullMode;
    desc_.RasterizerState.FillMode = fillMode;
}

void GraphicsPipelineBuilder::SetDepthBias(INT depthBias, FLOAT depthBiasClamp, FLOAT slopeScaledDepthBias) {
    // 影描画などで使う深度バイアスを設定し、Zファイティングを抑える。
    desc_.RasterizerState.DepthBias = depthBias;
    desc_.RasterizerState.DepthBiasClamp = depthBiasClamp;
    desc_.RasterizerState.SlopeScaledDepthBias = slopeScaledDepthBias;
}

void GraphicsPipelineBuilder::SetDepthStencilState(bool depthEnable, D3D12_DEPTH_WRITE_MASK writeMask, D3D12_COMPARISON_FUNC func) {
    // 深度テストと深度書き込みの挙動を設定する。
    desc_.DepthStencilState.DepthEnable = depthEnable;
    desc_.DepthStencilState.DepthWriteMask = writeMask;
    desc_.DepthStencilState.DepthFunc = func;
}

void GraphicsPipelineBuilder::SetRenderTargets(UINT numRenderTargets, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat) {
    // 出力先RTV/DSVフォーマットを設定し、PSOと描画先の互換性を取る。
    desc_.NumRenderTargets = numRenderTargets;

    for (UINT i = 0; i < numRenderTargets; ++i) {
        desc_.RTVFormats[i] = rtvFormats[i];
    }

    desc_.DSVFormat = dsvFormat;
}

void GraphicsPipelineBuilder::SetBlendMode(BlendMode mode) {
    // 通常、加算、減算、乗算、スクリーンなどのブレンド状態をまとめて設定する。
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    switch (mode) {
    // 不透明描画として背後の色を合成せず、深度も書き込みます。
    case BlendMode::None:
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        desc_.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        break;

    // 新しい色に自身のα、背景色に1−αを掛けて足します。この既定経路では深度も書き込みます。
    case BlendMode::Normal:
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc_.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        break;

    // 背景色にαで重み付けした描画色を足し、光の重なりを表現します。
    case BlendMode::Add:
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc_.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度書き込みOFF
        break;

    // 背景色からαで重み付けした描画色を引きます。
    case BlendMode::Subtract:
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc_.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        break;

    // 背景色へ描画色を掛けます。描画色が白なら背景はそのままです。
    case BlendMode::Multiply:
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc_.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        break;

    // 背景の1−色を描画色に掛けて足し、明るい背景では加算を弱めます。
    case BlendMode::Screen:
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc_.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        break;
    }

    desc_.BlendState = blendDesc;
}

void GraphicsPipelineBuilder::Build(ID3D12Device* device, ID3D12PipelineState** outPipelineState) {
    // 蓄積した設定からID3D12PipelineStateを作成する。
    HRESULT hr = device->CreateGraphicsPipelineState(&desc_, IID_PPV_ARGS(outPipelineState));
    assert(SUCCEEDED(hr));
}

// PSO作成失敗を呼び出し元で扱える入口。生成を試す前に出力先を空にし、HRESULTを返す。
HRESULT GraphicsPipelineBuilder::TryBuild(ID3D12Device* device, ID3D12PipelineState** outPipelineState) {
    if (!device || !outPipelineState)
        return E_INVALIDARG;
    *outPipelineState = nullptr;
    return device->CreateGraphicsPipelineState(&desc_, IID_PPV_ARGS(outPipelineState));
}
