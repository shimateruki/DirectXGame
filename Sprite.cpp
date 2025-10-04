#include "Sprite.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include <cassert>
#include "TextureManager.h"

/// <summary>
/// 初期化
/// </summary>
 void Sprite::Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath) {
	// ファイルパスからテクスチャハンドルを取得
uint32_t handle = TextureManager::GetInstance()->Load(textureFilePath);
// 取得したハンドルを使って、もう片方のInitialize関数を呼び出す
Initialize(dxCommon, handle);
}


void Sprite::Initialize(DirectXCommon* dxCommon, uint32_t textureHandle)
{
	assert(dxCommon);
	dxCommon_ = dxCommon;
	textureHandle_ = textureHandle; // 受け取ったハンドルをそのままメンバ変数に保存
	// 各種リソース作成
	vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * 4);
	vertexBufferView_ = { vertexResource_->GetGPUVirtualAddress(), sizeof(VertexData) * 4, sizeof(VertexData) };
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

	indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 6);
	indexBufferView_ = { indexResource_->GetGPUVirtualAddress(), sizeof(uint32_t) * 6, DXGI_FORMAT_R32_UINT };
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

	materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

	wvpResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
	wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));

	// --- 頂点データを「サイズ1x1」のものに変更 ---
	// 左下
	vertexData_[0] = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
	// 左上
	vertexData_[1] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };
	// 右下
	vertexData_[2] = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
	// 右上
	vertexData_[3] = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };

	// インデックスデータ
	indexData_[0] = 0; indexData_[1] = 1; indexData_[2] = 2;
	indexData_[3] = 1; indexData_[4] = 3; indexData_[5] = 2;

}

/// <summary>
/// 更新処理
/// </summary>
void Sprite::Update() {
	// スケール、回転、平行移動を元に行列を計算
	transform_.scale = { size_.x, size_.y, 1.0f };
	transform_.rotate = { 0.0f, 0.0f, rotation_ };
	transform_.translate = { position_.x, position_.y, 0.0f };

	Math math; // Mathクラスのインスタンスを作成
	Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrix = math.makeIdentity4x4();
	Matrix4x4 projectionMatrix = math.MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
	Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));

	// 定数バッファに書き込み
	wvpData_->WVP = worldViewProjectionMatrix;
	wvpData_->World = worldMatrix;
	// 色はSetColorで直接 materialData_->color に書き込んでいるので、ここでは何もしない
	materialData_->enableLighting = false;
	materialData_->uvTransform = math.makeIdentity4x4();
}

/// <summary>
/// 描画
/// </summary>
void Sprite::Draw(ID3D12GraphicsCommandList* commandList) {
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);

	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGpu = {};
	const auto& srvHeap = dxCommon_->GetSrvDescriptorHeap();
	const auto descriptorSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGpu.ptr = srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + (descriptorSize * textureHandle_);
	commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGpu);

	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}