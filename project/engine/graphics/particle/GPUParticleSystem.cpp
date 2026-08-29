#include "GPUParticleSystem.h"
#include "RenderStats.h"
#include "SRVManager.h"
#include <d3dcompiler.h>
#include <cassert>
#include "DebugConsole.h"
#include "WinApp.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h"
#include <TextureManager.h>
#include <algorithm>
#include <filesystem>

#pragma comment(lib, "d3dcompiler.lib")

void GPUParticleSystem::Initialize(DirectXCommon* dxCommon, uint32_t maxParticles) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    maxParticles_ = std::clamp(maxParticles, kMinParticles, kMaxParticles);
    CreateBuffer();
    CreateComputePipeline();
}

void GPUParticleSystem::CreateBuffer() {
    auto device = dxCommon_->GetDevice();

    // UAVバッファ作成
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = sizeof(Particle) * maxParticles_;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&particleBuffer_)
    );
    assert(SUCCEEDED(hr));

    resDesc.Width = sizeof(int32_t);
    hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&freeListIndexBuffer_)
    );
    assert(SUCCEEDED(hr));

    resDesc.Width = sizeof(uint32_t) * maxParticles_;
    hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&freeListBuffer_)
    );
    assert(SUCCEEDED(hr));

    hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&aliveParticleIndexBuffer_)
    );
    assert(SUCCEEDED(hr));

    resDesc.Width = sizeof(D3D12_DRAW_ARGUMENTS);
    hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&indirectDrawArgumentBuffer_)
    );
    assert(SUCCEEDED(hr));

    uavIndex_ = SRVManager::GetInstance()->Allocate();
    freeListIndexUav_ = SRVManager::GetInstance()->Allocate();
    freeListUav_ = SRVManager::GetInstance()->Allocate();
    aliveParticleIndexUav_ = SRVManager::GetInstance()->Allocate();
    indirectDrawArgumentUav_ = SRVManager::GetInstance()->Allocate();
    srvIndex_ = SRVManager::GetInstance()->Allocate();
    aliveParticleIndexSrv_ = SRVManager::GetInstance()->Allocate();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(Particle);
    device->CreateUnorderedAccessView(particleBuffer_.Get(), nullptr, &uavDesc, SRVManager::GetInstance()->GetCPUDescriptorHandle(uavIndex_));

    uavDesc.Buffer.NumElements = 1;
    uavDesc.Buffer.StructureByteStride = sizeof(int32_t);
    device->CreateUnorderedAccessView(freeListIndexBuffer_.Get(), nullptr, &uavDesc, SRVManager::GetInstance()->GetCPUDescriptorHandle(freeListIndexUav_));

    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    device->CreateUnorderedAccessView(freeListBuffer_.Get(), nullptr, &uavDesc, SRVManager::GetInstance()->GetCPUDescriptorHandle(freeListUav_));

    device->CreateUnorderedAccessView(aliveParticleIndexBuffer_.Get(), nullptr, &uavDesc, SRVManager::GetInstance()->GetCPUDescriptorHandle(aliveParticleIndexUav_));

    uavDesc.Buffer.NumElements = 4;
    uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    device->CreateUnorderedAccessView(indirectDrawArgumentBuffer_.Get(), nullptr, &uavDesc, SRVManager::GetInstance()->GetCPUDescriptorHandle(indirectDrawArgumentUav_));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = maxParticles_;
    srvDesc.Buffer.StructureByteStride = sizeof(Particle);
    SRVManager::GetInstance()->CreateSRVforResource(srvIndex_, particleBuffer_.Get(), srvDesc);

    srvDesc.Buffer.NumElements = maxParticles_;
    srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    SRVManager::GetInstance()->CreateSRVforResource(aliveParticleIndexSrv_, aliveParticleIndexBuffer_.Get(), srvDesc);

    configBuffer_ = dxCommon_->CreateBufferResource(sizeof(CSConfig) * (kMaxEmitRequests + 1));
    configBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&configData_));
    configData_->deltaTime = 0.0f;
    configData_->time = 0.0f;
    configData_->emitCount = 0;
    configData_->maxParticles = maxParticles_;

    cameraBuffer_ = dxCommon_->CreateBufferResource(sizeof(CameraData));
    cameraBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

    dummyVertexBuffer_ = dxCommon_->CreateBufferResource(64);
    Vector3 dummyPos = { 0.0f, 0.0f, 0.0f };
    void* mapped = nullptr;
    dummyVertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, &dummyPos, sizeof(Vector3));
    dummyVertexBuffer_->Unmap(0, nullptr);

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

    // Texture未指定でも有効なSRVをBindできるよう、白Textureを既定値として読み込みます。
    currentTextureHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/particle/white.png");

    CreateGraphicsPipeline();
}

