#include "GPUParticleManager.h"
#include "SRVManager.h"
#include <d3dcompiler.h>
#include <cassert>
#include <fstream>
#include <filesystem>
#include "json.hpp"
#include "DebugConsole.h"
#include"WinApp.h"
#include <TextureManager.h>
    
using json = nlohmann::json;
namespace fs = std::filesystem;


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

	// =========================================================
	// 5. 描画用のダミーバーテックスバッファの作成 (頂点シェーダーでインスタンスIDを得るためだけのもの)
	// =========================================================
    dummyVertexBuffer_ = dxCommon_->CreateBufferResource(64); // とりあえず64バイト
    Vector3 dummyPos = { 0.0f, 0.0f, 0.0f };
    void* mapped = nullptr;
    dummyVertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, &dummyPos, sizeof(Vector3));
    dummyVertexBuffer_->Unmap(0, nullptr);


    // 6. ダミーボーンバッファの作成 (クラッシュ防止)
    dummyBoneBuffer_ = dxCommon_->CreateBufferResource(sizeof(Matrix4x4));
    Matrix4x4 identityMat = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };
    void* boneMapped = nullptr;
    dummyBoneBuffer_->Map(0, nullptr, &boneMapped);
    memcpy(boneMapped, &identityMat, sizeof(Matrix4x4));
    dummyBoneBuffer_->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC boneSrvDesc{};
    boneSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    boneSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    boneSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    boneSrvDesc.Buffer.FirstElement = 0;
    boneSrvDesc.Buffer.NumElements = 1;
    boneSrvDesc.Buffer.StructureByteStride = sizeof(Matrix4x4);
    boneSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    dummyBoneSrvIndex_ = SRVManager::GetInstance()->Allocate();
    SRVManager::GetInstance()->CreateSRVforResource(dummyBoneSrvIndex_, dummyBoneBuffer_.Get(), boneSrvDesc);
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

    D3D12_DESCRIPTOR_RANGE boneRange{};
    boneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    boneRange.NumDescriptors = 1;
    boneRange.BaseShaderRegister = 1; // t1
    // [4] 深度テクスチャ (t2)
    D3D12_DESCRIPTOR_RANGE depthRange{};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = 1;
    depthRange.BaseShaderRegister = 2; // t2
    D3D12_ROOT_PARAMETER rootParams[5] = {};
    // [0] パーティクル配列 (UAV)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &uavRange;

    // [1] 時間データ (CBV)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0; // b0 に割り当て

	// [2] 描画用にSRVも渡しておく（Computeシェーダー内で読み取るため）
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[2].Descriptor.ShaderRegister = 0; // t0

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &boneRange;

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[4].DescriptorTable.pDescriptorRanges = &depthRange;

    D3D12_STATIC_SAMPLER_DESC samplerDesc{};
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; // 深度値は補間せず生データを使う
    samplerDesc.ShaderRegister = 0; // s0


    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 5; 
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;         
    rsDesc.pStaticSamplers = &samplerDesc; 

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
    // ★ 魔法の1行：時間を歪める（スローモーション対応）
    float scaledDeltaTime = deltaTime * timeScale_;

    totalTime_ += scaledDeltaTime;
    configData_->deltaTime = scaledDeltaTime; // GPUにもスケールされた時間を送る
    configData_->time = totalTime_;

    // =======================================================
    // ★修正: 私の省略のせいで消えてしまっていたパラメータ送信処理を復元！
    // =======================================================
    configData_->gravity = envGravity_;
    configData_->drag = envDrag_;
    configData_->wind = envWind_;
    configData_->turbulence = envTurbulence_;
    configData_->baseSize = baseSize_;
    configData_->endSize = endSize_;
    configData_->rotSpeedVariance = rotSpeed_;
    configData_->endColor = endColor_;

    configData_->emitCount = emitCountThisFrame_;

    // =======================================================
    // ★ オートエミッター（連続発生）の更新処理
    // =======================================================
    for (auto& emitter : autoEmitters_) {
        // 保存されているプリセットの設定を読み取る
        auto it = presets_.find(emitter.presetName);
        if (it != presets_.end()) {
            // エディタで「連続発生」にチェックが入っていれば実行！
            if (it->second.isLooping) {
                emitter.timer += scaledDeltaTime;
                // エディタで設定した「発生間隔（Interval）」を超えたら発生！
                if (emitter.timer >= it->second.emitInterval) {
                    Emit(emitter.presetName, emitter.position, emitter.transform);
                    emitter.timer = 0.0f; // タイマーリセット
                }
            }
        }
    }
}
void GPUParticleManager::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t textureHandle, uint32_t depthSrvHandle) {
    if (!commandList) return;

    // =======================================================
    // 1. まず裏側で、パーティクルの物理演算 (Compute) を実行する！
    // =======================================================
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);
    commandList->SetPipelineState(computePipelineState_.Get());
    commandList->SetComputeRootSignature(computeRootSignature_.Get());
    SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 0, uavIndex_);

    // =======================================================
    //  コリジョン用のカメラデータなどをCSConfigに書き込む
    // =======================================================
    static Math math;
    Matrix4x4 vp = math.Multiply(viewMatrix, projectionMatrix);
    configData_->viewProj = vp;
    configData_->inverseViewProj = math.Inverse(vp);
    configData_->screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };

    commandList->SetComputeRootConstantBufferView(1, configBuffer_->GetGPUVirtualAddress());

    // =======================================================
    // ★ メッシュの頂点バッファを Compute Shader (t0) に渡す！
    // =======================================================
    configData_->meshVertexCount = emitterVertexBuffer_ ? emitterVertexCount_ : 1;
    configData_->meshVertexStride = emitterVertexBuffer_ ? emitterVertexStride_ : sizeof(Vector3);

    ID3D12Resource* targetVB = emitterVertexBuffer_ ? emitterVertexBuffer_ : dummyVertexBuffer_.Get();

 
    commandList->SetComputeRootShaderResourceView(2, targetVB->GetGPUVirtualAddress());

    uint32_t boneSrv = (emitterBoneSrvIndex_ > 0) ? emitterBoneSrvIndex_ : dummyBoneSrvIndex_;
    SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 3, boneSrv);

    // =======================================================
    // [4] コリジョン判定用の深度テクスチャ (t2) をセット！
    // =======================================================
    if (depthSrvHandle > 0) {
        SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 4, depthSrvHandle);
    }

    // 計算の号令！
    UINT groupCountX = (kMaxParticles + 255) / 256;
    commandList->Dispatch(groupCountX, 1, 1);

    // 計算が終わったら、次のフレームのためにCPU側の発生カウントをリセット
    emitCountThisFrame_ = 0;

    // =======================================================
    // 2. 計算結果を使って、画面に描画 (Graphics) する！
    // =======================================================
    Matrix4x4 billboard = math.Inverse(viewMatrix);
    billboard.m[3][0] = 0.0f; billboard.m[3][1] = 0.0f; billboard.m[3][2] = 0.0f;

    // --- 定数バッファの更新 ---
    cameraData_->viewProj = vp; // Computeで計算したvpを使い回します
    cameraData_->billboardMatrix = billboard;
    cameraData_->projection = projectionMatrix;

    // ソフトパーティクルの馴染み幅をセット
    cameraData_->softParticleFade = softParticleFade_;

    // ディストーション用のパラメータ（モード・画面サイズ）をセット
    cameraData_->blendMode = static_cast<int>(blendMode_);
    cameraData_->screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };

    // 描画前に、バッファを「読み取り専用(SRV)モード」に切り替える
    D3D12_RESOURCE_BARRIER toSRV{};
    toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSRV.Transition.pResource = particleBuffer_.Get();
    toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &toSRV);

    // --- パイプラインのセット ---
    // 加算かそれ以外（半透明・歪み）かで切り替え
    if (blendMode_ == BlendMode::kAdd) {
        commandList->SetPipelineState(graphicsPipelineStateAdd_.Get());
    } else {
        commandList->SetPipelineState(graphicsPipelineStateAlpha_.Get());
    }

    commandList->SetGraphicsRootSignature(graphicsRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // --- 各ルートパラメータ（Slot）にデータをセット ---
    // Slot 0: パーティクルデータ (StructuredBuffer)
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 0, srvIndex_);

    // Slot 1: カメラ定数バッファ (CBV)
    commandList->SetGraphicsRootConstantBufferView(1, cameraBuffer_->GetGPUVirtualAddress());

    uint32_t texHandleToUse = (currentTextureHandle_ > 0) ? currentTextureHandle_ : textureHandle;
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, texHandleToUse);

    // Slot 3: 深度テクスチャ (t2)
    if (depthSrvHandle > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    }

    // Slot 4: 背景コピーテクスチャ (t3)
    uint32_t grabSrvHandle = dxCommon_->GetGrabSrvHandle();
    if (grabSrvHandle > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, grabSrvHandle);
    }

    // 描画の号令！
    commandList->DrawInstanced(4, kMaxParticles, 0, 0);

    // 描き終わったら、次のUpdate(Compute)のために「書き込み(UAV)モード」に戻す
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
	// 深度テクスチャ用のSRVも追加
    D3D12_DESCRIPTOR_RANGE srvRangeDepth{};
    srvRangeDepth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRangeDepth.NumDescriptors = 1;
    srvRangeDepth.BaseShaderRegister = 2; // t2: 深度テクスチ
    D3D12_DESCRIPTOR_RANGE srvRangeGrab{};
    srvRangeGrab.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRangeGrab.NumDescriptors = 1;
    srvRangeGrab.BaseShaderRegister = 3; // ★ t3: 背景コピー

    D3D12_ROOT_PARAMETER rootParams[5] = {};
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
	// [3] 深度テクスチャ (t2)
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &srvRangeDepth;

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[4].DescriptorTable.pDescriptorRanges = &srvRangeGrab;
    // 静的サンプラー (s0: テクスチャのピクセル補間用)
    D3D12_STATIC_SAMPLER_DESC samplerDesc{};
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 5;
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
    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = DirectXCommon::GetInstance()->CompileShader(L"Resources/shader/GPUParticleVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = DirectXCommon::GetInstance()->CompileShader(L"Resources/shader/GPUParticlePS.hlsl", L"ps_6_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = graphicsRootSignature_.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // ラスタライザ (両面描画)
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

    // 深度テスト (奥のものは描画しないが、パーティクル自体は深度を書き込まない)
    psoDesc.DepthStencilState.DepthEnable = FALSE; // TRUE から FALSE に変更！
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // 頂点レイアウト (シェーダー側で生成するので空っぽでOK)
    psoDesc.InputLayout.pInputElementDescs = nullptr;
    psoDesc.InputLayout.NumElements = 0;

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
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

// ====================================================================
// ★ 起動時に指定フォルダのパーティクルJSONをすべて読み込む！
// ====================================================================
void GPUParticleManager::LoadAllPresets(const std::string& directoryPath) {
    if (!fs::exists(directoryPath)) {
        fs::create_directories(directoryPath); // フォルダがなければ作る
        return;
    }

    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (entry.path().extension() == ".json") {
            std::string filename = entry.path().stem().string(); // ".json"を抜いた名前 (例: "Explosion")
            std::ifstream file(entry.path());
            if (file.is_open()) {
                json j;
                file >> j;

                GPUParticleConfig config;
                // --- JSONからデータを復元 ---
                if (j.contains("emitPos")) { config.emitPos.x = j["emitPos"][0]; config.emitPos.y = j["emitPos"][1]; config.emitPos.z = j["emitPos"][2]; }
                if (j.contains("emitArea")) { config.emitArea.x = j["emitArea"][0]; config.emitArea.y = j["emitArea"][1]; config.emitArea.z = j["emitArea"][2]; }
                if (j.contains("emitVelocity")) { config.emitVelocity.x = j["emitVelocity"][0]; config.emitVelocity.y = j["emitVelocity"][1]; config.emitVelocity.z = j["emitVelocity"][2]; }
                if (j.contains("emitCount")) config.emitCount = j["emitCount"];
                if (j.contains("emitLife")) config.emitLife = j["emitLife"];
                if (j.contains("velocityVariance")) config.velocityVariance = j["velocityVariance"];

                if (j.contains("baseColor")) { config.baseColor.x = j["baseColor"][0]; config.baseColor.y = j["baseColor"][1]; config.baseColor.z = j["baseColor"][2]; config.baseColor.w = j["baseColor"][3]; }
                if (j.contains("endColor")) { config.endColor.x = j["endColor"][0]; config.endColor.y = j["endColor"][1]; config.endColor.z = j["endColor"][2]; config.endColor.w = j["endColor"][3]; }
                if (j.contains("baseSize")) config.baseSize = j["baseSize"];
                if (j.contains("endSize")) config.endSize = j["endSize"];
                if (j.contains("rotSpeed")) config.rotSpeed = j["rotSpeed"];
                if (j.contains("blendModeIndex")) config.blendModeIndex = j["blendModeIndex"];

                if (j.contains("envGravity")) { config.envGravity.x = j["envGravity"][0]; config.envGravity.y = j["envGravity"][1]; config.envGravity.z = j["envGravity"][2]; }
                if (j.contains("envDrag")) config.envDrag = j["envDrag"];
                if (j.contains("envWind")) { config.envWind.x = j["envWind"][0]; config.envWind.y = j["envWind"][1]; config.envWind.z = j["envWind"][2]; }
                if (j.contains("envTurbulence")) config.envTurbulence = j["envTurbulence"];

                if (j.contains("isLooping")) config.isLooping = j["isLooping"];
                if (j.contains("emitInterval")) config.emitInterval = j["emitInterval"];
                if (j.contains("shapeType")) config.shapeType = j["shapeType"];
                if (j.contains("shapeRadius")) config.shapeRadius = j["shapeRadius"];
                if (j.contains("shapeAngle")) config.shapeAngle = j["shapeAngle"];
                if (j.contains("midColor")) {
                    config.midColor.x = j["midColor"][0];
                    config.midColor.y = j["midColor"][1];
                    config.midColor.z = j["midColor"][2];
                    config.midColor.w = j["midColor"][3];
                }
                if (j.contains("colorMidTime")) config.colorMidTime = j["colorMidTime"];
                if (j.contains("midSize")) config.midSize = j["midSize"];
                if (j.contains("sizeMidTime")) config.sizeMidTime = j["sizeMidTime"];
                if (j.contains("softParticleFade")) config.softParticleFade = j["softParticleFade"];
                if (j.contains("sizeEaseType")) config.sizeEaseType = j["sizeEaseType"];
                if (j.contains("colorEaseType")) config.colorEaseType = j["colorEaseType"];
                if(j.contains("enableCollision")) config.enableCollision = j["enableCollision"];
                if (j.contains("restitution")) config.restitution = j["restitution"];
                if (j.contains("colorIntensity")) config.colorIntensity = j["colorIntensity"];
                if (j.contains("texturePath")) config.texturePath = j["texturePath"];
                // 辞書に登録！
                presets_[filename] = config;
                DebugConsole::GetInstance()->AddLog("Loaded Particle Preset: " + filename);
            }
        }
    }
}

