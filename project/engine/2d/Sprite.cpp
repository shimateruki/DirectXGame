#include "Sprite.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/WinApp.h"
#include <cassert>
#include "engine/3d/TextureManager.h"
#include "engine/base/SRVManager.h"
/// <summary>
/// 初期化
/// </summary>
void Sprite::Initialize(SpriteCommon* common, const std::string& textureFilePath) {
	uint32_t handle = TextureManager::GetInstance()->Load(textureFilePath);
	// ★★★ commonを渡して、もう片方のInitializeを呼び出す ★★★
	Initialize(common, handle);
}

void Sprite::Initialize(SpriteCommon* common, uint32_t textureHandle)
{
	assert(common);
	common_ = common;
	dxCommon_ = common_->GetDxCommon();

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
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
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

	AdjustTextureSize();

}

/// <summary>
/// 更新処理
/// </summary>
void Sprite::Update() {
	// 頂点の座標計算
	{
		// アンカーポイントを考慮した頂点ごとのローカル座標
		float left = (0.0f - anchorPoint_.x) * size_.x;
		float right = (1.0f - anchorPoint_.x) * size_.x;
		float top = (0.0f - anchorPoint_.y) * size_.y;
		float bottom = (1.0f - anchorPoint_.y) * size_.y;

		if (isFlipX_) {
			// leftとrightの値を入れ替える
			std::swap(left, right);
		}
		if (isFlipY_) {
			// topとbottomの値を入れ替える
			std::swap(top, bottom);
		}
	

		// ４点の頂点データに反映
		vertexData_[0] = { { left, bottom, 0.0f, 1.0f } }; // 左下
		vertexData_[1] = { { left, top, 0.0f, 1.0f } };    // 左上
		vertexData_[2] = { { right, bottom, 0.0f, 1.0f } };// 右下
		vertexData_[3] = { { right, top, 0.0f, 1.0f } };   // 右上
	}


	// UV座標の計算
	{
		// テクスチャのメタデータを取得
		const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
		// テクスチャ全体のサイズ
		float texWidth = (float)metadata.width;
		float texHeight = (float)metadata.height;
		// UV座標に変換
		float tex_left = textureLeftTop_.x / texWidth;
		float tex_right = (textureLeftTop_.x + textureSize_.x) / texWidth;
		float tex_top = textureLeftTop_.y / texHeight;
		float tex_bottom = (textureLeftTop_.y + textureSize_.y) / texHeight;

		// ４点のUVデータに反映
		vertexData_[0].texcoord = { tex_left, tex_bottom }; // 左下
		vertexData_[1].texcoord = { tex_left, tex_top };    // 左上
		vertexData_[2].texcoord = { tex_right, tex_bottom };// 右下
		vertexData_[3].texcoord = { tex_right, tex_top };   // 右上
	}

	// 行列の計算
	transform_.scale = { 1.0f, 1.0f, 1.0f }; // 頂点座標でサイズ調整済みなので1.0に
	transform_.rotate = { 0.0f, 0.0f, rotation_ };
	transform_.translate = { position_.x, position_.y, 0.0f };

	Math math;
	Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrix = math.makeIdentity4x4();
	Matrix4x4 projectionMatrix = math.MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
	Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));

	// 定数バッファに書き込み
	wvpData_->WVP = worldViewProjectionMatrix;
	wvpData_->World = worldMatrix;
	materialData_->enableLighting = false;
	materialData_->uvTransform = math.makeIdentity4x4();
}


/// <summary>
/// 描画
/// </summary>
void Sprite::Draw() {
	// ★★★ メンバ変数の common_ を使う ★★★
	assert(common_);
	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

	common_->SetPipeline(commandList);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);
	commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, materialResource_->GetGPUVirtualAddress());
	SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, textureHandle_);
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::AdjustTextureSize() {
	const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
	size_ = { (float)metadata.width, (float)metadata.height };
	textureSize_ = { (float)metadata.width, (float)metadata.height };
}