void GPUParticleSystem::CreateComputePipeline() {
    auto device = dxCommon_->GetDevice();
    RootSignatureBuilder rsBuilder;
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 5, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSRV(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_ALL, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    rsBuilder.Build(device, computeRootSignature_.GetAddressOf());

    Microsoft::WRL::ComPtr<ID3DBlob> csBlobInit;
    Microsoft::WRL::ComPtr<ID3DBlob> csBlobResetDrawArguments;
    Microsoft::WRL::ComPtr<ID3DBlob> csBlobUpdate;
    Microsoft::WRL::ComPtr<ID3DBlob> csBlobEmit;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(L"Resources/shader/particle/ParticleCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "InitCS", "cs_5_0", 0, 0, &csBlobInit, &errorBlob);
    if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); assert(false); }

    hr = D3DCompileFromFile(L"Resources/shader/particle/ParticleCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "ResetDrawArgumentsCS", "cs_5_0", 0, 0, &csBlobResetDrawArguments, &errorBlob);
    if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); assert(false); }

    hr = D3DCompileFromFile(L"Resources/shader/particle/ParticleCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "UpdateCS", "cs_5_0", 0, 0, &csBlobUpdate, &errorBlob);
    if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); assert(false); }

    hr = D3DCompileFromFile(L"Resources/shader/particle/ParticleCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "EmitCS", "cs_5_0", 0, 0, &csBlobEmit, &errorBlob);
    if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); assert(false); }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = computeRootSignature_.Get();

    psoDesc.CS = { csBlobInit->GetBufferPointer(), csBlobInit->GetBufferSize() };
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePipelineStateInit_));
    assert(SUCCEEDED(hr));

    psoDesc.CS = { csBlobResetDrawArguments->GetBufferPointer(), csBlobResetDrawArguments->GetBufferSize() };
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePipelineStateResetDrawArguments_));
    assert(SUCCEEDED(hr));

    psoDesc.CS = { csBlobUpdate->GetBufferPointer(), csBlobUpdate->GetBufferSize() };
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePipelineStateUpdate_));
    assert(SUCCEEDED(hr));

    psoDesc.CS = { csBlobEmit->GetBufferPointer(), csBlobEmit->GetBufferSize() };
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePipelineStateEmit_));
    assert(SUCCEEDED(hr));
}

void GPUParticleSystem::CreateGraphicsPipeline() {
    auto device = dxCommon_->GetDevice();
    RootSignatureBuilder rsBuilder;
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rsBuilder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    rsBuilder.Build(device, graphicsRootSignature_.GetAddressOf());

    auto vsBlob = DirectXCommon::GetInstance()->CompileShader(L"Resources/shader/particle/GPUParticleVS.hlsl", L"vs_6_0");
    auto psBlob = DirectXCommon::GetInstance()->CompileShader(L"Resources/shader/particle/GPUParticlePS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(graphicsRootSignature_.Get());
    psoBuilder.SetInputLayout(nullptr, 0);
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);
    psoBuilder.SetDepthStencilState(true, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    psoBuilder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_UNKNOWN);

    psoBuilder.SetBlendMode(::BlendMode::kAdd);
    psoBuilder.Build(device, graphicsPipelineStateAdd_.GetAddressOf());
    psoBuilder.SetBlendMode(::BlendMode::kNormal);
    psoBuilder.Build(device, graphicsPipelineStateAlpha_.GetAddressOf());
    psoBuilder.Build(device, graphicsPipelineStateDistortion_.GetAddressOf());

    D3D12_INDIRECT_ARGUMENT_DESC argumentDesc{};
    argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc{};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &argumentDesc;
    HRESULT hr = device->CreateCommandSignature(
        &commandSignatureDesc,
        nullptr,
        IID_PPV_ARGS(&indirectDrawCommandSignature_));
    assert(SUCCEEDED(hr));
}

void GPUParticleSystem::Update(float deltaTime) {
    float scaledDeltaTime = deltaTime * timeScale_;
    totalTime_ += scaledDeltaTime;
    frameDeltaTime_ += scaledDeltaTime;

    // 軽量化タイマー更新
    lastEmitTimer_ += scaledDeltaTime;
}

