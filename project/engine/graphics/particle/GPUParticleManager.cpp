#include "GPUParticleManager.h"
#include "SRVManager.h"
#include <d3dcompiler.h>
#include <cassert>

#pragma comment(lib, "d3dcompiler.lib")

GPUParticleManager* GPUParticleManager::GetInstance() {
    static GPUParticleManager instance;
    return &instance;
}

void GPUParticleManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;

    // 1. バッファ（UAV & 定数バッファ）の作成
    CreateBuffer();

    // 2. Compute Shader 用のパイプライン作成
    CreateComputePipeline();
    CreateGraphicsPipeline();
}

void GPUParticleManager::CreateBuffer() {
    auto device = dxCommon_->GetDevice();

    // =========================================================
    // 1. パーティクル用バッファ (UAV) の作成
    // =========================================================
    // UAVとして使うため、ヒープタイプは DEFAULT にする
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1; // 念のため明示的に1にする(安全対策)
    heapProps.VisibleNodeMask = 1;  // 念のため明示的に1にする(安全対策)

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = sizeof(Particle) * kMaxParticles;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // ★ここが超重要：Unordered Access (読み書き) を許可する！
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&particleBuffer_)
    );
    assert(SUCCEEDED(hr));

    // =========================================================
    // 2. UAV（計算用）と SRV（描画用）のビューを作成
    // =========================================================
    uavIndex_ = SRVManager::GetInstance()->Allocate();
    srvIndex_ = SRVManager::GetInstance()->Allocate();

    // UAVの作成
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = kMaxParticles;
    uavDesc.Buffer.StructureByteStride = sizeof(Particle);

    // SRVManager に CreateUAVforResource が無い場合は、直接デバイスから作成します
    device->CreateUnorderedAccessView(
        particleBuffer_.Get(),
        nullptr,
        &uavDesc,
        SRVManager::GetInstance()->GetCPUDescriptorHandle(uavIndex_)
    );

    // SRVの作成 (後で描画する時に使う)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = kMaxParticles;
    srvDesc.Buffer.StructureByteStride = sizeof(Particle);
    SRVManager::GetInstance()->CreateSRVforResource(srvIndex_, particleBuffer_.Get(), srvDesc);

    // =========================================================
    // 3. Time送信用 定数バッファの作成
    // =========================================================
    configBuffer_ = dxCommon_->CreateBufferResource(sizeof(CSConfig));
    configBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&configData_));
    configData_->deltaTime = 0.0f;
    configData_->time = 0.0f;
    configData_->emitCount = 0; // 最初は出さない
	// =========================================================
	// 4. カメラデータ用 定数バッファの作成
	// =========================================================
    cameraBuffer_ = dxCommon_->CreateBufferResource(sizeof(CameraData));
    cameraBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
}

void GPUParticleManager::CreateComputePipeline() {
    auto device = dxCommon_->GetDevice();

    // =========================================================
    // 1. ルートシグネチャ (Compute用) の作成
    // =========================================================
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0; // u0 に割り当て

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    // [0] パーティクル配列 (UAV)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &uavRange;

    // [1] 時間データ (CBV)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0; // b0 に割り当て

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters = rootParams;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false);
    }
    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature_));

    // =========================================================
    // 2. Compute Shader のコンパイルと PSO の作成
    // =========================================================
    Microsoft::WRL::ComPtr<ID3DBlob> csBlob;
    hr = D3DCompileFromFile(L"Resources/shader/ParticleCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0", 0, 0, &csBlob, &errorBlob);
    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false);
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = computeRootSignature_.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePipelineState_));
    assert(SUCCEEDED(hr));
}

