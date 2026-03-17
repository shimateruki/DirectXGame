#include "Object3dCommon.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h" 
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

void Object3dCommon::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    CreateRootSignature();
    CreateShadowRootSignature();
    CreatePipelineStates();
    CreateLocalFogPipeline();

}

void Object3dCommon::SetGraphicsCommand() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetPipelineState(BlendMode blendMode) {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetPipelineState(graphicsPipelineStates_[static_cast<size_t>(blendMode)].Get());
}



void Object3dCommon::CreateRootSignature() {
    RootSignatureBuilder builder;

    // =================================================================
    // 1. ルートパラメータの設定 (順番は元の配列[0]～[12]と完全一致させています)
    // =================================================================

    // [0] Material (CBV b0 - Pixel)
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [1] TransformationMatrix (CBV b0 - Vertex)
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [2] Texture (DescriptorTable t0 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [3] DirectionalLight (CBV b1 - Pixel)
    builder.AddCBV(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [4] Camera (CBV b2 - Pixel)
    builder.AddCBV(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [5] PointLight (CBV b3 - Pixel)
    builder.AddCBV(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [6] SpotLight (CBV b4 - Pixel)
    builder.AddCBV(4, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [7] Skinning Matrix (DescriptorTable t1 - Vertex)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [8] Environment Map (DescriptorTable t2 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [9] Normal Map (DescriptorTable t3 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [10] ORM Map (DescriptorTable t4 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [11] Shadow WVP (CBV b1 - Vertex)
    builder.AddCBV(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [12] Shadow Map (DescriptorTable t5 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // =================================================================
    // 2. サンプラーの設定 (s0, s1)
    // =================================================================

    // s0: テクスチャ用の標準サンプラー (リニア補間、ラップ)
    builder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    // s1: 影マップ用の特殊サンプラー (くっきり判定、範囲外は真っ白)
    D3D12_STATIC_SAMPLER_DESC shadowSampler{};
    shadowSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    shadowSampler.MaxLOD = D3D12_FLOAT32_MAX;
    shadowSampler.ShaderRegister = 1; // s1
    shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    builder.AddStaticSamplerCustom(shadowSampler);

    // =================================================================
    // 3. ビルド実行
    // =================================================================
    builder.Build(dxCommon_->GetDevice(), rootSignature_.GetAddressOf());
}

void Object3dCommon::CreatePipelineStates() {
    ID3D12Device* device = dxCommon_->GetDevice();

    // 1. 入力レイアウトの定義
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "WEIGHT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "INDEX",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // 2. シェーダーのコンパイル
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/Object3d.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/Object3d.PS.hlsl", L"ps_6_0");

    // =========================================================
    // 【通常描画用 PSO (ブレンドモード6種類)】
    // =========================================================
    GraphicsPipelineBuilder builder;
    builder.SetRootSignature(rootSignature_.Get());
    builder.SetInputLayout(inputLayout, _countof(inputLayout));
    builder.SetShaders(vsBlob.Get(), psBlob.Get());
    builder.SetRasterizerState(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID);

    // レンダーターゲットと深度の設定 (HDR用 16bit Float + Depth 24bit)
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    builder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_D24_UNORM_S8_UINT);
    builder.SetDepthStencilState(true);

    // ブレンドモードごとにPSOを生成
    for (size_t i = 0; i < static_cast<size_t>(BlendMode::kCountOfBlendMode); ++i) {
        BlendMode mode = static_cast<BlendMode>(i);
        // ★ builder側でBlendStateとDepthWriteMaskを自動で良しなに設定してくれる！
        builder.SetBlendMode(mode);
        builder.Build(device, graphicsPipelineStates_[i].GetAddressOf());
    }

    // =========================================================
    // 【影描画用 (シャドウマップ) PSO】
    // =========================================================
    auto shadowVsBlob = dxCommon_->CompileShader(L"Resources/shader/Shadow.VS.hlsl", L"vs_6_0");

    GraphicsPipelineBuilder shadowBuilder;
    shadowBuilder.SetRootSignature(shadowRootSignature_.Get());
    shadowBuilder.SetInputLayout(inputLayout, _countof(inputLayout));

    // ★ 影なのでピクセルシェーダーは不要！
    shadowBuilder.SetShaders(shadowVsBlob.Get(), nullptr);
    shadowBuilder.SetRasterizerState(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID);

    // ★ 影のギザギザノイズ（シャドウアクネ）を防ぐ設定を一撃で！
    shadowBuilder.SetDepthBias(10000, 0.0f, 1.0f);
    shadowBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ALL);

    // ★ 書き込み先はRTVなし(0個)、深度は 32bit Float
    shadowBuilder.SetRenderTargets(0, nullptr, DXGI_FORMAT_D32_FLOAT);

    shadowBuilder.Build(device, shadowPipelineState_.GetAddressOf());
}

void Object3dCommon::SetShadowPipelineState() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetPipelineState(shadowPipelineState_.Get());
}

void Object3dCommon::SetShadowGraphicsCommand() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(shadowRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::CreateShadowRootSignature() {
    RootSignatureBuilder builder;


    // [0] WVP行列 (CBV b0 - VertexShader用)
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [1] ボーン情報 (DescriptorTable t0 - VertexShader用)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // ビルド実行
    builder.Build(dxCommon_->GetDevice(), shadowRootSignature_.GetAddressOf());
}
void Object3dCommon::CreateLocalFogPipeline() {
    auto device = dxCommon_->GetDevice();

    // =================================================================
    // 1. ルートシグネチャの作成
    // =================================================================
    RootSignatureBuilder rsBuilder;

    // [0] b0: WVP行列 (Model::DrawShadow と一致させる)
    rsBuilder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [1] t1: ボーン情報 (Model::DrawShadow がここに書き込んでくるので空けておく！)
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [2] t0: デプステクスチャ (ピクセルシェーダー用)
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [3] b1: ローカルフォグ設定
    rsBuilder.AddCBV(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // s0: テクスチャサンプラー (Clamp設定)
    rsBuilder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    rsBuilder.Build(device, localFogRootSignature_.GetAddressOf());

    // =================================================================
    // 2. PSOの作成
    // =================================================================
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "WEIGHT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "INDEX",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/LocalFog.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/LocalFog.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(localFogRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    // ★重要: カメラが箱の中に入っても見えるように、表面をカリングして裏面を描く！
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_FRONT, D3D12_FILL_MODE_SOLID);

    // 半透明合成設定 (BlendMode::kNormal と同じ設定を流用)
    psoBuilder.SetBlendMode(BlendMode::kNormal);

    // ★重要: フォグはZテスト不要＆Zバッファへの書き込みもしないので上書きしてOFFにする
    psoBuilder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO);

    // レンダーターゲット (デプスバッファは使わないので DSVFormat は UNKNOWN にする)
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    psoBuilder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_UNKNOWN);

    psoBuilder.Build(device, localFogPipelineState_.GetAddressOf());
}

void Object3dCommon::SetLocalFogGraphicsCommand() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(localFogRootSignature_.Get());
    commandList->SetPipelineState(localFogPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}