void GPUParticleSystem::RequestSimulationReset() {
    emitRequests_.clear();
    emitCountThisFrame_ = 0;
    lastConfig_ = {};
    totalTime_ = 0.0f;
    frameDeltaTime_ = 0.0f;
    lastEmitTimer_ = 0.0f;
    isInitialized_ = false;
    warmupRequested_ = true;
}

void GPUParticleSystem::ResetForSceneTransition() {
    emitRequests_.clear();
    emitCountThisFrame_ = 0;
    lastConfig_ = {};
    totalTime_ = 0.0f;
    frameDeltaTime_ = 0.0f;
    lastEmitTimer_ = activeLifetimeWindow_ + 0.001f;
    isInitialized_ = false;
    warmupRequested_ = false;
}

void GPUParticleSystem::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t dummyTex, uint32_t depthSrvHandle) {
    if (!commandList) return;

    // 最後に発生した粒子の寿命が終わったSystemは、Computeと描画を完全にスキップします。
    if (lastEmitTimer_ > activeLifetimeWindow_ && !warmupRequested_) {
        frameDeltaTime_ = 0.0f;
        return;
    }
    const bool warmupOnly = warmupRequested_ && emitRequests_.empty();
    const bool rebuiltSimulationThisDraw = !isInitialized_;

    // --- Compute Shader ---
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);
    commandList->SetComputeRootSignature(computeRootSignature_.Get());
    SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 0, uavIndex_);

    if (!isInitialized_) {
        commandList->SetPipelineState(computePipelineStateInit_.Get());
        CSConfig initConfig{};
        initConfig.maxParticles = maxParticles_;
        configData_[0] = initConfig;
        commandList->SetComputeRootConstantBufferView(1, configBuffer_->GetGPUVirtualAddress());
        UINT groupCountInit = (maxParticles_ + 1023) / 1024;
        commandList->Dispatch(groupCountInit, 1, 1);
        RenderStats::GetInstance()->RecordComputeDispatch(groupCountInit);
        
        D3D12_RESOURCE_BARRIER barriers[3] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[0].UAV.pResource = particleBuffer_.Get();
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[1].UAV.pResource = freeListIndexBuffer_.Get();
        barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[2].UAV.pResource = freeListBuffer_.Get();
        commandList->ResourceBarrier(3, barriers);

        isInitialized_ = true;
    }

    if (warmupOnly) {
        warmupRequested_ = false;
        lastEmitTimer_ = activeLifetimeWindow_ + 0.001f;
        // 初回だけ0粒子のIndirect Drawまで通し、実戦前に全GPUリソースを準備します。
    }

    static Math math;
    Matrix4x4 vp = math.Multiply(viewMatrix, projectionMatrix);

    D3D12_RESOURCE_BARRIER resetArgumentBarrier{};
    resetArgumentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    resetArgumentBarrier.UAV.pResource = indirectDrawArgumentBuffer_.Get();
    D3D12_RESOURCE_BARRIER updateBarriers[5] = {};
    updateBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    updateBarriers[0].UAV.pResource = particleBuffer_.Get();
    updateBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    updateBarriers[1].UAV.pResource = freeListIndexBuffer_.Get();
    updateBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    updateBarriers[2].UAV.pResource = freeListBuffer_.Get();
    updateBarriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    updateBarriers[3].UAV.pResource = aliveParticleIndexBuffer_.Get();
    updateBarriers[4].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    updateBarriers[4].UAV.pResource = indirectDrawArgumentBuffer_.Get();

    const auto rebuildAliveParticleList = [&](float simulationDeltaTime) {
        commandList->SetPipelineState(computePipelineStateResetDrawArguments_.Get());
        commandList->Dispatch(1, 1, 1);
        RenderStats::GetInstance()->RecordComputeDispatch(1);
        commandList->ResourceBarrier(1, &resetArgumentBarrier);

        commandList->SetPipelineState(computePipelineStateUpdate_.Get());
        CSConfig updateConfig = lastConfig_;
        updateConfig.deltaTime = simulationDeltaTime;
        updateConfig.time = totalTime_;
        updateConfig.viewProj = vp;
        updateConfig.inverseViewProj = math.Inverse(vp);
        updateConfig.screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };
        updateConfig.maxParticles = maxParticles_;
        configData_[0] = updateConfig;

        commandList->SetComputeRootConstantBufferView(1, configBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(2, dummyVertexBuffer_->GetGPUVirtualAddress());
        const uint32_t updateBoneSrv = (dummyBoneSrvIndex_ > 0) ? dummyBoneSrvIndex_ : 0;
        SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 3, updateBoneSrv);
        if (depthSrvHandle > 0) {
            SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 4, depthSrvHandle);
        } else {
            SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 4, dummyTex);
        }

        const UINT groupCountUpdate = (maxParticles_ + 255) / 256;
        commandList->Dispatch(groupCountUpdate, 1, 1);
        RenderStats::GetInstance()->RecordComputeDispatch(groupCountUpdate);
        commandList->ResourceBarrier(5, updateBarriers);
    };

    // Rebuild the alive list for particles that existed before this frame's emit pass.
    rebuildAliveParticleList(frameDeltaTime_);

    // 3. Emit Pass
    const bool hasEmitRequests = !emitRequests_.empty();
    if (hasEmitRequests) {
        commandList->SetPipelineState(computePipelineStateEmit_.Get());

        for (size_t i = 0; i < emitRequests_.size(); ++i) {
            auto& req = emitRequests_[i];
            req.config.viewProj = vp;
            req.config.inverseViewProj = math.Inverse(vp);
            req.config.screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };
            req.config.deltaTime = 0.0f; // Emit pass does not progress physics
            req.config.initialAge = rebuiltSimulationThisDraw ?
                (std::max)(0.0f, totalTime_ - req.config.time) : 0.0f;
            req.config.meshVertexCount = req.vb ? req.vCount : 1;
            req.config.meshVertexStride = req.vb ? req.vStride : sizeof(Vector3);
            req.config.maxParticles = maxParticles_;

            // Start storing configs at index 1 since index 0 is used by Update Pass
            configData_[i + 1] = req.config;
            D3D12_GPU_VIRTUAL_ADDRESS cbvAddress = configBuffer_->GetGPUVirtualAddress() + ((i + 1) * sizeof(CSConfig));
            commandList->SetComputeRootConstantBufferView(1, cbvAddress);

            ID3D12Resource* targetVB = req.vb ? req.vb : dummyVertexBuffer_.Get();
            commandList->SetComputeRootShaderResourceView(2, targetVB->GetGPUVirtualAddress());

            uint32_t boneSrv = (req.boneSrv > 0) ? req.boneSrv : dummyBoneSrvIndex_;
            SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 3, boneSrv);

            if (depthSrvHandle > 0) {
                SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 4, depthSrvHandle);
            } else {
                SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 4, dummyTex);
            }

            UINT groupCountEmit = (req.config.emitCount + 255) / 256;
            commandList->Dispatch(groupCountEmit, 1, 1);
            RenderStats::GetInstance()->RecordComputeDispatch(groupCountEmit);

            commandList->ResourceBarrier(5, updateBarriers);
        }
        
        lastConfig_ = emitRequests_.back().config;
    }

    // A seek reset initializes and emits in the same frame. Rebuild once more so
    // the newly emitted particles become visible immediately at the scrubbed time.
    if (rebuiltSimulationThisDraw && hasEmitRequests) {
        rebuildAliveParticleList(0.0f);
        warmupRequested_ = false;
    }

    emitRequests_.clear();
    emitCountThisFrame_ = 0;
    frameDeltaTime_ = 0.0f;

    // --- Graphics Shader ---
    Matrix4x4 billboard = math.Inverse(viewMatrix);
    billboard.m[3][0] = 0.0f; billboard.m[3][1] = 0.0f; billboard.m[3][2] = 0.0f;

    cameraData_->viewProj = vp;
    cameraData_->billboardMatrix = billboard;
    cameraData_->projection = projectionMatrix;
    cameraData_->softParticleFade = softParticleFade_;
    cameraData_->blendMode = blendModeIndex_;
    cameraData_->screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };
    cameraData_->spriteSheetColumns = spriteSheetColumns_;
    cameraData_->spriteSheetRows = spriteSheetRows_;
    cameraData_->spriteSheetFrameCount = spriteSheetFrameCount_;
    cameraData_->spriteSheetFps = spriteSheetFps_;
    cameraData_->spriteSheetLoop = spriteSheetLoop_;
    cameraData_->spriteSheetRandomStart = spriteSheetRandomStart_;
    cameraData_->alignToVelocity = alignToVelocity_;
    cameraData_->velocityStretch = velocityStretch_;
    cameraData_->particleType = particleType_;
    cameraData_->trailLength = trailLength_;
    cameraData_->receiveLighting = receiveLighting_;
    cameraData_->lightingStrength = lightingStrength_;
    cameraData_->lightDirection = lightDirection_;
    cameraData_->lightColor = lightColor_;

    D3D12_RESOURCE_BARRIER drawBarriers[3] = {};
    drawBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    drawBarriers[0].Transition.pResource = particleBuffer_.Get();
    drawBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    drawBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    drawBarriers[1].Transition.pResource = aliveParticleIndexBuffer_.Get();
    drawBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    drawBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    drawBarriers[2].Transition.pResource = indirectDrawArgumentBuffer_.Get();
    drawBarriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawBarriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    commandList->ResourceBarrier(3, drawBarriers);

    // パイプラインの切り替え（0:加算、1:半透明、2:歪み）
    if (blendModeIndex_ == 0) {
        commandList->SetPipelineState(graphicsPipelineStateAdd_.Get());
    }
    else if (blendModeIndex_ == 1) {
        commandList->SetPipelineState(graphicsPipelineStateAlpha_.Get());
    }
    else if (blendModeIndex_ == 2) {
        commandList->SetPipelineState(graphicsPipelineStateDistortion_.Get());
    }

    commandList->SetGraphicsRootSignature(graphicsRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // =========================================================
    // Preset切替時に前の形状固有頂点を引き継がないよう、Mesh設定を初期化します。
    // =========================================================
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);

    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 0, srvIndex_);
    commandList->SetGraphicsRootConstantBufferView(1, cameraBuffer_->GetGPUVirtualAddress());


    uint32_t texToUse = (currentTextureHandle_ > 0) ? currentTextureHandle_ : dummyTex;
    if (texToUse > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, texToUse);
    } else {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, dummyTex);
    }

    if (depthSrvHandle > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    } else {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, dummyTex);
    }

    uint32_t grabSrvHandle = dxCommon_->GetGrabSrvHandle();
    if (grabSrvHandle > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, grabSrvHandle);
    } else {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, dummyTex);
    }

    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 5, aliveParticleIndexSrv_);

    commandList->ExecuteIndirect(
        indirectDrawCommandSignature_.Get(),
        1,
        indirectDrawArgumentBuffer_.Get(),
        0,
        nullptr,
        0);
    RenderStats::GetInstance()->RecordNonIndexedIndirectDraw();
    RenderStats::GetInstance()->RecordGpuParticleSystem(maxParticles_);

    for (D3D12_RESOURCE_BARRIER& barrier : drawBarriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    commandList->ResourceBarrier(3, drawBarriers);
}
void GPUParticleSystem::EmitFromConfig(const GPUParticleConfig& config) {
    if (emitRequests_.size() >= kMaxEmitRequests) return;

    // 寿命が2秒を超えるプリセットも途中で描画停止しないよう、活動期間を延長する。
    const float requestedLifetimeWindow = (std::max)(config.emitLife + 0.1f, 2.0f);
    if (lastEmitTimer_ > activeLifetimeWindow_) {
        activeLifetimeWindow_ = requestedLifetimeWindow;
    }
    else {
        activeLifetimeWindow_ = (std::max)(activeLifetimeWindow_, requestedLifetimeWindow);
    }
    lastEmitTimer_ = 0.0f;

    CSConfig reqConfig = {};
    reqConfig.time = totalTime_;
    reqConfig.emitPos = config.emitPos;
    reqConfig.emitArea = config.emitArea;
    reqConfig.emitVelocity = config.emitVelocity;
    reqConfig.emitLife = config.emitLife;
    reqConfig.velocityVariance = config.velocityVariance;
    reqConfig.baseColor = config.baseColor;
    reqConfig.gravity = config.envGravity;
    reqConfig.drag = config.envDrag;
    reqConfig.wind = config.envWind;
    reqConfig.turbulence = config.envTurbulence;
    reqConfig.baseSize = config.baseSize;
    reqConfig.endSize = config.endSize;
    reqConfig.rotSpeedVariance = config.rotSpeed;
    reqConfig.endColor = config.endColor;
    reqConfig.shapeType = static_cast<uint32_t>(config.shapeType);
    reqConfig.shapeRadius = config.shapeRadius;
    reqConfig.shapeAngle = config.shapeAngle;
    reqConfig.midColor = config.midColor;
    reqConfig.colorMidTime = config.colorMidTime;
    reqConfig.midSize = config.midSize;
    reqConfig.sizeMidTime = config.sizeMidTime;
    reqConfig.emitCount = config.emitCount;
    reqConfig.sizeEaseType = config.sizeEaseType;
    reqConfig.colorEaseType = config.colorEaseType;
    reqConfig.emitterWorldMatrix = config.emitterWorldMatrix;
    reqConfig.enableCollision = config.enableCollision;
    reqConfig.restitution = config.restitution;
    reqConfig.colorIntensity = config.colorIntensity;
    reqConfig.maxParticles = maxParticles_;
    reqConfig.fieldType = static_cast<uint32_t>(std::clamp(config.fieldType, 0, 3));
    reqConfig.fieldStrength = config.fieldStrength;
    reqConfig.fieldRadius = (std::max)(config.fieldRadius, 0.001f);
    reqConfig.fieldFalloff = (std::max)(config.fieldFalloff, 0.01f);
    reqConfig.fieldPosition = config.emitPos + config.fieldPosition;

    EmitRequest request;
    request.config = reqConfig;
    request.vb = emitterVertexBuffer_;
    request.vCount = emitterVertexCount_;
    request.vStride = emitterVertexStride_;
    request.boneSrv = emitterBoneSrvIndex_;

    emitRequests_.push_back(request);

    blendModeIndex_ = static_cast<uint32_t>(std::clamp(config.blendModeIndex, 0, 2));
    softParticleFade_ = config.softParticleFade;
    spriteSheetColumns_ = config.spriteSheetColumns > 0 ? static_cast<uint32_t>(config.spriteSheetColumns) : 1u;
    spriteSheetRows_ = config.spriteSheetRows > 0 ? static_cast<uint32_t>(config.spriteSheetRows) : 1u;
    spriteSheetFrameCount_ = config.spriteSheetFrameCount > 0 ? static_cast<uint32_t>(config.spriteSheetFrameCount) : 1u;
    spriteSheetFps_ = config.spriteSheetFps > 0.0f ? config.spriteSheetFps : 0.0f;
    spriteSheetLoop_ = config.spriteSheetLoop != 0 ? 1u : 0u;
    spriteSheetRandomStart_ = config.spriteSheetRandomStart != 0 ? 1u : 0u;
    alignToVelocity_ = config.alignToVelocity != 0 ? 1u : 0u;
    velocityStretch_ = (std::max)(config.velocityStretch, 0.0f);
    particleType_ = config.particleType == 1 ? 1u : 0u;
    trailLength_ = (std::max)(config.trailLength, 0.0f);
    receiveLighting_ = config.receiveLighting != 0 ? 1u : 0u;
    lightingStrength_ = std::clamp(config.lightingStrength, 0.0f, 1.0f);
    lightDirection_ = config.lightDirection;
    lightColor_ = config.lightColor;
}