void GPUParticleManager::Update(float deltaTime) {
    totalTime_ += deltaTime;
    configData_->deltaTime = deltaTime;
    configData_->time = totalTime_;
     configData_->gravity = envGravity_;
     configData_->drag = envDrag_;
     configData_->wind = envWind_;
     configData_->turbulence = envTurbulence_;
    // CPU側で設定するだけ（GPUへの送信はDrawで行う）
    configData_->emitCount = emitCountThisFrame_;
}
void GPUParticleManager::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t textureHandle) {
    if (!commandList) return;

    // =======================================================
    // 1. まず裏側で、10万個のパーティクルの物理演算 (Compute) を実行する！
    // =======================================================
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);

    // ★ ここは「計算用」のパイプラインをセットする！
    commandList->SetPipelineState(computePipelineState_.Get());
    commandList->SetComputeRootSignature(computeRootSignature_.Get());

    SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 0, uavIndex_);
    commandList->SetComputeRootConstantBufferView(1, configBuffer_->GetGPUVirtualAddress());

    // 計算の号令！
    UINT groupCountX = (kMaxParticles + 255) / 256;
    commandList->Dispatch(groupCountX, 1, 1);

    // 計算が終わったら、次のフレームのためにCPU側の発生カウントをリセット
    emitCountThisFrame_ = 0;

    // =======================================================
    // 2. 計算結果を使って、画面に描画 (Graphics) する！
    // =======================================================
    static Math math;
    Matrix4x4 billboard = math.Inverse(viewMatrix);
    billboard.m[3][0] = 0.0f; billboard.m[3][1] = 0.0f; billboard.m[3][2] = 0.0f;

    cameraData_->viewProj = math.Multiply(viewMatrix, projectionMatrix);
    cameraData_->billboardMatrix = billboard;

    // 描画前に、バッファを「読み取り専用(SRV)モード」に切り替える（これがComputeとGraphicsの境界線！）
    D3D12_RESOURCE_BARRIER toSRV{};
    toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSRV.Transition.pResource = particleBuffer_.Get();
    toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &toSRV);

    // --- いつもの描画処理 ---

    // ★ ここが修正ポイント！描画用のパイプラインをブレンドモードで切り替える！
    if (blendMode_ == BlendMode::kAdd) {
        commandList->SetPipelineState(graphicsPipelineStateAdd_.Get());
    } else {
        commandList->SetPipelineState(graphicsPipelineStateAlpha_.Get());
    }

    commandList->SetGraphicsRootSignature(graphicsRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 0, srvIndex_);
    commandList->SetGraphicsRootConstantBufferView(1, cameraBuffer_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, textureHandle);

    // 描画の号令！
    commandList->DrawInstanced(4, kMaxParticles, 0, 0);

    // 描き終わったら、次のUpdate(Compute)のために「書き込み(UAV)モード」に戻す！
    D3D12_RESOURCE_BARRIER toUAV{};
    toUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toUAV.Transition.pResource = particleBuffer_.Get();
    toUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &toUAV);
}
void GPUParticleManager::CreateGraphicsPipeline() {
    auto device = dxCommon_->GetDevice();

    // =========================================================
    // 1. ルートシグネチャ (Graphics用) の作成
    // =========================================================
    D3D12_DESCRIPTOR_RANGE srvRangeParticle{};
    srvRangeParticle.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRangeParticle.NumDescriptors = 1;
    srvRangeParticle.BaseShaderRegister = 0; // t0: パーティクル配列

    D3D12_DESCRIPTOR_RANGE srvRangeTexture{};
    srvRangeTexture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRangeTexture.NumDescriptors = 1;
    srvRangeTexture.BaseShaderRegister = 1; // t1: テクスチャ

    D3D12_ROOT_PARAMETER rootParams[3] = {};
    // [0] パーティクル配列 (t0)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRangeParticle;

    // [1] カメラ定数バッファ (b0)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0;

    // [2] テクスチャ (t1)
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &srvRangeTexture;

    // 静的サンプラー (s0: テクスチャのピクセル補間用)
    D3D12_STATIC_SAMPLER_DESC samplerDesc{};
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &samplerDesc;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signatureBlob, &errorBlob);
    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&graphicsRootSignature_));

    // =========================================================
    // 2. PSO の作成 (共通設定)
    // =========================================================
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    D3DCompileFromFile(L"Resources/shader/GPUParticleVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    D3DCompileFromFile(L"Resources/shader/GPUParticlePS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = graphicsRootSignature_.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // ラスタライザ (両面描画)
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

    // 深度テスト (奥のものは描画しないが、パーティクル自体は深度を書き込まない)
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度書き込みOFF
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // 頂点レイアウト (シェーダー側で生成するので空っぽでOK)
    psoDesc.InputLayout.pInputElementDescs = nullptr;
    psoDesc.InputLayout.NumElements = 0;

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // =========================================================
    // ★ PSO 1: 加算合成 (Additive) - 光、炎、魔法用
    // =========================================================
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE; // ここが ONE だと加算合成
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineStateAdd_));
    assert(SUCCEEDED(hr));

    // =========================================================
    // ★ PSO 2: 半透明合成 (Alpha Blend) - 霧、黒煙、砂埃用
    // =========================================================
    // DestBlend を INV_SRC_ALPHA に変えるだけで、背景を透かす「半透明（アルファブレンド）」になる！
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineStateAlpha_));
    assert(SUCCEEDED(hr));
}

void GPUParticleManager::Emit(const Vector3& pos, const Vector3& area, const Vector3& velocity, uint32_t count, float life, float variance, const Vector4& color) {
    // GPUに送る発生用データをセット
    configData_->startIndex = currentParticleIndex_;



    configData_->emitPos = pos;
    configData_->emitArea = area; // ★超重要: これが抜けていたので範囲が反映されませんでした！
    configData_->emitVelocity = velocity;
    configData_->emitLife = life;
    configData_->velocityVariance = variance;
    configData_->baseColor = color;

    // 発生予定数を加算
    emitCountThisFrame_ += count;

    // 10万個を使い切ったら 0 番目に戻る (リングバッファ)
    currentParticleIndex_ = (currentParticleIndex_ + count) % kMaxParticles;
}