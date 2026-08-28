#include "PostEffect.h"
#include "SRVManager.h"
#include"WinApp.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h"
#include "engine/graphics/core/ColorSpace.h"
#include "LightManager.h"
#include "RenderStats.h"
#include "TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
PostEffect::Params MakeNeutralPostEffectParams(float time) {
    PostEffect::Params params{};
    params.time = time;
    params.threshold = 1.0f;
    params.bloomIntensity = 0.0f;
    params.spread = 1.0f;
    params.enableToneMapping = ColorSpace::WorkflowSettings::GetInstance().IsLinearWorkflowEnabled() ? 1 : 0;
    params.vignetteIntensity = 0.0f;
    params.chromaticAberration = 0.0f;
    params.filmGrainIntensity = 0.0f;
    params.vignettePower = 1.0f;
    params.radialCenterX = 0.5f;
    params.radialCenterY = 0.5f;
    params.radialIntensity = 0.0f;
    params.radialBlurSamples = 1;
    params.lutIntensity = 0.0f;
    params.colorExposure = 0.0f;
    params.colorContrast = 1.0f;
    params.colorSaturation = 1.0f;
    params.colorTemperature = 0.0f;
    params.colorTint = 0.0f;
    params.damageFlash = 0.0f;
    params.cinemaBarHeight = 0.0f;
    params.wobbleIntensity = 0.0f;
    params.scanlineIntensity = 0.0f;
    params.mosaicSize = 0.0f;
    params.dangerVignette = 0.0f;
    params.blackout = 0.0f;
    params.grayscaleIntensity = 0.0f;
    params.sepiaIntensity = 0.0f;
    params.boxFilterSize = 0;
    params.gaussianFilterSize = 0;
    params.gaussianSigma = 1.0f;
    params.luminanceOutlineIntensity = 0.0f;
    params.depthOutlineIntensity = 0.0f;
    params.dissolveThreshold = 0.0f;
    params.dissolveEdgeWidth = 0.02f;
    params.randomIntensity = 0.0f;
    params.linearWorkflowEnabled = ColorSpace::WorkflowSettings::GetInstance().IsLinearWorkflowEnabled() ? 1.0f : 0.0f;
    params.dissolveEdgeColor = { 1.0f, 0.4f, 0.3f };
    params.projectionInverse = Math::MakeIdentity4x4();
    params.slimeFadeIntensity = 0.0f;
    params.slimeDensity = 1.0f;
    params.slimeColor = { 0.18f, 0.8f, 0.44f };
    params.irisFadeIntensity = 0.0f;
    params.irisCenterX = 0.5f;
    params.irisCenterY = 0.5f;
    return params;
}
}

