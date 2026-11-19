#include "Object3DCommon.h"
#include "engine/graphics/3d/material/SpecialMaterialPipelines.h"
#include "engine/graphics/3d/material/ShaderMaterialRegistry.h"
#include "../FoliageInteraction.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h" 
#include "engine/system/scene/LoadingProfiler.h"
#include "DebugConsole.h"
#include <cassert>
#include <algorithm>
#include <filesystem>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

const size_t engine::Object3DCommon::kInitializationStepCount =
    kCommonPreparationStepCount + SpecialMaterialPipelines::kPreparationStepCount + 1; // 末尾はスカイボックス

engine::Object3DCommon::Object3DCommon() = default;
engine::Object3DCommon::~Object3DCommon() = default;

void engine::Object3DCommon::Initialize(engine::DirectXCommon* dxCommon) {
    // 描画パスごとに専用 PSO を先に作り、実行中の描画切り替えでは再コンパイルしない。
    StartupProfileScope pipelineProfile("Object3dCommon pipelines");
    BeginInitialize(dxCommon);

    while (!InitializeStep()) {
    }
}

void engine::Object3DCommon::InitializeForTitle(engine::DirectXCommon* dxCommon) {
    // タイトル背景で使う通常描画、影、水、炎、ゲート、空だけを初回描画前に準備する。
    // 未使用の材質は各描画入口で初回使用時に生成し、配置アセットの追加にも対応する。
    StartupProfileScope pipelineProfile("Object3dCommon title pipelines");
    BeginInitialize(dxCommon);
    if (!rootSignature_) CreateRootSignature();
    if (!shadowRootSignature_) CreateShadowRootSignature();
    CreatePipelineStatesInternal(true);
    specialMaterialPipelines_->PrepareForTitle();
    if (!skyboxPipelineState_) CreateSkyboxPipeline();
    initializationStep_ = kInitializationStepCount;
}

void engine::Object3DCommon::EnsurePipelineState(
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& state,
    void (engine::Object3DCommon::*createPipeline)()) {
    if (!state) {
        (this->*createPipeline)();
    }
}

// 準備の巡回位置を先頭へ戻す。タイトルで作成済みのPSOは保持し、InitializeStepで不足分だけを補う。
void engine::Object3DCommon::BeginInitialize(engine::DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    if (!specialMaterialPipelines_) {
        specialMaterialPipelines_ = std::make_unique<SpecialMaterialPipelines>();
    }
    specialMaterialPipelines_->SetDirectX(*dxCommon);
    initializationStep_ = 0;
}

bool engine::Object3DCommon::InitializeStep() {
    if (initializationStep_ >= kInitializationStepCount) return true;

    const std::size_t stepIndex = initializationStep_++;
    const bool complete = initializationStep_ == kInitializationStepCount;
    constexpr const char* kProfilePrefix = "Object3dCommon pipeline step ";

    if (stepIndex < kCommonPreparationStepCount) {
        using Step = void (engine::Object3DCommon::*)();
        static constexpr std::array<Step, kCommonPreparationStepCount> steps = {
            &Object3DCommon::CreateRootSignature,
            &Object3DCommon::CreateShadowRootSignature,
            &Object3DCommon::CreatePipelineStates,
            &Object3DCommon::CreateLocalFogPipeline,
            &Object3DCommon::CreateEffectRootSignature,
            &Object3DCommon::CreateEffectPipeline,
        };
        const auto allReady = [](const auto& states) {
            return std::all_of(states.begin(), states.end(), [](const auto& state) { return state.Get() != nullptr; });
        };
        // タイトルなどで準備済みのPSOは段階初期化でも使い回し、不足分だけを各段で調べます。
        const std::array<bool, kCommonPreparationStepCount> ready = {
            rootSignature_.Get() != nullptr,
            shadowRootSignature_.Get() != nullptr,
            allReady(graphicsPipelineStates_) && shadowPipelineState_ && foliageShadowPipelineState_,
            localFogPipelineState_.Get() != nullptr,
            effectRootSignature_.Get() != nullptr,
            allReady(effectPipelineStates_),
        };
        if (!ready[stepIndex]) {
            StartupProfileScope stepProfile(kProfilePrefix + std::to_string(stepIndex));
            (this->*steps[stepIndex])();
        }
    } else if (!complete) {
        // 共通描画の準備に続き、水系を共有ルートから一段ずつ準備する。
        const std::size_t materialStep = stepIndex - kCommonPreparationStepCount;
        if (!specialMaterialPipelines_->IsStepReady(materialStep)) {
            StartupProfileScope stepProfile(kProfilePrefix + std::to_string(stepIndex));
            specialMaterialPipelines_->PrepareStep(materialStep);
        }
    } else if (!skyboxPipelineState_) {
        StartupProfileScope stepProfile(kProfilePrefix + std::to_string(stepIndex));
        CreateSkyboxPipeline();
    }
    return complete;
}