// ====================================================================
// ★ ゲームシステム用: 名前と座標を渡すだけでパーティクル発生！
// ====================================================================
void GPUParticleManager::Emit(const std::string& presetName, const Vector3& position, const Matrix4x4& emitterWorldMatrix) {
    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        GPUParticleConfig config = it->second;
        config.emitPos = position;
        config.emitterWorldMatrix = emitterWorldMatrix; 
        EmitFromConfig(config);
    }
}
// ====================================================================
// ★ エディタ＆システム共通: Configデータを元にパーティクル発生！
// ====================================================================
void GPUParticleManager::EmitFromConfig(const GPUParticleConfig& config) {
    configData_->startIndex = currentParticleIndex_;
    configData_->emitPos = config.emitPos;
    configData_->emitArea = config.emitArea;
    configData_->emitVelocity = config.emitVelocity;
    configData_->emitLife = config.emitLife;
    configData_->velocityVariance = config.velocityVariance;
    configData_->baseColor = config.baseColor;

    // 現在の仕様に合わせて、マネージャーの内部変数もConfigで上書き
    envGravity_ = config.envGravity;
    envDrag_ = config.envDrag;
    envWind_ = config.envWind;
    envTurbulence_ = config.envTurbulence;
    baseSize_ = config.baseSize;
    endSize_ = config.endSize;
    rotSpeed_ = config.rotSpeed;
    endColor_ = config.endColor;
    blendMode_ = static_cast<BlendMode>(config.blendModeIndex);
    configData_->shapeType = static_cast<uint32_t>(config.shapeType);
    configData_->shapeRadius = config.shapeRadius;
    configData_->shapeAngle = config.shapeAngle;
    configData_->midColor = config.midColor;
    configData_->colorMidTime = config.colorMidTime;

    configData_->midSize = config.midSize;
    configData_->sizeMidTime = config.sizeMidTime;
    emitCountThisFrame_ += config.emitCount;
    softParticleFade_ = config.softParticleFade;
    configData_->sizeEaseType = config.sizeEaseType;
    configData_->colorEaseType = config.colorEaseType;
    configData_->emitterWorldMatrix = config.emitterWorldMatrix;
    configData_->enableCollision = config.enableCollision;
    configData_->restitution = config.restitution;
    configData_->colorIntensity = config.colorIntensity;
    SetCurrentTexture(config.texturePath);
    currentParticleIndex_ = (currentParticleIndex_ + config.emitCount) % kMaxParticles;
}