void PostEffect::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    renderWidth_ = WinApp::kClientWidth;
    renderHeight_ = WinApp::kClientHeight;
    renderTextures_.resize(8);
    CreateConstBuffer();
    CreateRootSignature();
    CreatePipelineState();
	// [0] シーン描画用 (HDR / 元サイズ)
    CreateRenderTexture(0, WinApp::kClientWidth, WinApp::kClientHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
    // [1] トーンマップ後・ImGui表示用 (SDR / 元サイズ)
    CreateRenderTexture(1, WinApp::kClientWidth, WinApp::kClientHeight,  DXGI_FORMAT_R16G16B16A16_FLOAT);

    // [2] 高輝度抽出用 (HDR / 1/2サイズで少し軽くする)
    CreateRenderTexture(2, WinApp::kClientWidth / 2, WinApp::kClientHeight / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // [3] 縮小ブラー用1 (HDR / 1/4サイズ)
    CreateRenderTexture(3, WinApp::kClientWidth / 4, WinApp::kClientHeight / 4, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // [4] 縮小ブラー用2 (HDR / 1/8サイズ)
    CreateRenderTexture(4, WinApp::kClientWidth / 8, WinApp::kClientHeight / 8, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // [5] 縮小ブラー用3 (HDR / 1/16サイズ)
    CreateRenderTexture(5, WinApp::kClientWidth / 16, WinApp::kClientHeight / 16, DXGI_FORMAT_R16G16B16A16_FLOAT);
    ResizeCameraPreviewTextures();
}

void PostEffect::Update(float deltaTime) {
    LightManager::GetInstance()->UpdateEnvironmentProfile(deltaTime);
    // 時間を進める（ノイズのアニメーション用）
    paramsData_->time += deltaTime;
    paramsData_->linearWorkflowEnabled =
        ColorSpace::WorkflowSettings::GetInstance().IsLinearWorkflowEnabled() ? 1.0f : 0.0f;
}

bool PostEffect::SetLUTTexturePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    const uint32_t handle = TextureManager::GetInstance()->Load(
        path,
        TextureManager::TextureColorSpace::SRGB);
    if (handle == 0) {
        return false;
    }
    lutSrvHandle_ = handle;
    lutTexturePath_ = path;
    return true;
}
void PostEffect::ResetToNeutral() {
    if (!paramsData_) {
        return;
    }

    const float currentTime = paramsData_->time;
    *paramsData_ = MakeNeutralPostEffectParams(currentTime);
}

void PostEffect::SetBloomQuality(BloomQuality quality) {
    switch (quality) {
    case BloomQuality::Low:
    case BloomQuality::Medium:
    case BloomQuality::High:
        bloomQuality_ = quality;
        break;
    default:
        bloomQuality_ = BloomQuality::High;
        break;
    }
}

bool PostEffect::IsBloomActive() const {
    constexpr float kBloomIntensityEpsilon = 0.0001f;
    return bloomEnabled_ && paramsData_ && paramsData_->bloomIntensity > kBloomIntensityEpsilon;
}

int PostEffect::GetBloomLevelCount() const {
    switch (bloomQuality_) {
    case BloomQuality::Low: return 1;
    case BloomQuality::Medium: return 2;
    case BloomQuality::High: return 4;
    default: return 4;
    }
}

int PostEffect::GetExpectedPostEffectPassCount() const {
    if (!IsBloomActive()) {
        return 1;
    }
    return GetBloomLevelCount() * 2 + 2;
}

void PostEffect::SetCameraPreviewResolutionScale(float scale) {
    const float clampedScale = std::clamp(scale, 0.25f, 1.0f);
    if (std::abs(cameraPreviewResolutionScale_ - clampedScale) < 0.0001f) {
        return;
    }

    cameraPreviewResolutionScale_ = clampedScale;
    if (dxCommon_ && renderTextures_.size() >= 8 && renderWidth_ > 0 && renderHeight_ > 0) {
        ResizeCameraPreviewTextures();
    }
}

void PostEffect::ResizeRenderTextures(int width, int height) {
    if (!dxCommon_ || renderTextures_.size() < 8) {
        return;
    }

    width = (std::max)(width, 16);
    height = (std::max)(height, 16);
    renderWidth_ = width;
    renderHeight_ = height;

    // 画面サイズ変更時は、ポストエフェクト用RTも同じタイミングで作り直す。
    CreateRenderTexture(0, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRenderTexture(1, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRenderTexture(2, (std::max)(width / 2, 1), (std::max)(height / 2, 1), DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRenderTexture(3, (std::max)(width / 4, 1), (std::max)(height / 4, 1), DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRenderTexture(4, (std::max)(width / 8, 1), (std::max)(height / 8, 1), DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRenderTexture(5, (std::max)(width / 16, 1), (std::max)(height / 16, 1), DXGI_FORMAT_R16G16B16A16_FLOAT);
    ResizeCameraPreviewTextures();
}

void PostEffect::ResizeCameraPreviewTextures() {
    if (!dxCommon_ || renderTextures_.size() < 8 || renderWidth_ <= 0 || renderHeight_ <= 0) {
        return;
    }

    // プレビューは表示側と同じ16:9に固定し、縦横比による映像の伸びを防ぎます。
    constexpr float kPreviewAspect = 16.0f / 9.0f;
    int width = (std::max)(static_cast<int>(renderWidth_ * cameraPreviewResolutionScale_), 16);
    int height = (std::max)(static_cast<int>(renderHeight_ * cameraPreviewResolutionScale_), 16);
    if (static_cast<float>(width) / static_cast<float>(height) > kPreviewAspect) {
        width = (std::max)(static_cast<int>(height * kPreviewAspect), 16);
    }
    else {
        height = (std::max)(static_cast<int>(width / kPreviewAspect), 16);
    }

    cameraPreviewWidth_ = width;
    cameraPreviewHeight_ = height;
    CreateRenderTexture(kCameraPreviewTextureIndex, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRenderTexture(kCinematicCameraPreviewTextureIndex, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
}


void PostEffect::CreateRootSignature() {
    RootSignatureBuilder builder;

    // =================================================================
    // 1. ルートパラメータの設定
    // =================================================================

    // [0] 定数バッファ (CBV b0 - PixelShader用)
    builder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [1] メイン画像 (DescriptorTable t0 - PixelShader用)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [2] LUT画像 (DescriptorTable t1 - PixelShader用)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [3] 深度テクスチャ (DescriptorTable t2 - PixelShader用)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // [4] ノイズテクスチャ (DescriptorTable t3 - PixelShader用)
    builder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    // =================================================================
    // 2. サンプラーの設定
    // =================================================================

    // s0: テクスチャサンプラー (リニア補間、クランプ - PixelShader用)
    builder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    // s1: ポイントサンプラー (ポイント補間、クランプ - PixelShader用)
    builder.AddStaticSampler(1, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    // =================================================================
    // 3. ビルド実行
    // =================================================================
    builder.Build(dxCommon_->GetDevice(), rootSignature_.GetAddressOf());
}

void PostEffect::CreatePipelineState() {
    ID3D12Device* device = dxCommon_->GetDevice();

    // ★ VSは1つ、PSは用途に合わせて5つコンパイルする！
    auto vsBlob = dxCommon_->CompileShader(L"Resources/shader/postprocess/PostEffect.VS.hlsl", L"vs_6_0", L"main");
    auto psCopy = dxCommon_->CompileShader(L"Resources/shader/postprocess/PostEffect.PS.hlsl", L"ps_6_0", L"mainCopy");
    auto psExtract = dxCommon_->CompileShader(L"Resources/shader/postprocess/PostEffect.PS.hlsl", L"ps_6_0", L"mainExtract");
    auto psDownsample = dxCommon_->CompileShader(L"Resources/shader/postprocess/PostEffect.PS.hlsl", L"ps_6_0", L"mainDownsample");
    auto psAdd = dxCommon_->CompileShader(L"Resources/shader/postprocess/PostEffect.PS.hlsl", L"ps_6_0", L"mainAdd");
    auto psComposite = dxCommon_->CompileShader(L"Resources/shader/postprocess/PostEffect.PS.hlsl", L"ps_6_0", L"mainComposite");

    // ==========================================================
    // --- 共通設定 ---
    // ==========================================================
    GraphicsPipelineBuilder builder;
    builder.SetRootSignature(rootSignature_.Get());
    builder.SetInputLayout(nullptr, 0); // 頂点バッファレス方式に変更！

    // カリングなし、Zテストなし
    builder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);
    builder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO);

    // HDR出力 (16bit Float)
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    builder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_UNKNOWN);

    // 基本はブレンドなし（不透明描画/上書き）
    builder.SetBlendMode(::BlendMode::kNone);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

    // ==========================================================
    // [PSO 0] Copy用 (HDR出力)
    // ==========================================================
    builder.SetShaders(vsBlob.Get(), psCopy.Get());
    builder.Build(device, pso.ReleaseAndGetAddressOf());
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 1] トーンマップ・最終合成用 (SDR出力)
    // ==========================================================
    builder.SetShaders(vsBlob.Get(), psComposite.Get());
    builder.Build(device, pso.ReleaseAndGetAddressOf());
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 2] 抽出用 (HDR出力)
    // ==========================================================
    builder.SetShaders(vsBlob.Get(), psExtract.Get());
    builder.Build(device, pso.ReleaseAndGetAddressOf());
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 3] 縮小ブラー用 (HDR出力)
    // ==========================================================
    builder.SetShaders(vsBlob.Get(), psDownsample.Get());
    builder.Build(device, pso.ReleaseAndGetAddressOf());
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 4] 加算合成用 (HDR出力)
    // ==========================================================
    builder.SetShaders(vsBlob.Get(), psAdd.Get());
    builder.SetBlendMode(::BlendMode::kAdd); 
    builder.Build(device, pso.ReleaseAndGetAddressOf());
    pipelineStates_.push_back(pso);

    // バックバッファはsRGB形式なので、HDR中間ターゲットとは別のPSOを使用する。
    const DXGI_FORMAT backBufferFormat[] = { dxCommon_->GetRTVFormat() };
    builder.SetRenderTargets(1, backBufferFormat, DXGI_FORMAT_UNKNOWN);
    builder.SetBlendMode(::BlendMode::kNone);
    builder.SetShaders(vsBlob.Get(), psComposite.Get());
    builder.Build(device, pso.ReleaseAndGetAddressOf());
    pipelineStates_.push_back(pso);
}

// ==========================================================
// ★ リソースバリア（テクスチャの状態切り替え）関数
// ==========================================================
void PostEffect::TransitionToRTV(ID3D12GraphicsCommandList* commandList, int texIndex) {
    RenderTexture& rt = renderTextures_[texIndex];
    if (rt.currentState == D3D12_RESOURCE_STATE_RENDER_TARGET) return; // 既にRTVなら何もしない

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = rt.resource.Get();
    barrier.Transition.StateBefore = rt.currentState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);
    rt.currentState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

void PostEffect::TransitionToSRV(ID3D12GraphicsCommandList* commandList, int texIndex) {
    RenderTexture& rt = renderTextures_[texIndex];
    if (rt.currentState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) return; // 既にSRVなら何もしない

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = rt.resource.Get();
    barrier.Transition.StateBefore = rt.currentState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
    rt.currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}


void PostEffect::PreDrawScene(ID3D12GraphicsCommandList* commandList, int targetTexIndex, bool clear) {
    // 指定されたテクスチャを描画先(RTV)に切り替える
    TransitionToRTV(commandList, targetTexIndex);

    RenderTexture& rt = renderTextures_[targetTexIndex];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    // ★ 引数が true の時だけ画面をクリアする (加算合成のときはクリアしない)
    if (clear) {
        float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    }

    // ★ テクスチャ自身のサイズを取得して、ビューポート(描画範囲)を合わせる！
    D3D12_RESOURCE_DESC resDesc = rt.resource->GetDesc();
    float width = (float)resDesc.Width;
    float height = (float)resDesc.Height;

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)resDesc.Width, (LONG)resDesc.Height };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
}

void PostEffect::PreDrawSceneWithDepth(ID3D12GraphicsCommandList* commandList, int targetTexIndex, bool clear) {
    TransitionToRTV(commandList, targetTexIndex);

    RenderTexture& rt = renderTextures_[targetTexIndex];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = rt.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

    if (clear) {
        const Vector4& sceneClearColor = LightManager::GetInstance()->GetSceneClearColor();
        float clearColor[] = {
            sceneClearColor.x,
            sceneClearColor.y,
            sceneClearColor.z,
            sceneClearColor.w
        };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }

    D3D12_RESOURCE_DESC resDesc = rt.resource->GetDesc();
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)resDesc.Width, (float)resDesc.Height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)resDesc.Width, (LONG)resDesc.Height };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    ID3D12DescriptorHeap* descriptorHeaps[] = { SRVManager::GetInstance()->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
}