// 完了した準備段階の割合を返す。各段階のコンパイル時間は含めない。
float engine::Object3DCommon::GetInitializationProgress() const {
    return static_cast<float>(initializationStep_) / static_cast<float>(kInitializationStepCount);
}

void engine::Object3DCommon::SetGraphicsCommand() {
    // ルート番号 15 は通常描画と影描画で共有する草の接触履歴に対応する。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetGraphicsRoot32BitConstants(15, 32, FoliageInteraction::samples.data(), 0);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void engine::Object3DCommon::SetPipelineState(BlendMode blendMode) {
    // 呼び出し元が選んだブレンド方式と初期化時に作成した PSO の添字を揃える。
    if (!graphicsPipelineStates_[static_cast<size_t>(blendMode)]) {
        CreatePipelineStatesInternal(false);
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetPipelineState(graphicsPipelineStates_[static_cast<size_t>(blendMode)].Get());
}

void engine::Object3DCommon::SetMaterialPipelineState(BlendMode blendMode, int materialType, const std::string& shaderMaterialId) {
    // 材質と合成方式の組を初回だけ作る。通常材質の既存PSOはそのまま使用する。
    if (blendMode < BlendMode::None || blendMode >= BlendMode::Count)
        blendMode = BlendMode::Normal;
    if (materialType == 0 && !shaderMaterialId.empty()) {
        const auto* definition = ShaderMaterialRegistry::Instance().Find(shaderMaterialId);
        if (definition) {
            const std::string& shaderPath = definition->pixelShader;
            // シェーダーの定義を再読込した場合に古いPSOや作成失敗を使い回さないよう、台帳の世代をキーに含めます。
            const std::string cacheKey = shaderPath + "#" + std::to_string(ShaderMaterialRegistry::Instance().Generation());
            const std::string failureKey = cacheKey + "#" + std::to_string(static_cast<int>(blendMode));
            auto& states = shaderMaterialPipelineStates_[cacheKey];
            auto& state = states[static_cast<size_t>(blendMode)];
            if (!state && !failedShaderMaterials_.contains(failureKey)) {
                const std::wstring widePath = std::filesystem::path(std::u8string(shaderPath.begin(), shaderPath.end())).wstring();
                if (!CreateMaterialVariantPipeline(widePath.c_str(), blendMode, state.GetAddressOf()))
                    failedShaderMaterials_.insert(failureKey);
            }
            if (state) {
                dxCommon_->GetCommandList()->SetPipelineState(state.Get());
                return;
            }
        }
        // 未登録・コンパイル失敗時は通常材質へ戻し、描画欠落を避ける。
        SetPipelineState(blendMode);
        return;
    }
    const wchar_t* shaderPath = nullptr;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)>* states = nullptr;

    // 従来の番号を持つダッシュパネルとデカールは専用PSを使い、それ以外は共通PSOを使う。
    if (materialType == 24) {
        shaderPath = L"Resources/shader/object3d/Object3d.DashPanel.PS.hlsl";
        states = &dashPanelPipelineStates_;
    } else if (materialType == 28) {
        shaderPath = L"Resources/shader/object3d/Object3d.Decal.PS.hlsl";
        states = &decalPipelineStates_;
    }

    if (!states) {
        SetPipelineState(blendMode);
        return;
    }

    auto& pipeline = (*states)[static_cast<size_t>(blendMode)];

    if (!pipeline)
        CreateMaterialVariantPipeline(shaderPath, blendMode, pipeline.GetAddressOf());
    if (pipeline)
        dxCommon_->GetCommandList()->SetPipelineState(pipeline.Get());
    else
        SetPipelineState(blendMode);
}

