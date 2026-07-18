#include "PrimitiveDrawer.h"
#include "RenderStats.h"
#include "DirectXCommon.h"
#include "CameraManager.h"
#include <cassert>

PrimitiveDrawer::~PrimitiveDrawer() {
    Finalize();
}

void PrimitiveDrawer::Initialize(DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;
    assert(dxCommon_);
    ID3D12Device* device = dxCommon_->GetDevice();
    HRESULT hr;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; params[1].Descriptor.ShaderRegister = 1;
    rsDesc.pParameters = params; rsDesc.NumParameters = _countof(params);
    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) { OutputDebugStringA((char*)errBlob->GetBufferPointer()); assert(false); }
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&primitiveRootSignature_)); assert(SUCCEEDED(hr));

    D3D12_INPUT_ELEMENT_DESC inputElems[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
    D3D12_INPUT_LAYOUT_DESC inputLayout = { inputElems, _countof(inputElems) };
    D3D12_RASTERIZER_DESC rasterDesc{}; rasterDesc.CullMode = D3D12_CULL_MODE_NONE; rasterDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shader/debug/DebugPrimitive.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shader/debug/DebugPrimitive.PS.hlsl", L"ps_6_0");
    assert(vsBlob && psBlob);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = primitiveRootSignature_.Get(); psoDesc.InputLayout = inputLayout;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() }; psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState = rasterDesc; psoDesc.NumRenderTargets = 1; psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.SampleDesc.Count = 1; psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;
    D3D12_DEPTH_STENCIL_DESC depthDesc = dxCommon_->GetDefaultDepthStencilDesc();
    depthDesc.DepthEnable = TRUE;
    psoDesc.DepthStencilState = depthDesc;
    psoDesc.DSVFormat = dxCommon_->GetDSVFormat();

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&primitivePipelineState_)); assert(SUCCEEDED(hr));

    Vector4 cubeVerts[] = { {-0.5f,-0.5f,-0.5f,1}, {0.5f,-0.5f,-0.5f,1}, {-0.5f,0.5f,-0.5f,1}, {0.5f,0.5f,-0.5f,1}, {-0.5f,-0.5f,0.5f,1}, {0.5f,-0.5f,0.5f,1}, {-0.5f,0.5f,0.5f,1}, {0.5f,0.5f,0.5f,1} };
    uint32_t cubeIdx[] = { 0,1, 1,3, 3,2, 2,0, 4,5, 5,7, 7,6, 6,4, 0,4, 1,5, 2,6, 3,7 };
    cubeVertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(cubeVerts)); cubeVertexBufferView_ = { cubeVertexBuffer_->GetGPUVirtualAddress(), sizeof(cubeVerts), sizeof(Vector4) };
    void* vbData; hr = cubeVertexBuffer_->Map(0, nullptr, &vbData); assert(SUCCEEDED(hr)); memcpy(vbData, cubeVerts, sizeof(cubeVerts)); cubeVertexBuffer_->Unmap(0, nullptr);
    cubeIndexBuffer_ = dxCommon_->CreateBufferResource(sizeof(cubeIdx)); cubeIndexBufferView_ = { cubeIndexBuffer_->GetGPUVirtualAddress(), sizeof(cubeIdx), DXGI_FORMAT_R32_UINT };
    void* ibData; hr = cubeIndexBuffer_->Map(0, nullptr, &ibData); assert(SUCCEEDED(hr)); memcpy(ibData, cubeIdx, sizeof(cubeIdx)); cubeIndexBuffer_->Unmap(0, nullptr);

    primitiveColorBuffer_ = dxCommon_->CreateBufferResource(sizeof(AlignedVector4) * kMaxInstances);
    hr = primitiveColorBuffer_->Map(0, nullptr, (void**)&primitiveColorData_);
    assert(SUCCEEDED(hr));
    const int kSegments = 36; // 16角形で円を作る
    Vector4 sphereVerts[kSegments * 3];
    uint32_t sphereIdx[kSegments * 6];

    for (int i = 0; i < kSegments; ++i) {
        float theta = (2.0f * 3.14159265f * i) / kSegments;
        float cosT = std::cos(theta) * 0.5f; // 半径0.5の基準球
        float sinT = std::sin(theta) * 0.5f;

        // XY平面, YZ平面, ZX平面 の3つの円を作る
        sphereVerts[i] = { cosT, sinT, 0.0f, 1.0f };
        sphereVerts[kSegments + i] = { 0.0f, cosT, sinT, 1.0f };
        sphereVerts[kSegments * 2 + i] = { sinT, 0.0f, cosT, 1.0f };

        // インデックス結線
        sphereIdx[i * 2] = i;
        sphereIdx[i * 2 + 1] = (i + 1) % kSegments;

        sphereIdx[kSegments * 2 + i * 2] = kSegments + i;
        sphereIdx[kSegments * 2 + i * 2 + 1] = kSegments + ((i + 1) % kSegments);

        sphereIdx[kSegments * 4 + i * 2] = kSegments * 2 + i;
        sphereIdx[kSegments * 4 + i * 2 + 1] = kSegments * 2 + ((i + 1) % kSegments);
    }
    sphereVertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(sphereVerts));
    sphereVertexBufferView_ = { sphereVertexBuffer_->GetGPUVirtualAddress(), sizeof(sphereVerts), sizeof(Vector4) };
    void* svbData; hr = sphereVertexBuffer_->Map(0, nullptr, &svbData); assert(SUCCEEDED(hr));
    memcpy(svbData, sphereVerts, sizeof(sphereVerts)); sphereVertexBuffer_->Unmap(0, nullptr);

    sphereIndexBuffer_ = dxCommon_->CreateBufferResource(sizeof(sphereIdx));
    sphereIndexBufferView_ = { sphereIndexBuffer_->GetGPUVirtualAddress(), sizeof(sphereIdx), DXGI_FORMAT_R32_UINT };
    void* sibData; hr = sphereIndexBuffer_->Map(0, nullptr, &sibData); assert(SUCCEEDED(hr));
    memcpy(sibData, sphereIdx, sizeof(sphereIdx)); sphereIndexBuffer_->Unmap(0, nullptr);

    primitiveWVPBuffer_ = dxCommon_->CreateBufferResource(sizeof(AlignedMatrix4x4) * kMaxInstances);
    hr = primitiveWVPBuffer_->Map(0, nullptr, (void**)&primitiveWVPData_);
    assert(SUCCEEDED(hr));
    // --- 円柱のバッファ作成 ---
    const int kCylinderSegments = 16; // 16角柱で円柱を表現
    Vector4 cylinderVerts[kCylinderSegments * 2];
    uint32_t cylinderIdx[kCylinderSegments * 6];

    for (int i = 0; i < kCylinderSegments; ++i) {
        float theta = (2.0f * 3.14159265f * i) / kCylinderSegments;
        float cosT = std::cos(theta) * 0.5f; // 半径0.5
        float sinT = std::sin(theta) * 0.5f;

        // 上面の頂点 (Y = 0.5)
        cylinderVerts[i] = { cosT, 0.5f, sinT, 1.0f };
        // 底面の頂点 (Y = -0.5)
        cylinderVerts[kCylinderSegments + i] = { cosT, -0.5f, sinT, 1.0f };

        // インデックス結線
        // ① 上面の円の線分
        cylinderIdx[i * 2] = i;
        cylinderIdx[i * 2 + 1] = (i + 1) % kCylinderSegments;

        // ② 底面の円の線分
        cylinderIdx[kCylinderSegments * 2 + i * 2] = kCylinderSegments + i;
        cylinderIdx[kCylinderSegments * 2 + i * 2 + 1] = kCylinderSegments + ((i + 1) % kCylinderSegments);

        // ③ 上下を繋ぐ縦線
        cylinderIdx[kCylinderSegments * 4 + i * 2] = i;
        cylinderIdx[kCylinderSegments * 4 + i * 2 + 1] = kCylinderSegments + i;
    }

    // 頂点バッファ生成
    cylinderVertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(cylinderVerts));
    cylinderVertexBufferView_ = { cylinderVertexBuffer_->GetGPUVirtualAddress(), sizeof(cylinderVerts), sizeof(Vector4) };
    void* cvbData; hr = cylinderVertexBuffer_->Map(0, nullptr, &cvbData); assert(SUCCEEDED(hr));
    memcpy(cvbData, cylinderVerts, sizeof(cylinderVerts)); cylinderVertexBuffer_->Unmap(0, nullptr);

    // インデックスバッファ生成
    cylinderIndexBuffer_ = dxCommon_->CreateBufferResource(sizeof(cylinderIdx));
    cylinderIndexBufferView_ = { cylinderIndexBuffer_->GetGPUVirtualAddress(), sizeof(cylinderIdx), DXGI_FORMAT_R32_UINT };
    void* cibData; hr = cylinderIndexBuffer_->Map(0, nullptr, &cibData); assert(SUCCEEDED(hr));
    memcpy(cibData, cylinderIdx, sizeof(cylinderIdx)); cylinderIndexBuffer_->Unmap(0, nullptr);
}