bool PostEffect::BeginCameraPreviewSpecialPass(ID3D12GraphicsCommandList* commandList, int targetTexIndex) {
    if (!commandList || targetTexIndex < 0 || targetTexIndex >= static_cast<int>(renderTextures_.size())) {
        return false;
    }

    RenderTexture& rt = renderTextures_[targetTexIndex];
    if (!rt.resource || !rt.depthResource || !rt.grabResource ||
        !rt.rtvHeap || rt.depthSrvHandle == 0 || rt.grabSrvHandle == 0) {
        return false;
    }

    commandList->OMSetRenderTargets(0, nullptr, false, nullptr);

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = rt.resource.Get();
    barriers[0].Transition.StateBefore = rt.currentState;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = rt.grabResource.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    commandList->ResourceBarrier(2, barriers);

    commandList->CopyResource(rt.grabResource.Get(), rt.resource.Get());

    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(2, barriers);
    rt.currentState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    if (rt.depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = rt.depthResource.Get();
        depthBarrier.Transition.StateBefore = rt.depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &depthBarrier);
        rt.depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
    return true;
}

void PostEffect::EndCameraPreviewSpecialPass(ID3D12GraphicsCommandList* commandList, int targetTexIndex) {
    if (!commandList || targetTexIndex < 0 || targetTexIndex >= static_cast<int>(renderTextures_.size())) {
        return;
    }

    RenderTexture& rt = renderTextures_[targetTexIndex];
    if (!rt.resource || !rt.depthResource || !rt.rtvHeap || !rt.dsvHeap) {
        return;
    }

    if (rt.depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = rt.depthResource.Get();
        depthBarrier.Transition.StateBefore = rt.depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        commandList->ResourceBarrier(1, &depthBarrier);
        rt.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = rt.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
}

void PostEffect::Draw(ID3D12GraphicsCommandList* commandList, uint32_t srvHandle, int psoIndex) {
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineStates_[psoIndex].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // 3頂点で全画面を覆う
    // commandList->IASetVertexBuffers() はいらない！
    commandList->SetGraphicsRootConstantBufferView(0, constBuffer_->GetGPUVirtualAddress());

    // [1] t0 のセット (メイン画像)
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 1, srvHandle);

    // [2] t1 のセット (LUT画像) 
    uint32_t currentLutHandle = (lutSrvHandle_ > 0) ? lutSrvHandle_ : srvHandle;
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, currentLutHandle);

    // [3] t2 のセット (深度テクスチャ)
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, dxCommon_->GetDepthSrvHandle());

    // [4] t3 のセット (ノイズテクスチャ)
    uint32_t currentNoiseHandle = (noiseSrvHandle_ > 0) ? noiseSrvHandle_ : srvHandle;
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, currentNoiseHandle);

    commandList->DrawInstanced(3, 1, 0, 0); // 3頂点に変更
    RenderStats::GetInstance()->RecordNonIndexedDraw(3, 1, 1);
    RenderStats::GetInstance()->RecordPostProcessPass();
}
void PostEffect::CreateConstBuffer() {
    // 定数バッファは256バイトアラインメントが必要
    constBuffer_ = dxCommon_->CreateBufferResource((sizeof(Params) + 0xff) & ~0xff);
    constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
    *paramsData_ = MakeNeutralPostEffectParams(0.0f);
}