void GPUParticleManager::SetCurrentTexture(const std::string& path) {
  
    uint32_t handle = TextureManager::GetInstance()->GetSrvHandle(path);

    if (handle > 0) {
        currentTextureHandle_ = handle;
    }
}

uint32_t GPUParticleManager::PlayAutoEmitter(const std::string& presetName, const Vector3& position) {
    Matrix4x4 identity = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };
    return PlayAutoEmitter(presetName, position, identity);
}

uint32_t GPUParticleManager::PlayAutoEmitter(const std::string& presetName, const Vector3& position, const Matrix4x4& transform) {
    AutoEmitter em;
    em.id = nextAutoEmitterId_++;
    em.presetName = presetName;
    em.position = position;
    em.transform = transform;
    em.timer = 0.0f;
    autoEmitters_.push_back(em);

    // 登録した瞬間にまずは1回目を出す！
    Emit(presetName, position, transform);
    return em.id;
}

void GPUParticleManager::StopAutoEmitter(uint32_t id) {
    autoEmitters_.erase(std::remove_if(autoEmitters_.begin(), autoEmitters_.end(),
        [id](const AutoEmitter& e) { return e.id == id; }), autoEmitters_.end());
}

void GPUParticleManager::ClearAllAutoEmitters() {
    autoEmitters_.clear();
}

