#include "PostEffect.h"
#include "SRVManager.h"
#include"WinApp.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h"
#include <cassert>

void PostEffect::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    renderTextures_.resize(6);
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
}

void PostEffect::Update(float deltaTime) {
    // 時間を進める（ノイズのアニメーション用）
    paramsData_->time += deltaTime;
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
}
void PostEffect::CreateConstBuffer() {
    // 定数バッファは256バイトアラインメントが必要
    constBuffer_ = dxCommon_->CreateBufferResource((sizeof(Params) + 0xff) & ~0xff);
    constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
    // 初期値セット
    paramsData_->threshold = 0.8f;
    paramsData_->bloomIntensity = 2.0f;
    paramsData_->spread = 2.0f;
    paramsData_->slimeFadeIntensity = 0.0f;
    paramsData_->slimeDensity = 1.5f; // 少し密度を上げてディテールを出す
    paramsData_->slimeColor = { 0.1f, 0.9f, 0.2f }; // よりスライムらしい色味へ
    paramsData_->irisFadeIntensity = 0.0f;
    paramsData_->irisCenterX = 0.5f;
    paramsData_->irisCenterY = 0.5f;
}

void PostEffect::CreateRenderTexture(int texIndex, int width, int height, DXGI_FORMAT format) {
    ID3D12Device* device = dxCommon_->GetDevice();

    RenderTexture& rt = renderTextures_[texIndex];

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
    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&rt.resource)
    );
    assert(SUCCEEDED(hr));
    rt.currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // 2. RTV (描画先としての設定)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rt.rtvHeap));
    assert(SUCCEEDED(hr));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(rt.resource.Get(), &rtvDesc, rt.rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // 3. SRV (画像としての設定)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    // SRVManagerを使ってインデックスを取得
    rt.srvHandle = SRVManager::GetInstance()->CreateSRV(rt.resource.Get(), srvDesc);
}