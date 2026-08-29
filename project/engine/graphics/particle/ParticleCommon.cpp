#include "ParticleCommon.h"
#include "DirectXCommon.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h"
#include <cassert>

void ParticleCommon::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    CreateRootSignature();
    CreatePipeline();
}


void ParticleCommon::CreateRootSignature() {
    RootSignatureBuilder builder;

    // =================================================================
    // 1. ルートパラメータの設定
    // =================================================================

    // [0] Camera (CBV b0 - VertexShader用)
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [1] Texture (DescriptorTable t0 - PixelShader用)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // =================================================================
    // 2. サンプラーの設定
    // =================================================================

    // s0: テクスチャサンプラー (リニア補間、ラップ - PixelShader用)
    builder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    // =================================================================
    // 3. ビルド実行
    // =================================================================
    builder.Build(dxCommon_->GetDevice(), rootSignature_.GetAddressOf());
}


void ParticleCommon::SetPipeline(ID3D12GraphicsCommandList* commandList, ParticleBlendMode mode) {
    // 範囲チェック（念のため）
    if ((int)mode >= (int)ParticleBlendMode::kCount) return;

    commandList->SetPipelineState(graphicsPipelines_[(int)mode].Get());
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}


void ParticleCommon::CreatePipeline() {
    ID3D12Device* device = dxCommon_->GetDevice();

    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/particle/Particle.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/particle/Particle.PS.hlsl", L"ps_6_0");

    // =================================================================
    // 1. 入力レイアウト (インスタンシング対応の特殊レイアウト)
    // =================================================================
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "WORLD",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "WORLD",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "WORLD",    2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "WORLD",    3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    // =================================================================
    // 2. PSOの共通設定
    // =================================================================
    GraphicsPipelineBuilder builder;
    builder.SetRootSignature(rootSignature_.Get());
    builder.SetInputLayout(inputLayout, _countof(inputLayout));
    builder.SetShaders(vsBlob.Get(), psBlob.Get());

    // カリングなし（両面描画）
    builder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);

    // レンダーターゲットと深度フォーマット
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    builder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_D24_UNORM_S8_UINT);

    // =================================================================
    // 3. ブレンドモードごとの生成ループ
    // =================================================================
    for (int i = 0; i < (int)ParticleBlendMode::kCount; ++i) {
        ParticleBlendMode pMode = static_cast<ParticleBlendMode>(i);

        // Particle用Blend ModeをPipeline共通のBlend Modeへ変換します。
        ::BlendMode globalMode = ::BlendMode::kNormal;
        switch (pMode) {
        case ParticleBlendMode::kAlpha:    globalMode = ::BlendMode::kNormal;   break;
        case ParticleBlendMode::kAdd:      globalMode = ::BlendMode::kAdd;      break;
        case ParticleBlendMode::kSubtract: globalMode = ::BlendMode::kSubtract; break;
        case ParticleBlendMode::kMultiply: globalMode = ::BlendMode::kMultiply; break;
        case ParticleBlendMode::kScreen:   globalMode = ::BlendMode::kScreen;   break;
        }

        // 変換したBlend ModeをPipelineへ設定します。
        builder.SetBlendMode(globalMode);

        // Particleは透明描画のためDepth Testだけを行い、Depthへは書き込みません。
     
        builder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);

        // 設定済みのPipelineを生成します。
        builder.Build(device, graphicsPipelines_[i].GetAddressOf());
    }
}
