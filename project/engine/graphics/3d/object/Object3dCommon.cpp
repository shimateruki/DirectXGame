#include "Object3dCommon.h"
#include "../FoliageInteraction.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h" 
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

void Object3dCommon::Initialize(DirectXCommon* dxCommon) {
    // 描画パスごとに専用 PSO を先に作り、実行中の描画切り替えでは再コンパイルしない。
    BeginInitialize(dxCommon);
    while (!InitializeStep()) {
    }
}

void Object3dCommon::BeginInitialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    initializationStep_ = 0;
}

bool Object3dCommon::InitializeStep() {
    using Step = void (Object3dCommon::*)();
    static constexpr std::array<Step, kInitializationStepCount> steps = {
        &Object3dCommon::CreateRootSignature,
        &Object3dCommon::CreateShadowRootSignature,
        &Object3dCommon::CreatePipelineStates,
        &Object3dCommon::CreateLocalFogPipeline,
        &Object3dCommon::CreateEffectRootSignature,
        &Object3dCommon::CreateEffectPipeline,
        &Object3dCommon::CreateWaterRootSignature,
        &Object3dCommon::CreateWaterPipeline,
        &Object3dCommon::CreateMagmaPipeline,
        &Object3dCommon::CreateIcePipeline,
        &Object3dCommon::CreateFirePipeline,
        &Object3dCommon::CreateLaserPipeline,
        &Object3dCommon::CreateSlimeGelPipeline,
        &Object3dCommon::CreateShockwavePipeline,
        &Object3dCommon::CreateLiquidContactPipeline,
        &Object3dCommon::CreateDamageCrackPipeline,
        &Object3dCommon::CreateUpdraftPipeline,
        &Object3dCommon::CreateStunBindPipeline,
        &Object3dCommon::CreateCrownUnlockPipeline,
        &Object3dCommon::CreatePoisonSporePipeline,
        &Object3dCommon::CreateCloudPipeline,
        &Object3dCommon::CreateGatePortalPipeline,
        &Object3dCommon::CreateWindOrbPipeline,
        &Object3dCommon::CreateSkyboxPipeline,
    };
    if (initializationStep_ >= steps.size()) {
        return true;
    }
    // 依存するRootSignatureを先に作る順序を保ったまま、一回で一種類だけ生成する。
    (this->*steps[initializationStep_++])();
    return initializationStep_ == steps.size();
}

float Object3dCommon::GetInitializationProgress() const {
    return static_cast<float>(initializationStep_) / static_cast<float>(kInitializationStepCount);
}

void Object3dCommon::SetGraphicsCommand() {
    // ルート番号 15 は通常描画と影描画で共有する草の接触履歴に対応する。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetGraphicsRoot32BitConstants(15, 32, FoliageInteraction::samples.data(), 0);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetPipelineState(BlendMode blendMode) {
    // 呼び出し元が選んだブレンド方式と初期化時に作成した PSO の添字を揃える。
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

    // [13] Mesh Material Factors (Root Constants b5 - Pixel)
    builder.AddConstants(5, 6, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    // [14] 草の頂点アニメーション用マテリアル。既存の番号は維持する。
    builder.AddCBV(6, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    // [15] プレイヤーの草への接触履歴。Root Parameter合計60 DWORD。
    builder.AddConstants(7, 32, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // =================================================================
    // 2. サンプラーの設定 (s0, s1)
    // =================================================================

    // s0: テクスチャ用の標準サンプラー (リニア補間、ラップ)
    builder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    // s1: 影マップ用の特殊サンプラー (くっきり判定、範囲外は真っ白)
    D3D12_STATIC_SAMPLER_DESC shadowSampler{};
    shadowSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/object3d/Object3d.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/object3d/Object3d.PS.hlsl", L"ps_6_0");

    // =========================================================
    // 【通常描画用 PSO (ブレンドモード6種類)】
    // =========================================================
    GraphicsPipelineBuilder builder;
    builder.SetRootSignature(rootSignature_.Get());
    builder.SetInputLayout(inputLayout, _countof(inputLayout));
    builder.SetShaders(vsBlob.Get(), psBlob.Get());
    builder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);

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
    auto shadowVsBlob = dxCommon_->CompileShader(L"Resources/shader/object3d/Shadow.VS.hlsl", L"vs_6_0");

    GraphicsPipelineBuilder shadowBuilder;
    shadowBuilder.SetRootSignature(shadowRootSignature_.Get());
    shadowBuilder.SetInputLayout(inputLayout, _countof(inputLayout));

    // ★ 影なのでピクセルシェーダーは不要！
    shadowBuilder.SetShaders(shadowVsBlob.Get(), nullptr);
    shadowBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);

    // ★ 影のギザギザノイズ（シャドウアクネ）を防ぐ設定を一撃で！
    shadowBuilder.SetDepthBias(10000, 0.0f, 1.0f);
    shadowBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ALL);

    // ★ 書き込み先はRTVなし(0個)、深度は 32bit Float
    shadowBuilder.SetRenderTargets(0, nullptr, DXGI_FORMAT_D32_FLOAT);

    shadowBuilder.Build(device, shadowPipelineState_.GetAddressOf());
    // 草の薄い葉が大きな深度バイアスで地面の裏へ消えない専用設定。
    shadowBuilder.SetDepthBias(128, 0.0f, 0.35f);
    shadowBuilder.Build(device, foliageShadowPipelineState_.GetAddressOf());
}

