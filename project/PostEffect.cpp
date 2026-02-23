#include "PostEffect.h"
#include "SRVManager.h"
#include"WinApp.h"
#include <cassert>

void PostEffect::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    renderTextures_.resize(6);
    CreateMesh();
    CreateConstBuffer();
    CreateRootSignature();
    CreatePipelineState();
	// [0] シーン描画用 (HDR / 元サイズ)
    CreateRenderTexture(0, WinApp::kClientWidth, WinApp::kClientHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
    // [1] トーンマップ後・ImGui表示用 (SDR / 元サイズ)
    CreateRenderTexture(1, WinApp::kClientWidth, WinApp::kClientHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    // [2] 高輝度抽出用 (HDR / 1/2サイズで少し軽くする)
    CreateRenderTexture(2, WinApp::kClientWidth / 2, WinApp::kClientHeight / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // [3] 縮小ブラー用1 (HDR / 1/4サイズ)
    CreateRenderTexture(3, WinApp::kClientWidth / 4, WinApp::kClientHeight / 4, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // [4] 縮小ブラー用2 (HDR / 1/8サイズ)
    CreateRenderTexture(4, WinApp::kClientWidth / 8, WinApp::kClientHeight / 8, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // [5] 縮小ブラー用3 (HDR / 1/16サイズ)
    CreateRenderTexture(5, WinApp::kClientWidth / 16, WinApp::kClientHeight / 16, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void PostEffect::CreateMesh() {
    // 画面全体を覆う板ポリゴン（Zは0）
    VertexPosUv vertices[] = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}}, // 左下
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f}}, // 左上
        {{ 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}}, // 右下
        {{ 1.0f,  1.0f, 0.0f}, {1.0f, 0.0f}}, // 右上
    };

    vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(vertices));
    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(VertexPosUv);

    void* mappedData = nullptr;
    vertexBuffer_->Map(0, nullptr, &mappedData);
    memcpy(mappedData, vertices, sizeof(vertices));
    vertexBuffer_->Unmap(0, nullptr);
}

void PostEffect::CreateRootSignature() {
    ID3D12Device* device = dxCommon_->GetDevice();

    // t0 用 (メイン画像)
    D3D12_DESCRIPTOR_RANGE descriptorRange0[1] = {};
    descriptorRange0[0].BaseShaderRegister = 0; // t0
    descriptorRange0[0].NumDescriptors = 1;
    descriptorRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ★追加：t1 用 (LUT画像)
    D3D12_DESCRIPTOR_RANGE descriptorRange1[1] = {};
    descriptorRange1[0].BaseShaderRegister = 1; // t1
    descriptorRange1[0].NumDescriptors = 1;
    descriptorRange1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ★配列のサイズを 2 から 3 に変更！
    D3D12_ROOT_PARAMETER rootParams[3] = {};

    // [0] CBV (定数バッファ b0)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].Descriptor.ShaderRegister = 0;

    // [1] t0 (メイン画像)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.pDescriptorRanges = descriptorRange0;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;

    // [2] t1 (LUT画像) ★追加
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].DescriptorTable.pDescriptorRanges = descriptorRange1;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;

    // サンプラー (画像の色を拾う設定)
    D3D12_STATIC_SAMPLER_DESC samplerDesc{};
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.ShaderRegister = 0; // s0
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParams;
    rootSignatureDesc.NumParameters = _countof(rootParams); // 自動で 3 になる
    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.NumStaticSamplers = 1;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob, errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); assert(false); }

    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}
void PostEffect::CreatePipelineState() {
    ID3D12Device* device = dxCommon_->GetDevice();

    // ★ VSは1つ、PSは用途に合わせて5つコンパイルする！
    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shader/PostEffect.VS.hlsl", L"vs_6_0", L"main");
    Microsoft::WRL::ComPtr<IDxcBlob> psCopy = dxCommon_->CompileShader(L"Resources/shader/PostEffect.PS.hlsl", L"ps_6_0", L"mainCopy");
    Microsoft::WRL::ComPtr<IDxcBlob> psExtract = dxCommon_->CompileShader(L"Resources/shader/PostEffect.PS.hlsl", L"ps_6_0", L"mainExtract");
    Microsoft::WRL::ComPtr<IDxcBlob> psDownsample = dxCommon_->CompileShader(L"Resources/shader/PostEffect.PS.hlsl", L"ps_6_0", L"mainDownsample");
    Microsoft::WRL::ComPtr<IDxcBlob> psAdd = dxCommon_->CompileShader(L"Resources/shader/PostEffect.PS.hlsl", L"ps_6_0", L"mainAdd");
    Microsoft::WRL::ComPtr<IDxcBlob> psComposite = dxCommon_->CompileShader(L"Resources/shader/PostEffect.PS.hlsl", L"ps_6_0", L"mainComposite");

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // --- 共通設定 ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // 基本はアルファブレンド（不透明描画）
    D3D12_RENDER_TARGET_BLEND_DESC defaultBlend{};
    defaultBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0] = defaultBlend;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

    // ==========================================================
    // [PSO 0] Copy用 (HDR出力)
    // ==========================================================
    psoDesc.PS = { psCopy->GetBufferPointer(), psCopy->GetBufferSize() };
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)); assert(SUCCEEDED(hr));
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 1] トーンマップ・最終合成用 (SDR出力)
    // ==========================================================
    psoDesc.PS = { psComposite->GetBufferPointer(), psComposite->GetBufferSize() };
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // ★これだけSDR(ImGui用)
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)); assert(SUCCEEDED(hr));
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 2] 抽出用 (HDR出力)
    // ==========================================================
    psoDesc.PS = { psExtract->GetBufferPointer(), psExtract->GetBufferSize() };
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)); assert(SUCCEEDED(hr));
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 3] 縮小ブラー用 (HDR出力)
    // ==========================================================
    psoDesc.PS = { psDownsample->GetBufferPointer(), psDownsample->GetBufferSize() };
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)); assert(SUCCEEDED(hr));
    pipelineStates_.push_back(pso);

    // ==========================================================
    // [PSO 4] 加算合成用 (HDR出力 / ★ブレンドモードを加算にする)
    // ==========================================================
    psoDesc.PS = { psAdd->GetBufferPointer(), psAdd->GetBufferSize() };

    // 加算ブレンド設定 (元の色 + 新しい色)
    D3D12_RENDER_TARGET_BLEND_DESC addBlend{};
    addBlend.BlendEnable = TRUE;
    addBlend.SrcBlend = D3D12_BLEND_ONE;
    addBlend.DestBlend = D3D12_BLEND_ONE; // ここがONEなので加算される！
    addBlend.BlendOp = D3D12_BLEND_OP_ADD;
    addBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    addBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    addBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    addBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0] = addBlend;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)); assert(SUCCEEDED(hr));
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
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(0, constBuffer_->GetGPUVirtualAddress());

    // [1] t0 のセット (メイン画像)
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 1, srvHandle);

    // [2] t1 のセット (LUT画像) 
    uint32_t currentLutHandle = (lutSrvHandle_ > 0) ? lutSrvHandle_ : srvHandle;
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, currentLutHandle);

    commandList->DrawInstanced(4, 1, 0, 0);
}
void PostEffect::CreateConstBuffer() {
    // 定数バッファは256バイトアラインメントが必要
    constBuffer_ = dxCommon_->CreateBufferResource((sizeof(Params) + 0xff) & ~0xff);
    constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
    // 初期値セット
    paramsData_->threshold = 0.8f;
    paramsData_->bloomIntensity = 2.0f;
    paramsData_->spread = 2.0f;
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