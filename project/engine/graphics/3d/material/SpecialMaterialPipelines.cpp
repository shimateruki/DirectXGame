#include "SpecialMaterialPipelines.h"
#include "DirectXCommon.h"
#include "GraphicsPipelineBuilder.h"
#include "RootSignatureBuilder.h"

namespace engine {

struct SpecialMaterialPipelines::PipelineDescription {
    // 水・マグマ・氷・炎は専用の生成関数を使う。
    void (SpecialMaterialPipelines::*create)() = nullptr;
    const wchar_t* vertexShaderPath = nullptr;
    const wchar_t* pixelShaderPath = nullptr;
    BlendMode blendMode = BlendMode::None;
    D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE;
    // trueなら深度をシェーダーで読み、固定機能の深度テストを無効にする。
    bool shaderDepthTest = false;
};

const SpecialMaterialPipelines::PipelineDescription&
SpecialMaterialPipelines::GetPipelineDescription(PipelineKind kind) {
    static constexpr std::array<PipelineDescription, static_cast<std::size_t>(PipelineKind::Count)> descriptions = {{
        {&SpecialMaterialPipelines::CreateWaterPipeline},
        {&SpecialMaterialPipelines::CreateMagmaPipeline},
        {&SpecialMaterialPipelines::CreateIcePipeline},
        {&SpecialMaterialPipelines::CreateFirePipeline},
        {nullptr, L"Resources/shader/materials/LaserBeam.VS.hlsl",
                  L"Resources/shader/materials/LaserBeam.PS.hlsl",
                  BlendMode::Add, D3D12_CULL_MODE_NONE, true},
        {nullptr, L"Resources/shader/materials/SlimeGel.VS.hlsl",
                  L"Resources/shader/materials/SlimeGel.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/GroundShockwave.VS.hlsl",
                  L"Resources/shader/materials/GroundShockwave.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/LiquidContact.VS.hlsl",
                  L"Resources/shader/materials/LiquidContact.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/DamageCrack.VS.hlsl",
                  L"Resources/shader/materials/DamageCrack.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/Updraft.VS.hlsl",
                  L"Resources/shader/materials/Updraft.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/StunBind.VS.hlsl",
                  L"Resources/shader/materials/StunBind.PS.hlsl",
                  BlendMode::Add, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/CrownUnlock.VS.hlsl",
                  L"Resources/shader/materials/CrownUnlock.PS.hlsl",
                  BlendMode::Add, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/PoisonSpore.VS.hlsl",
                  L"Resources/shader/materials/PoisonSpore.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/Cloud.VS.hlsl",
                  L"Resources/shader/materials/Cloud.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/GatePortal.VS.hlsl",
                  L"Resources/shader/materials/GatePortal.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_NONE},
        {nullptr, L"Resources/shader/materials/WindOrb.VS.hlsl",
                  L"Resources/shader/materials/WindOrb.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_BACK},
        {nullptr, L"Resources/shader/materials/VisibilitySilhouette.VS.hlsl",
                  L"Resources/shader/materials/VisibilitySilhouette.PS.hlsl",
                  BlendMode::Normal, D3D12_CULL_MODE_BACK, true},
    }};
    return descriptions.at(static_cast<std::size_t>(kind));
}

SpecialMaterialPipelines::PipelineState&
SpecialMaterialPipelines::GetPipelineState(PipelineKind kind) {
    return pipelines_.at(static_cast<std::size_t>(kind));
}

ID3D12PipelineState* SpecialMaterialPipelines::PreparePipeline(PipelineKind kind) {
    auto& state = GetPipelineState(kind);
    if (!state) {
        const auto& description = GetPipelineDescription(kind);
        // 専用の合成・深度条件がある四種類は個別の生成関数へ渡し、残りは定義表の設定で作ります。
        if (description.create) {
            (this->*description.create)();
        } else {
            CreateSpecialMaterialPipeline(description, state.GetAddressOf());
        }
    }
    return state.Get();
}

void SpecialMaterialPipelines::PrepareForTitle() {
    if (!waterRootSignature_) CreateWaterRootSignature();
    // タイトルで使う材質だけを先に準備します。他の材質は共通ロードまたは初回描画で作られます。
    PreparePipeline(PipelineKind::Water);
    PreparePipeline(PipelineKind::Fire);
    PreparePipeline(PipelineKind::GatePortal);
}

bool SpecialMaterialPipelines::IsStepReady(std::size_t step) const {
    if (step == 0) return waterRootSignature_.Get() != nullptr;
    // 準備段階0はルートシグネチャ用なので、PSOの添字は段階番号から1を引きます。
    return pipelines_.at(step - 1).Get() != nullptr;
}

// ロード処理から一段ずつ呼ぶ。最初に共有ルートを用意し、その後は段階に対応するPSOを作る。
void SpecialMaterialPipelines::PrepareStep(std::size_t step) {
    static_assert(static_cast<std::size_t>(PipelineKind::VisibilitySilhouette) + 1 == kPreparationStepCount);
    if (step == 0) {
        CreateWaterRootSignature();
        return;
    }
    PreparePipeline(static_cast<PipelineKind>(step - 1));
}

void SpecialMaterialPipelines::CreateWaterRootSignature() {
    RootSignatureBuilder builder;

    // [0] wvp (b0レジスタ)
    builder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [1] 波パラメータ (b1レジスタ)
    builder.AddCbv(1, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [2] マテリアル/色 (b2レジスタ)
    builder.AddCbv(2, 0, D3D12_SHADER_VISIBILITY_ALL);

    //  [3] 背景深度テクスチャ (t0レジスタ)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    // [4] 背景色のコピー (t1レジスタ)。特殊材質の描画先と別の画像を参照します。
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
void SpecialMaterialPipelines::CreateWaterPipeline() {
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
    psoBuilder.SetBlendMode(BlendMode::Normal);

    // Zテストはする(true)が、Z書き込みはしない(ZERO)ように引数で直接指定
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    // RenderTargetのフォーマット設定
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);

    // 引数でポインタを渡して構築結果を受け取る
    psoBuilder.Build(dxCommon_->GetDevice(), GetPipelineState(PipelineKind::Water).GetAddressOf());
}

void SpecialMaterialPipelines::CreateSpecialMaterialPipeline(
    const PipelineDescription& description, ID3D12PipelineState** out) {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    auto vsBlob = dxCommon_->CompileShader(description.vertexShaderPath, L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(description.pixelShaderPath, L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(waterRootSignature_.Get());
    psoBuilder.SetInputLayout(inputLayout, _countof(inputLayout));
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());
    psoBuilder.SetRasterizerState(description.cullMode, D3D12_FILL_MODE_SOLID);
    psoBuilder.SetBlendMode(description.blendMode);

    // shaderDepthTestを選ぶ材質は固定機能の深度テストを止め、シェーダーで遮蔽を判定します。
    psoBuilder.SetDepthStencilState(!description.shaderDepthTest, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), out);
}

// ==========================================
// マグマ専用のパイプライン作成
// ==========================================
void SpecialMaterialPipelines::CreateMagmaPipeline() {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // 水系のルートシグネチャを共有しますが、頂点・ピクセルの処理はマグマ専用です。
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
    psoBuilder.SetBlendMode(BlendMode::None);

    // ★ZテストもZ書き込みも行う
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), GetPipelineState(PipelineKind::Magma).GetAddressOf());
}

// ==========================================
// 氷専用のパイプライン作成
// ==========================================
void SpecialMaterialPipelines::CreateIcePipeline() {
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
    psoBuilder.SetBlendMode(BlendMode::Normal);

    // ★Zテストはするが、Z書き込みはしない(透明物の基本)
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), GetPipelineState(PipelineKind::Ice).GetAddressOf());
}

