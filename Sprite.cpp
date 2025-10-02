#include "Sprite.h"
#include "DirectXCommon.h"
#include "WinApp.h" // ウィンドウサイズを取得するため
#include <cassert>
#include"Math.h"

/// <summary>
/// 初期化
/// </summary>
void Sprite::Initialize(DirectXCommon* dxCommon, uint32_t textureHandle) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    textureHandle_ = textureHandle;

    // 頂点リソース作成
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * 4);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

    // インデックスリソース作成
    indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 6);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

    // マテリアル用リソース作成
    materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    // WVP用リソース作成
    wvpResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));

    // --- 頂点とインデックスのデータを書き込む ---
    // 左上
    vertexData_[0] = { { 0.0f, 360.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
    // 右上
    vertexData_[1] = { { 640.0f, 360.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
    // 左下
    vertexData_[2] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };
    // 右下
    vertexData_[3] = { { 640.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };

    // インデックスデータ
    indexData_[0] = 0; indexData_[1] = 2; indexData_[2] = 1;
    indexData_[3] = 2; indexData_[4] = 3; indexData_[5] = 1;
}

/// <summary>
/// 更新処理
/// </summary>
void Sprite::Update() {
    // 行列の計算
    Matrix4x4 worldMatrix =math-> MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrix = math->makeIdentity4x4();
    Matrix4x4 projectionMatrix = math->MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
    Matrix4x4 worldViewProjectionMatrix = math->Multiply(worldMatrix, math-> Multiply(viewMatrix, projectionMatrix));

    // 定数バッファに書き込み
    wvpData_->WVP = worldViewProjectionMatrix;
    wvpData_->World = worldMatrix;
    materialData_->color = color_;
    // 3Dオブジェクトとシェーダーを共有しているので、ライティングを無効化
    materialData_->enableLighting = false;
    materialData_->uvTransform = math->makeIdentity4x4();
}

/// <summary>
/// 描画
/// </summary>
void Sprite::Draw(ID3D12GraphicsCommandList* commandList) {
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // 3Dオブジェクトとルートシグネチャを共有しているため、インデックスを合わせる
    // material -> rootParmeters[0]
    // wvp -> rootParmeters[1]
    // texture -> rootParmeters[2]
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

    // SRVのハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGpu = {};
    const auto& srvHeap = dxCommon_->GetSrvDescriptorHeap();
    const auto descriptorSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    textureSrvHandleGpu.ptr = srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + (descriptorSize * textureHandle_);
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGpu);

    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}