void PostEffect::CreateRenderTexture(int texIndex, int width, int height, DXGI_FORMAT format) {
    ID3D12Device* device = dxCommon_->GetDevice();

    RenderTexture& rt = renderTextures_[texIndex];
    const bool needsPreviewSampling =
        texIndex == kCameraPreviewTextureIndex ||
        texIndex == kCinematicCameraPreviewTextureIndex;

    // 1. リソース設定 
    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Width = width;
    resDesc.Height = height;
    resDesc.MipLevels = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.Format = format; // ★ HDRフォーマットが渡される
    resDesc.SampleDesc.Count = 1;
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    // クリアカラー (少し暗めのグレーなど)
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = format;
    clearValue.Color[0] = 0.1f; clearValue.Color[1] = 0.1f; clearValue.Color[2] = 0.1f; clearValue.Color[3] = 1.0f;

    // リソース生成
    D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
    Microsoft::WRL::ComPtr<ID3D12Resource> newResource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&newResource)
    );
    if (FAILED(hr) || !newResource) {
        return;
    }

    // 2. RTV (描画先としての設定)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newRtvHeap;
    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&newRtvHeap));
    if (FAILED(hr) || !newRtvHeap) {
        return;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(newResource.Get(), &rtvDesc, newRtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.DepthOrArraySize = 1;
    // DSVとSRVの両方から参照するため、Resource本体はTypelessで作成します。
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClearValue = {};
    depthClearValue.Format = dxCommon_->GetDSVFormat();
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.DepthStencil.Stencil = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> newDepthResource;
    hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue,
        IID_PPV_ARGS(&newDepthResource)
    );
    if (FAILED(hr) || !newDepthResource) {
        return;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newDsvHeap;
    hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&newDsvHeap));
    if (FAILED(hr) || !newDsvHeap) {
        return;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = dxCommon_->GetDSVFormat();
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(newDepthResource.Get(), &dsvDesc, newDsvHeap->GetCPUDescriptorHandleForHeapStart());

    if (needsPreviewSampling) {
        D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
        depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MipLevels = 1;
        depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (rt.depthSrvHandle != 0) {
            SRVManager::GetInstance()->CreateSRVforResource(rt.depthSrvHandle, newDepthResource.Get(), depthSrvDesc);
        }
        else {
            rt.depthSrvHandle = SRVManager::GetInstance()->CreateSRV(newDepthResource.Get(), depthSrvDesc);
        }
    }

    // 3. SRV (画像としての設定)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    // SRVManagerを使ってインデックスを取得
    if (rt.srvHandle != 0) {
        SRVManager::GetInstance()->CreateSRVforResource(rt.srvHandle, newResource.Get(), srvDesc);
    } else {
        rt.srvHandle = SRVManager::GetInstance()->CreateSRV(newResource.Get(), srvDesc);
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> newGrabResource;
    if (needsPreviewSampling) {
        // 特殊マテリアル用の背景コピーはCamera Previewの2枚だけに確保します。
        D3D12_RESOURCE_DESC grabDesc = resDesc;
        grabDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &grabDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&newGrabResource));
        if (FAILED(hr) || !newGrabResource) {
            return;
        }
        if (rt.grabSrvHandle != 0) {
            SRVManager::GetInstance()->CreateSRVforResource(rt.grabSrvHandle, newGrabResource.Get(), srvDesc);
        }
        else {
            rt.grabSrvHandle = SRVManager::GetInstance()->CreateSRV(newGrabResource.Get(), srvDesc);
        }
    }

    rt.resource = newResource;
    rt.rtvHeap = newRtvHeap;
    rt.depthResource = newDepthResource;
    rt.dsvHeap = newDsvHeap;
    rt.grabResource = newGrabResource;
    rt.currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    rt.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}