void PrimitiveDrawer::Finalize() {
    if (primitiveWVPData_) { primitiveWVPBuffer_->Unmap(0, nullptr); primitiveWVPData_ = nullptr; }
    if (primitiveColorData_) { primitiveColorBuffer_->Unmap(0, nullptr); primitiveColorData_ = nullptr; }
}

void PrimitiveDrawer::PreDraw(ID3D12GraphicsCommandList* commandList) {
    commandList->SetPipelineState(primitivePipelineState_.Get());
    commandList->SetGraphicsRootSignature(primitiveRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &cubeVertexBufferView_);
    commandList->IASetIndexBuffer(&cubeIndexBufferView_);
}

void PrimitiveDrawer::DrawWireCube(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex) {
    Math math; const Camera* camera = CameraManager::GetInstance()->GetMainCamera(); if (!camera) return;

    primitiveWVPData_[instanceIndex].matrix = math.Multiply(worldMatrix, math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix()));

    Vector4 opaqueColor = color;
    opaqueColor.w = 1.0f;
    primitiveColorData_[instanceIndex].vector = opaqueColor;

    D3D12_GPU_VIRTUAL_ADDRESS wvpGpuAddress = primitiveWVPBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedMatrix4x4));
    D3D12_GPU_VIRTUAL_ADDRESS colorGpuAddress = primitiveColorBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedVector4));

    commandList->SetGraphicsRootConstantBufferView(0, wvpGpuAddress);
    commandList->SetGraphicsRootConstantBufferView(1, colorGpuAddress);

    commandList->DrawIndexedInstanced(24, 1, 0, 0, 0);
    RenderStats::GetInstance()->RecordIndexedDraw(24);
}