void SpecialMaterialPipelines::CreateFirePipeline() {
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
    psoBuilder.SetBlendMode(BlendMode::Normal);

    // このパスはDSVを外して深度を読むため、遮蔽は炎シェーダー側で処理します。
    psoBuilder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS);

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    psoBuilder.Build(dxCommon_->GetDevice(), GetPipelineState(PipelineKind::Fire).GetAddressOf());
}

void SpecialMaterialPipelines::BindPipeline(PipelineKind kind) {
    // 初回の描画呼び出しでも使えるようにPSOの作成を済ませ、その後で今のコマンドリストへ束縛します。
    auto* state = PreparePipeline(kind);
    auto* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(waterRootSignature_.Get());
    commandList->SetPipelineState(state);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void SpecialMaterialPipelines::BindWater() { BindPipeline(PipelineKind::Water); }
void SpecialMaterialPipelines::BindMagma() { BindPipeline(PipelineKind::Magma); }
void SpecialMaterialPipelines::BindIce() { BindPipeline(PipelineKind::Ice); }
void SpecialMaterialPipelines::BindFire() { BindPipeline(PipelineKind::Fire); }
void SpecialMaterialPipelines::BindLaser() { BindPipeline(PipelineKind::Laser); }
void SpecialMaterialPipelines::BindSlimeGel() { BindPipeline(PipelineKind::SlimeGel); }
void SpecialMaterialPipelines::BindShockwave() { BindPipeline(PipelineKind::Shockwave); }
void SpecialMaterialPipelines::BindLiquidContact() { BindPipeline(PipelineKind::LiquidContact); }
void SpecialMaterialPipelines::BindDamageCrack() { BindPipeline(PipelineKind::DamageCrack); }
void SpecialMaterialPipelines::BindUpdraft() { BindPipeline(PipelineKind::Updraft); }
void SpecialMaterialPipelines::BindStunBind() { BindPipeline(PipelineKind::StunBind); }
void SpecialMaterialPipelines::BindCrownUnlock() { BindPipeline(PipelineKind::CrownUnlock); }
void SpecialMaterialPipelines::BindPoisonSpore() { BindPipeline(PipelineKind::PoisonSpore); }
void SpecialMaterialPipelines::BindCloud() { BindPipeline(PipelineKind::Cloud); }
void SpecialMaterialPipelines::BindGatePortal() { BindPipeline(PipelineKind::GatePortal); }
void SpecialMaterialPipelines::BindWindOrb() { BindPipeline(PipelineKind::WindOrb); }
void SpecialMaterialPipelines::BindVisibilitySilhouette() { BindPipeline(PipelineKind::VisibilitySilhouette); }

} // namespace engine
