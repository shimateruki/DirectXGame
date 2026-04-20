#include "GPUParticleSystem.h"
#include "SRVManager.h"
#include <d3dcompiler.h>
#include <cassert>
#include "DebugConsole.h"
#include "WinApp.h"
#include "RootSignatureBuilder.h"
#include "GraphicsPipelineBuilder.h"
#include <TextureManager.h>

#pragma comment(lib, "d3dcompiler.lib")

void GPUParticleSystem::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    CreateBuffer();
    CreateComputePipeline();
    CreateGraphicsPipeline();
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
    resDesc.Width = sizeof(Particle) * kMaxParticles;
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

    uavIndex_ = SRVManager::GetInstance()->Allocate();
    srvIndex_ = SRVManager::GetInstance()->Allocate();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = kMaxParticles;
    uavDesc.Buffer.StructureByteStride = sizeof(Particle);
    device->CreateUnorderedAccessView(particleBuffer_.Get(), nullptr, &uavDesc, SRVManager::GetInstance()->GetCPUDescriptorHandle(uavIndex_));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = kMaxParticles;
    srvDesc.Buffer.StructureByteStride = sizeof(Particle);
    SRVManager::GetInstance()->CreateSRVforResource(srvIndex_, particleBuffer_.Get(), srvDesc);

    configBuffer_ = dxCommon_->CreateBufferResource(sizeof(CSConfig) * kMaxEmitRequests);
    configBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&configData_));
    configData_->deltaTime = 0.0f;
    configData_->time = 0.0f;
    configData_->emitCount = 0;

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
}