void PrimitiveDrawer::DrawWireSphere(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex) {
    Math math; const Camera* camera = CameraManager::GetInstance()->GetMainCamera(); if (!camera) return;

    primitiveWVPData_[instanceIndex].matrix = math.Multiply(worldMatrix, math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix()));

    Vector4 opaqueColor = color;
    opaqueColor.w = 1.0f;
    primitiveColorData_[instanceIndex].vector = opaqueColor;

    D3D12_GPU_VIRTUAL_ADDRESS wvpGpuAddress = primitiveWVPBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedMatrix4x4));
    D3D12_GPU_VIRTUAL_ADDRESS colorGpuAddress = primitiveColorBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedVector4));

    commandList->SetGraphicsRootConstantBufferView(0, wvpGpuAddress);
    commandList->SetGraphicsRootConstantBufferView(1, colorGpuAddress);

    // 球体専用のバッファをセットして描画
    commandList->IASetVertexBuffers(0, 1, &sphereVertexBufferView_);
    commandList->IASetIndexBuffer(&sphereIndexBufferView_);

    // 16分割 * 3平面 * 2(線分) = 96 インデックス
    commandList->DrawIndexedInstanced(216, 1, 0, 0, 0);
    RenderStats::GetInstance()->RecordIndexedDraw(216);

    // デフォルトの描画（キューブ）用バッファに復帰
    commandList->IASetVertexBuffers(0, 1, &cubeVertexBufferView_);
    commandList->IASetIndexBuffer(&cubeIndexBufferView_);
}


void PrimitiveDrawer::DrawWireCylinder(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex) {
    Math math;
    const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return;

    // 定数バッファの更新（WVP行列と色）
    primitiveWVPData_[instanceIndex].matrix = math.Multiply(worldMatrix, math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix()));
    Vector4 opaqueColor = color;
    opaqueColor.w = 1.0f;
    primitiveColorData_[instanceIndex].vector = opaqueColor;

    D3D12_GPU_VIRTUAL_ADDRESS wvpGpuAddress = primitiveWVPBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedMatrix4x4));
    D3D12_GPU_VIRTUAL_ADDRESS colorGpuAddress = primitiveColorBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedVector4));

    commandList->SetGraphicsRootConstantBufferView(0, wvpGpuAddress);
    commandList->SetGraphicsRootConstantBufferView(1, colorGpuAddress);

    // 円柱専用のバッファをセットして描画
    commandList->IASetVertexBuffers(0, 1, &cylinderVertexBufferView_);
    commandList->IASetIndexBuffer(&cylinderIndexBufferView_);

    // 16分割 × (上面+底面+縦線=3パーツ) × 2インデックス = 96インデックス
    commandList->DrawIndexedInstanced(96, 1, 0, 0, 0);
    RenderStats::GetInstance()->RecordIndexedDraw(96);

    // 他の描画（DrawWireCube等）に影響が出ないようにキューブ用に戻しておく
    commandList->IASetVertexBuffers(0, 1, &cubeVertexBufferView_);
    commandList->IASetIndexBuffer(&cubeIndexBufferView_);
}