void Object3dCommon::SetShadowPipelineState(bool foliage) {
    // 葉の影だけ深度バイアスを弱め、薄い面が地面の裏へ消えるのを防ぐ。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetPipelineState(foliage ? foliageShadowPipelineState_.Get() : shadowPipelineState_.Get());
}

void Object3dCommon::SetShadowGraphicsCommand() {
    // 影パスは色ターゲットを持たないため、通常描画とは別のルート構成を使う。
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

    // [2,3] 通常描画と同じ風・踏み分けを影にも反映する。
    builder.AddCBV(6, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    builder.AddConstants(7, 32, 0, D3D12_SHADER_VISIBILITY_VERTEX);

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

    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/materials/LocalFog.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/materials/LocalFog.PS.hlsl", L"ps_6_0");

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
    // 深度から霧量を再構成する専用パスへ切り替える。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(localFogRootSignature_.Get());
    commandList->SetPipelineState(localFogPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// ==========================================================
//  エフェクト用コマンドのセット
// ==========================================================
void Object3dCommon::SetEffectGraphicsCommand(BlendMode blendMode) {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(effectRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 指定されたブレンドモードのPSOをセット
    commandList->SetPipelineState(effectPipelineStates_[static_cast<size_t>(blendMode)].Get());
}

// ==========================================================
//  エフェクト用ルートシグネチャの構築
// ==========================================================
void Object3dCommon::CreateEffectRootSignature() {
    RootSignatureBuilder builder;

    // パラメータ0: CBV (b0, 頂点シェーダー用) -> ViewProjection
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // パラメータ1: CBV (b1, 頂点シェーダー用) -> WorldTransform
    builder.AddCBV(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // パラメータ2: CBV (b0, ピクセルシェーダー用) -> EffectMaterial (色・UVスクロール時間など)
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // パラメータ3: DescriptorTable (t0, ピクセルシェーダー用) -> エフェクト用メインテクスチャ
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // パラメータ4: DescriptorTable (t1, ピクセルシェーダー用) -> 背景Grabテクスチャ
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // =========================================================
    //  パラメータ5: DescriptorTable (t2, ピクセルシェーダー用) -> ノイズテクスチャ
    // =========================================================
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    // サンプラー (s0, ピクセルシェーダー用)
    // デフォルト引数で MIN_MAG_MIP_LINEAR と WRAP が設定されるので指定はレジスタ番号だけでOK！
    builder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // ビルド実行 (頂点レイアウトを使用するフラグを立てる)
    builder.Build(
        dxCommon_->GetDevice(),
        effectRootSignature_.GetAddressOf(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );
}

// ==========================================================
// ★ エフェクト用パイプラインの構築 (ブレンドモード全対応版)
// ==========================================================
void Object3dCommon::CreateEffectPipeline() {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
         { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
         { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
         { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
         { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
         { "WEIGHT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
         { "INDEX",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // シェーダーのコンパイル (前回作成した軽量シェーダー)
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/effects/Effect3d.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/effects/Effect3d.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(effectRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    // ★超重要: 斬撃の裏側が消えないように CULL_MODE_NONE にする！
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);

    // Zテスト(奥にあるものは隠れる)は有効にしておく
    // ※エフェクトなのでZバッファへの書き込みはしない(ZERO)
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO);

    // レンダーターゲットの設定
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);


    for (size_t i = 0; i < static_cast<size_t>(BlendMode::kCountOfBlendMode); ++i) {
        BlendMode mode = static_cast<BlendMode>(i);

        // ビルダーに現在のループのブレンドモードをセット
        psoBuilder.SetBlendMode(mode);

        // 配列の該当インデックスにPSOをビルドして保存
        psoBuilder.Build(dxCommon_->GetDevice(), effectPipelineStates_[i].GetAddressOf());
    }
}

// 水専用のルートシグネチャ作成
void Object3dCommon::CreateWaterRootSignature() {
    RootSignatureBuilder builder;

    // [0] WVP (b0レジスタ)
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    // [1] 波パラメータ (b1レジスタ)
    builder.AddCBV(1, 0, D3D12_SHADER_VISIBILITY_ALL);
    // [2] マテリアル/色 (b2レジスタ)
    builder.AddCBV(2, 0, D3D12_SHADER_VISIBILITY_ALL);
    //  [3] 背景深度テクスチャ (t0レジスタ)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    // [5-7] ベイク済みの補助テクスチャ (t2-t4レジスタ)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    //  サンプラー (s0レジスタ) 境界が綺麗に補間されるようにCLAMPを指定
    builder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    // 引数でポインタを渡して構築結果を受け取る
    builder.Build(dxCommon_->GetDevice(), waterRootSignature_.GetAddressOf(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
}

// 水専用のパイプライン（PSO）作成
void Object3dCommon::CreateWaterPipeline() {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // 先ほど作ったシェーダーを読み込む
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Water.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Water.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(waterRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    // 水が裏側（水中）からも見えるようにカリングをオフ
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);

    // 半透明合成（アルファブレンド）
    psoBuilder.SetBlendMode(BlendMode::kNormal);

    // Zテストはする(true)が、Z書き込みはしない(ZERO)ように引数で直接指定
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    // RenderTargetのフォーマット設定
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);

    // 引数でポインタを渡して構築結果を受け取る
    psoBuilder.Build(dxCommon_->GetDevice(), waterPipelineState_.GetAddressOf());
}

// 描画コマンドセット
void Object3dCommon::SetWaterGraphicsCommand() {
    // 水材質の共有ルートと半透明 PSO を対で設定する。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(waterPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::CreateSpecialMaterialPipeline(const wchar_t* vertexShaderPath, const wchar_t* pixelShaderPath, BlendMode blendMode, D3D12_CULL_MODE cullMode, ID3D12PipelineState** outPipelineState, bool shaderDepthTest) {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    auto vsBlob = dxCommon_->CompileShader(vertexShaderPath, L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(pixelShaderPath, L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(waterRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());
    psoBuilder.SetRasterizerState(cullMode, D3D12_FILL_MODE_SOLID);
    psoBuilder.SetBlendMode(blendMode);
    // 深度をSRVとして読む材質はDSVが接続されないため、シェーダーで遮蔽を判定します。
    psoBuilder.SetDepthStencilState(!shaderDepthTest, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), outPipelineState);
}

// ==========================================
// マグマ専用のパイプライン作成
// ==========================================
void Object3dCommon::CreateMagmaPipeline() {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // ★頂点シェーダーは水(Water.VS)を使い回し、ピクセルシェーダーだけMagmaにする！
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Magma.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Magma.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    // ルートシグネチャも水のものを完全に使い回す
    psoBuilder.SetRootSignature(waterRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    // ★マグマは不透明で分厚いので裏面カリング(CULL_MODE_BACK)
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID);
    // ★ブレンドなし（完全不透明）
    psoBuilder.SetBlendMode(BlendMode::kNone);
    // ★ZテストもZ書き込みも行う
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), magmaPipelineState_.GetAddressOf());
}

// ==========================================
// 氷専用のパイプライン作成
// ==========================================
void Object3dCommon::CreateIcePipeline() {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Ice.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Ice.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(waterRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    // ★氷は分厚く見せたいのでカリングなし(CULL_MODE_NONE)
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);
    // ★半透明合成
    psoBuilder.SetBlendMode(BlendMode::kNormal);
    // ★Zテストはするが、Z書き込みはしない(透明物の基本)
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), icePipelineState_.GetAddressOf());
}

// コマンドセット関数
void Object3dCommon::SetMagmaGraphicsCommand() {
    // 水系の定数バッファ配置を共用しつつ、不透明のマグマ PSO を選ぶ。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(magmaPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetIceGraphicsCommand() {
    // 氷の半透明パスへ切り替え、深度書き込みをしない PSO を使う。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(icePipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::CreateFirePipeline() {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Fire.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/materials/Fire.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(waterRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    // 炎は裏側からも見えるようにカリングなし、半透明合成
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);
    psoBuilder.SetBlendMode(BlendMode::kNormal);
    // このパスはDSVを外して深度を読むため、遮蔽は炎シェーダー側で処理します。
    psoBuilder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), firePipelineState_.GetAddressOf());
}

void Object3dCommon::SetFireGraphicsCommand() {
    if (!firePipelineState_) return; // NULLチェック追加
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(firePipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::CreateLaserPipeline() {
    // ビームは加算合成し、深度テクスチャによる遮蔽をシェーダー側で判定する。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/LaserBeam.VS.hlsl",
        L"Resources/shader/materials/LaserBeam.PS.hlsl",
        BlendMode::kAdd,
        D3D12_CULL_MODE_NONE,
        laserPipelineState_.GetAddressOf(), true
    );
}

void Object3dCommon::CreateSlimeGelPipeline() {
    // ゼリー状の表面は両面を半透明合成する。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/SlimeGel.VS.hlsl",
        L"Resources/shader/materials/SlimeGel.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        slimeGelPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateShockwavePipeline() {
    // 地表の衝撃波は薄い面の裏側からも見えるよう両面描画する。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/GroundShockwave.VS.hlsl",
        L"Resources/shader/materials/GroundShockwave.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        shockwavePipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateLiquidContactPipeline() {
    // 水面との接触表現は水と同じ定数バッファの配置を使う。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/LiquidContact.VS.hlsl",
        L"Resources/shader/materials/LiquidContact.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        liquidContactPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateDamageCrackPipeline() {
    // ひび割れは地形の向きに依存せず見えるよう両面描画する。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/DamageCrack.VS.hlsl",
        L"Resources/shader/materials/DamageCrack.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        damageCrackPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateUpdraftPipeline() {
    // 上昇気流は視点が内側に入っても見える半透明の両面パスにする。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/Updraft.VS.hlsl",
        L"Resources/shader/materials/Updraft.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        updraftPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateStunBindPipeline() {
    // 拘束光は背景へ加算して発光させる。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/StunBind.VS.hlsl",
        L"Resources/shader/materials/StunBind.PS.hlsl",
        BlendMode::kAdd,
        D3D12_CULL_MODE_NONE,
        stunBindPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateCrownUnlockPipeline() {
    // 王冠解除の光も加算合成し、暗い背景で消えないようにする。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/CrownUnlock.VS.hlsl",
        L"Resources/shader/materials/CrownUnlock.PS.hlsl",
        BlendMode::kAdd,
        D3D12_CULL_MODE_NONE,
        crownUnlockPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreatePoisonSporePipeline() {
    // 胞子の薄い面を両側から描けるようカリングを無効にする。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/PoisonSpore.VS.hlsl",
        L"Resources/shader/materials/PoisonSpore.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        poisonSporePipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateCloudPipeline() {
    // 雲は半透明で描き、視点が内部に入っても面を残す。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/Cloud.VS.hlsl",
        L"Resources/shader/materials/Cloud.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        cloudPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateGatePortalPipeline() {
    // ゲート面は通過前後の両側から見えるよう両面描画する。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/GatePortal.VS.hlsl",
        L"Resources/shader/materials/GatePortal.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_NONE,
        gatePortalPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::CreateWindOrbPipeline() {
    // 風の球は閉じた形状なので裏面を除き、半透明で合成する。
    CreateSpecialMaterialPipeline(
        L"Resources/shader/materials/WindOrb.VS.hlsl",
        L"Resources/shader/materials/WindOrb.PS.hlsl",
        BlendMode::kNormal,
        D3D12_CULL_MODE_BACK,
        windOrbPipelineState_.GetAddressOf()
    );
}

void Object3dCommon::SetLaserGraphicsCommand() {
    // 未生成の PSO では描かず、水系の共有ルートとビーム用 PSO を組にする。
    if (!laserPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(laserPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetSlimeGelGraphicsCommand() {
    // ゼリー材質の描画前に、水系の共有ルートを再設定する。
    if (!slimeGelPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(slimeGelPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetShockwaveGraphicsCommand() {
    // 衝撃波の描画へ切り替える際に、共有ルートも同時に束縛する。
    if (!shockwavePipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(shockwavePipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetLiquidContactGraphicsCommand() {
    // 接触エフェクトへ切り替える際に、共有ルートも同時に束縛する。
    if (!liquidContactPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(liquidContactPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetDamageCrackGraphicsCommand() {
    // ひび割れ材質へ切り替える際に、共有ルートも同時に束縛する。
    if (!damageCrackPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(damageCrackPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetUpdraftGraphicsCommand() {
    // 上昇気流の半透明パスを使う前に、共有ルートを束縛する。
    if (!updraftPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(updraftPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetStunBindGraphicsCommand() {
    // 拘束光の加算パスを使う前に、共有ルートを束縛する。
    if (!stunBindPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(stunBindPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetCrownUnlockGraphicsCommand() {
    // 王冠解除の加算パスを使う前に、共有ルートを束縛する。
    if (!crownUnlockPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(crownUnlockPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetPoisonSporeGraphicsCommand() {
    // 胞子材質の半透明パスを使う前に、共有ルートを束縛する。
    if (!poisonSporePipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(poisonSporePipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetCloudGraphicsCommand() {
    // 雲材質の半透明パスを使う前に、共有ルートを束縛する。
    if (!cloudPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(cloudPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetGatePortalGraphicsCommand() {
    // ゲート材質の半透明パスを使う前に、共有ルートを束縛する。
    if (!gatePortalPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(gatePortalPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::SetWindOrbGraphicsCommand() {
    // 風の球材質の半透明パスを使う前に、共有ルートを束縛する。
    if (!windOrbPipelineState_) return;
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(windOrbPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::CreateSkyboxPipeline() {
    // 背景は常に奥へ描くため、深度テストと書き込みを行わない。
    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rsBuilder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rsBuilder.Build(dxCommon_->GetDevice(), skyboxRootSignature_.GetAddressOf());
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/skybox/Skybox.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/skybox/Skybox.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder pipelineBuilder;
    pipelineBuilder.SetRootSignature(skyboxRootSignature_.Get());
    pipelineBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    pipelineBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE);

    pipelineBuilder.SetBlendMode(BlendMode::kNone);

    pipelineBuilder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS);

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    pipelineBuilder.SetInputLayout(inputElements, _countof(inputElements));

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    pipelineBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    pipelineBuilder.Build(dxCommon_->GetDevice(), skyboxPipelineState_.GetAddressOf());
}