bool engine::Object3DCommon::CreateMaterialVariantPipeline(const wchar_t* pixelShaderPath, BlendMode blendMode, ID3D12PipelineState** outPipelineState) {
    // 通常描画と同じ頂点入力・ルート・深度設定を使い、ピクセルシェーダだけを差し替える。
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "WEIGHT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "INDEX",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    auto vertexShader = dxCommon_->CompileShader(L"Resources/shader/object3d/Object3d.VS.hlsl", L"vs_6_0");
    std::string shaderError;
    // 編集した外部シェーダーのエラーをログへ返し、上位側の通常材質への復帰を可能にします。
    auto pixelShader = dxCommon_->TryCompileShader(pixelShaderPath, L"ps_6_0", L"main", shaderError);
    if (!pixelShader) {
        DebugConsole::GetInstance()->AddLog("Shader Material compile error: " + shaderError);
        return false;
    }

    GraphicsPipelineBuilder builder;
    builder.SetRootSignature(rootSignature_.Get());
    builder.SetInputLayout(inputLayout, _countof(inputLayout));
    builder.SetShaders(vertexShader.Get(), pixelShader.Get());
    builder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    builder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_D24_UNORM_S8_UINT);
    builder.SetDepthStencilState(true);
    builder.SetBlendMode(blendMode);
    const HRESULT result = builder.TryBuild(dxCommon_->GetDevice(), outPipelineState);
    if (FAILED(result)) {
        DebugConsole::GetInstance()->AddLog("Shader Material pipeline error: " + std::to_string(result));
        return false;
    }
    return outPipelineState && *outPipelineState;
}