void GPUParticleSystem::CreateComputePipeline() {
    auto device = dxCommon_->GetDevice();
    RootSignatureBuilder rsBuilder;
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSRV(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_ALL, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    rsBuilder.Build(device, computeRootSignature_.GetAddressOf());

    Microsoft::WRL::ComPtr<ID3DBlob> csBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(L"Resources/shader/ParticleCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0", 0, 0, &csBlob, &errorBlob);
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

void GPUParticleSystem::CreateGraphicsPipeline() {
    auto device = dxCommon_->GetDevice();
    RootSignatureBuilder rsBuilder;
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddCBV(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    rsBuilder.AddStaticSampler(0, 0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    rsBuilder.Build(device, graphicsRootSignature_.GetAddressOf());

    auto vsBlob = DirectXCommon::GetInstance()->CompileShader(L"Resources/shader/GPUParticleVS.hlsl", L"vs_6_0");
    auto psBlob = DirectXCommon::GetInstance()->CompileShader(L"Resources/shader/GPUParticlePS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder.SetRootSignature(graphicsRootSignature_.Get());
    psoBuilder.SetInputLayout(nullptr, 0);
    psoBuilder.SetShaders(vsBlob.Get(), psBlob.Get());
    psoBuilder.SetRasterizerState(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID);
    psoBuilder.SetDepthStencilState(false, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_LESS_EQUAL);
    DXGI_FORMAT rtvFormats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    psoBuilder.SetRenderTargets(1, rtvFormats, DXGI_FORMAT_UNKNOWN);

    psoBuilder.SetBlendMode(::BlendMode::kAdd);
    psoBuilder.Build(device, graphicsPipelineStateAdd_.GetAddressOf());
    psoBuilder.SetBlendMode(::BlendMode::kNormal);
    psoBuilder.Build(device, graphicsPipelineStateAlpha_.GetAddressOf());
}

void GPUParticleSystem::Update(float deltaTime) {
    float scaledDeltaTime = deltaTime * timeScale_;
    totalTime_ += scaledDeltaTime;
    frameDeltaTime_ = scaledDeltaTime;
}

void GPUParticleSystem::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t dummyTex, uint32_t depthSrvHandle) {
    if (!commandList) return;

    // --- Compute Shader ---
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);
    commandList->SetPipelineState(computePipelineState_.Get());
    commandList->SetComputeRootSignature(computeRootSignature_.Get());
    SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 0, uavIndex_);

    static Math math;
    Matrix4x4 vp = math.Multiply(viewMatrix, projectionMatrix);

    if (emitRequests_.empty()) {
        EmitRequest dummy = {};
        dummy.config = lastConfig_;
        dummy.config.emitCount = 0;
        dummy.vb = dummyVertexBuffer_.Get();
        dummy.vCount = 1;
        dummy.vStride = sizeof(Vector3);
        dummy.boneSrv = dummyBoneSrvIndex_;
        emitRequests_.push_back(dummy);
    }
    else {
        lastConfig_ = emitRequests_.back().config;
    }

    for (size_t i = 0; i < emitRequests_.size(); ++i) {
        auto& req = emitRequests_[i];
        req.config.viewProj = vp;
        req.config.inverseViewProj = math.Inverse(vp);
        req.config.screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };
        req.config.deltaTime = (i == 0) ? frameDeltaTime_ : 0.0f;
        req.config.time = totalTime_;
        req.config.meshVertexCount = req.vb ? req.vCount : 1;
        req.config.meshVertexStride = req.vb ? req.vStride : sizeof(Vector3);

        configData_[i] = req.config;
        D3D12_GPU_VIRTUAL_ADDRESS cbvAddress = configBuffer_->GetGPUVirtualAddress() + (i * sizeof(CSConfig));
        commandList->SetComputeRootConstantBufferView(1, cbvAddress);

        ID3D12Resource* targetVB = req.vb ? req.vb : dummyVertexBuffer_.Get();
        commandList->SetComputeRootShaderResourceView(2, targetVB->GetGPUVirtualAddress());

        uint32_t boneSrv = (req.boneSrv > 0) ? req.boneSrv : dummyBoneSrvIndex_;
        SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 3, boneSrv);

        if (depthSrvHandle > 0) {
            SRVManager::GetInstance()->SetComputeRootDescriptorTable(commandList, 4, depthSrvHandle);
        }

        UINT groupCountX = (kMaxParticles + 255) / 256;
        commandList->Dispatch(groupCountX, 1, 1);

        if (i < emitRequests_.size() - 1) {
            D3D12_RESOURCE_BARRIER uavBarrier{};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = particleBuffer_.Get();
            commandList->ResourceBarrier(1, &uavBarrier);
        }
    }

    emitRequests_.clear();
    emitCountThisFrame_ = 0;

    // --- Graphics Shader ---
    Matrix4x4 billboard = math.Inverse(viewMatrix);
    billboard.m[3][0] = 0.0f; billboard.m[3][1] = 0.0f; billboard.m[3][2] = 0.0f;

    cameraData_->viewProj = vp;
    cameraData_->billboardMatrix = billboard;
    cameraData_->projection = projectionMatrix;
    cameraData_->softParticleFade = softParticleFade_;
    cameraData_->blendMode = blendModeIndex_;
    cameraData_->screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };

    D3D12_RESOURCE_BARRIER toSRV{};
    toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSRV.Transition.pResource = particleBuffer_.Get();
    toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &toSRV);

    // パイプラインの切り替え（0:加算、1:半透明、2:歪み）
    if (blendModeIndex_ == 0) {
        commandList->SetPipelineState(graphicsPipelineStateAdd_.Get());
    }
    else {
        commandList->SetPipelineState(graphicsPipelineStateAlpha_.Get());
    }

    commandList->SetGraphicsRootSignature(graphicsRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // =========================================================
    // ★ 修正1: 斬撃エフェクトなどの頂点データを引き継がないようにリセット！
    // =========================================================
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);

    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 0, srvIndex_);
    commandList->SetGraphicsRootConstantBufferView(1, cameraBuffer_->GetGPUVirtualAddress());


    uint32_t texToUse = (currentTextureHandle_ > 0) ? currentTextureHandle_ : dummyTex;
    if (texToUse > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, texToUse);
    }

    if (depthSrvHandle > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    }

    uint32_t grabSrvHandle = dxCommon_->GetGrabSrvHandle();
    if (grabSrvHandle > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, grabSrvHandle);
    }

    commandList->DrawInstanced(4, kMaxParticles, 0, 0);

    D3D12_RESOURCE_BARRIER toUAV{};
    toUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toUAV.Transition.pResource = particleBuffer_.Get();
    toUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &toUAV);
}
void GPUParticleSystem::EmitFromConfig(const GPUParticleConfig& config) {
    if (emitRequests_.size() >= kMaxEmitRequests) return;

    CSConfig reqConfig = {};
    reqConfig.startIndex = currentParticleIndex_;
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

    EmitRequest request;
    request.config = reqConfig;
    request.vb = emitterVertexBuffer_;
    request.vCount = emitterVertexCount_;
    request.vStride = emitterVertexStride_;
    request.boneSrv = emitterBoneSrvIndex_;

    emitRequests_.push_back(request);

    SetCurrentTexture(config.texturePath);
    blendModeIndex_ = config.blendModeIndex;
    softParticleFade_ = config.softParticleFade;

    currentParticleIndex_ = (currentParticleIndex_ + config.emitCount) % kMaxParticles;
}

void GPUParticleSystem::SetCurrentTexture(const std::string& path) {
    uint32_t handle = TextureManager::GetInstance()->GetSrvHandle(path);
    if (handle > 0) {
        currentTextureHandle_ = handle;
    }
}