size_t GPUParticleSystem::GetEstimatedMemoryBytes() const {
    size_t bytes = emitRequests_.capacity() * sizeof(EmitRequest);
    ID3D12Resource* resources[] = {
        particleBuffer_.Get(),
        freeListIndexBuffer_.Get(),
        freeListBuffer_.Get(),
        aliveParticleIndexBuffer_.Get(),
        indirectDrawArgumentBuffer_.Get(),
        configBuffer_.Get(),
        cameraBuffer_.Get(),
        dummyVertexBuffer_.Get(),
        dummyBoneBuffer_.Get()
    };
    for (ID3D12Resource* resource : resources) {
        if (resource) {
            bytes += static_cast<size_t>(resource->GetDesc().Width);
        }
    }
    return bytes;
}

void GPUParticleSystem::SetCurrentTexture(const std::string& path) {
    if (!path.empty()) {
        // 描画ループ中のLoadはGPUクラッシュを引き起こすため、GetSrvHandleを使用
        uint32_t handle = TextureManager::GetInstance()->GetSrvHandle(path);
        if (handle == 0 && std::filesystem::exists(path)) {
            TextureManager::GetInstance()->Load(path);
            handle = TextureManager::GetInstance()->GetSrvHandle(path);
        }
        if (handle > 0) {
            currentTextureHandle_ = handle;
        } else {
            // ロードされていない場合は、安全な白画像またはparticle画像を使う
            uint32_t fallback = TextureManager::GetInstance()->GetSrvHandle("Resources/sprite/particle/particle.png");
            if (fallback == 0) fallback = TextureManager::GetInstance()->GetSrvHandle("Resources/sprite/particle/white.png");
            if (fallback > 0) currentTextureHandle_ = fallback;
        }
    }
}