void engine::Object3DCommon::CreateRootSignature() {
    RootSignatureBuilder builder;

    // =================================================================
    // 各番号は描画時の束縛先と一致させる。既存の0～12を維持し、材質係数と草の描画用に13～15を使う。
    // =================================================================

    // [0] Material (CBV b0 - Pixel)
    builder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [1] TransformationMatrix (CBV b0 - Vertex)
    builder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [2] Texture (DescriptorTable t0 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [3] DirectionalLight (CBV b1 - Pixel)
    builder.AddCbv(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [4] Camera (CBV b2 - Pixel)
    builder.AddCbv(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [5] PointLight (CBV b3 - Pixel)
    builder.AddCbv(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [6] SpotLight (CBV b4 - Pixel)
    builder.AddCbv(4, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [7] Skinning Matrix (DescriptorTable t1 - Vertex)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [8] Environment Map (DescriptorTable t2 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [9] Normal Map (DescriptorTable t3 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [10] ORM Map (DescriptorTable t4 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [11] Shadow wvp (CBV b1 - Vertex)
    builder.AddCbv(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [12] Shadow Map (DescriptorTable t5 - Pixel)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [13] Mesh Material Factors (Root Constants b5 - Pixel)
    builder.AddConstants(5, 6, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [14] 草の頂点アニメーション用マテリアル。既存の番号は維持する。
    builder.AddCbv(6, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [15] プレイヤーの草への接触履歴。Root Parameter合計60 DWORD。
    builder.AddConstants(7, 32, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // =================================================================
    // 2. サンプラーの設定 (s0, s1)
    // =================================================================

    // s0: テクスチャ用の標準サンプラー (リニア補間、ラップ)
    builder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    // s1: 影マップは点サンプリング。Uの範囲外は白、Vの範囲外は端の値を使う。
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

void engine::Object3DCommon::CreatePipelineStates() {
    CreatePipelineStatesInternal(false);
}

void engine::Object3DCommon::CreatePipelineStatesInternal(bool titleBlendModesOnly) {
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
    for (size_t i = 0; i < static_cast<size_t>(BlendMode::Count); ++i) {
        // タイトルの軽量準備では不透明と通常合成だけを先に揃え、その他の合成は後の準備へ回します。
        if ((titleBlendModesOnly && i > static_cast<size_t>(BlendMode::Normal)) ||
            graphicsPipelineStates_[i]) {
            continue;
        }

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

    if (!shadowPipelineState_) {
        shadowBuilder.Build(device, shadowPipelineState_.GetAddressOf());
    }

    // 草の薄い葉が大きな深度バイアスで地面の裏へ消えない専用設定。
    shadowBuilder.SetDepthBias(128, 0.0f, 0.35f);

    if (!foliageShadowPipelineState_) {
        shadowBuilder.Build(device, foliageShadowPipelineState_.GetAddressOf());
    }
}

void engine::Object3DCommon::SetShadowPipelineState(bool foliage) {
    // 葉の影だけ深度バイアスを弱め、薄い面が地面の裏へ消えるのを防ぐ。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetPipelineState(foliage ? foliageShadowPipelineState_.Get() : shadowPipelineState_.Get());
}

void engine::Object3DCommon::SetShadowGraphicsCommand() {
    // 影パスは色ターゲットを持たないため、通常描画とは別のルート構成を使う。
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(shadowRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void engine::Object3DCommon::CreateShadowRootSignature() {
    RootSignatureBuilder builder;


    // [0] WVP行列 (CBV b0 - VertexShader用)
    builder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [1] ボーン情報 (DescriptorTable t0 - VertexShader用)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [2,3] 通常描画と同じ風・踏み分けを影にも反映する。
    builder.AddCbv(6, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    builder.AddConstants(7, 32, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // ビルド実行
    builder.Build(dxCommon_->GetDevice(), shadowRootSignature_.GetAddressOf());
}

void engine::Object3DCommon::CreateLocalFogPipeline() {
    auto device = dxCommon_->GetDevice();

    // =================================================================
    // 1. ルートシグネチャの作成
    // =================================================================
    RootSignatureBuilder rsBuilder;

    // [0] b0: WVP行列 (Model::DrawShadow と一致させる)
    rsBuilder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [1] t1: ボーン情報 (Model::DrawShadow がここに書き込んでくるので空けておく！)
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // [2] t0: デプステクスチャ (ピクセルシェーダー用)
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [3] b1: ローカルフォグ設定
    rsBuilder.AddCbv(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

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

    // 半透明合成設定 (BlendMode::Normal と同じ設定を流用)
    psoBuilder.SetBlendMode(BlendMode::Normal);

    // 霧の奥行きはピクセルシェーダーがシーン深度を読み取って決めるため、固定機能の深度テストと書き込みを無効にする。
    psoBuilder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO);

    // レンダーターゲット (デプスバッファは使わないので DSVFormat は UNKNOWN にする)
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    psoBuilder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_UNKNOWN);

    psoBuilder.Build(device, localFogPipelineState_.GetAddressOf());
}

void engine::Object3DCommon::SetLocalFogGraphicsCommand() {
    // 深度から霧量を再構成する専用パスへ切り替える。
    EnsurePipelineState(localFogPipelineState_, &engine::Object3DCommon::CreateLocalFogPipeline);
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(localFogRootSignature_.Get());
    commandList->SetPipelineState(localFogPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// ==========================================================
//  エフェクト用コマンドのセット
// ==========================================================
// 段階準備を省いた場合は初回描画でルートとPSOを用意し、指定した合成方式の状態を選ぶ。
void engine::Object3DCommon::SetEffectGraphicsCommand(BlendMode blendMode) {
    if (!effectPipelineStates_[static_cast<size_t>(blendMode)]) {
        if (!effectRootSignature_) {
            CreateEffectRootSignature();
        }

        CreateEffectPipeline();
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(effectRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 指定されたブレンドモードのPSOをセット
    commandList->SetPipelineState(effectPipelineStates_[static_cast<size_t>(blendMode)].Get());
}

// ==========================================================
//  エフェクト用ルートシグネチャの構築
// ==========================================================
void engine::Object3DCommon::CreateEffectRootSignature() {
    RootSignatureBuilder builder;

    // パラメータ0: CBV (b0, 頂点シェーダー用) -> ViewProjection
    builder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // パラメータ1: 頂点側のb1を予約する。現在の描画経路はWVPを渡し、Effect3d.VSでは参照しない。
    builder.AddCbv(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // パラメータ2: CBV (b0, ピクセルシェーダー用) -> EffectMaterial (色・UVスクロール時間など)
    builder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

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
void engine::Object3DCommon::CreateEffectPipeline() {
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
    // ここでは深度書き込みを無効にします。下のSetBlendModeが合成方式ごとの書き込み設定を上書きします。
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO);

    // レンダーターゲットの設定
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);


    for (size_t i = 0; i < static_cast<size_t>(BlendMode::Count); ++i) {
        if (effectPipelineStates_[i]) continue;
        BlendMode mode = static_cast<BlendMode>(i);

        // ビルダーに現在のループのブレンドモードをセット
        psoBuilder.SetBlendMode(mode);

        // 配列の該当インデックスにPSOをビルドして保存
        psoBuilder.Build(dxCommon_->GetDevice(), effectPipelineStates_[i].GetAddressOf());
    }
}

// 水材質のPSOと共有ルートを、現在のコマンドリストへ束縛します。
void engine::Object3DCommon::SetWaterGraphicsCommand() {
    specialMaterialPipelines_->BindWater();
}

void engine::Object3DCommon::SetMagmaGraphicsCommand() {
    specialMaterialPipelines_->BindMagma();
}

void engine::Object3DCommon::SetIceGraphicsCommand() {
    specialMaterialPipelines_->BindIce();
}

void engine::Object3DCommon::SetFireGraphicsCommand() {
    specialMaterialPipelines_->BindFire();
}

void engine::Object3DCommon::SetLaserGraphicsCommand() {
    specialMaterialPipelines_->BindLaser();
}

void engine::Object3DCommon::SetSlimeGelGraphicsCommand() {
    specialMaterialPipelines_->BindSlimeGel();
}

void engine::Object3DCommon::SetShockwaveGraphicsCommand() {
    specialMaterialPipelines_->BindShockwave();
}

void engine::Object3DCommon::SetLiquidContactGraphicsCommand() {
    specialMaterialPipelines_->BindLiquidContact();
}

void engine::Object3DCommon::SetDamageCrackGraphicsCommand() {
    specialMaterialPipelines_->BindDamageCrack();
}

void engine::Object3DCommon::SetUpdraftGraphicsCommand() {
    specialMaterialPipelines_->BindUpdraft();
}

void engine::Object3DCommon::SetStunBindGraphicsCommand() {
    specialMaterialPipelines_->BindStunBind();
}

void engine::Object3DCommon::SetCrownUnlockGraphicsCommand() {
    specialMaterialPipelines_->BindCrownUnlock();
}

void engine::Object3DCommon::SetPoisonSporeGraphicsCommand() {
    specialMaterialPipelines_->BindPoisonSpore();
}

void engine::Object3DCommon::SetCloudGraphicsCommand() {
    specialMaterialPipelines_->BindCloud();
}

void engine::Object3DCommon::SetGatePortalGraphicsCommand() {
    specialMaterialPipelines_->BindGatePortal();
}

void engine::Object3DCommon::SetWindOrbGraphicsCommand() {
    specialMaterialPipelines_->BindWindOrb();
}

void engine::Object3DCommon::SetVisibilitySilhouetteGraphicsCommand() {
    specialMaterialPipelines_->BindVisibilitySilhouette();
}

void engine::Object3DCommon::CreateSkyboxPipeline() {
    // 背景はシーン側で不透明物より先に描く。後から配置物を重ねられるよう、深度テストと書き込みは無効にする。
    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCbv(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rsBuilder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rsBuilder.Build(dxCommon_->GetDevice(), skyboxRootSignature_.GetAddressOf());
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/skybox/Skybox.VS.hlsl", L"vs_6_0");
    auto psBlob = dxCommon_->CompileShader(L"Resources/shader/skybox/Skybox.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder pipelineBuilder;
    pipelineBuilder.SetRootSignature(skyboxRootSignature_.Get());
    pipelineBuilder.SetShaders(vsBlob.Get(), psBlob.Get());

    pipelineBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE);

    pipelineBuilder.SetBlendMode(BlendMode::None);

    pipelineBuilder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS);

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    pipelineBuilder.SetInputLayout(inputElements, _countof(inputElements));

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    pipelineBuilder.SetRenderTargets(1, &rtvFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);
    pipelineBuilder.Build(dxCommon_->GetDevice(), skyboxPipelineState_.GetAddressOf());
}

