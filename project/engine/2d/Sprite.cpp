#include "Sprite.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/WinApp.h"
#include <cassert>
#include "engine/3d/TextureManager.h"
#include "engine/base/SRVManager.h"

/// <summary>
/// 初期化 (ファイルパス指定)
/// </summary>
void Sprite::Initialize(SpriteCommon* common, const std::string& textureFilePath) {
	uint32_t handle = TextureManager::GetInstance()->Load(textureFilePath);
	Initialize(common, handle);
}

/// <summary>
/// 初期化 (テクスチャハンドル指定)
/// </summary>
void Sprite::Initialize(SpriteCommon* common, uint32_t textureHandle) {
	assert(common);
	common_ = common;
	dxCommon_ = common_->GetDxCommon();

	textureHandle_ = textureHandle;
	AdjustTextureSize(); // テクスチャサイズに合わせてスプライトのサイズも調整

	// 各種リソース作成
	vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * 4);
	vertexBufferView_ = { vertexResource_->GetGPUVirtualAddress(), sizeof(VertexData) * 4, sizeof(VertexData) };
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

	indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 6);
	indexBufferView_ = { indexResource_->GetGPUVirtualAddress(), sizeof(uint32_t) * 6, DXGI_FORMAT_R32_UINT };
	uint32_t* indexData = nullptr; // マッピング用のポインタ
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
	indexData[0] = 0; indexData[1] = 1; indexData[2] = 2;
	indexData[3] = 1; indexData[4] = 3; indexData[5] = 2;
	indexResource_->Unmap(0, nullptr); // アンマップ

	wvpResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
	wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
	Math math;
	wvpData_->WVP = math.makeIdentity4x4();
	wvpData_->World = math.makeIdentity4x4();

	materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
}

/// <summary>
/// 更新処理
/// </summary>
void Sprite::Update() {
	// ▼▼▼ アニメーション処理 ▼▼▼
	if (isPlaying_) {
		// タイマーを進める (60FPSを想定)
		animationTimer_ += 1.0f / 60.0f;

		if (animationTimer_ >= frameDuration_) {
			// 次のフレームへ
			currentFrame_++;
			animationTimer_ = 0.0f; // タイマーリセット

			if (currentFrame_ >= totalFrames_) {
				if (isLooping_) {
					currentFrame_ = 0; // ループするなら最初に戻る
				} else {
					currentFrame_ = totalFrames_ - 1; // ループしないなら最後のフレームで止める
					isPlaying_ = false;
				}
			}
		}

		// 現在のフレームに基づいてテクスチャの表示範囲を計算
		textureLeftTop_.x = (float)(currentFrame_ * frameWidth_);
		textureLeftTop_.y = 0; // 今回は横一列のアニメーションを想定
		textureSize_.x = (float)frameWidth_;
		textureSize_.y = (float)frameHeight_;
	}
	// ▲▲▲ ここまでアニメーション処理 ▲▲▲


	// --- 頂点座標計算 ---
	{
		// アンカーポイントを考慮した頂点ごとのローカル座標
		float left = (0.0f - anchorPoint_.x) * size_.x;
		float right = (1.0f - anchorPoint_.x) * size_.x;
		float top = (0.0f - anchorPoint_.y) * size_.y;
		float bottom = (1.0f - anchorPoint_.y) * size_.y;

		if (isFlipX_) {
			std::swap(left, right);
		}
		if (isFlipY_) {
			std::swap(top, bottom);
		}

		// ★★★ エラー箇所を修正 ★★★
		// ４点の頂点データに反映
		vertexData_[0].position = { left, bottom, 0.0f, 1.0f };
		vertexData_[1].position = { left, top, 0.0f, 1.0f };
		vertexData_[2].position = { right, bottom, 0.0f, 1.0f };
		vertexData_[3].position = { right, top, 0.0f, 1.0f };
	}

	// --- UV座標計算 ---
	{
		const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
		float texWidth = (float)metadata.width;
		float texHeight = (float)metadata.height;
		float tex_left = textureLeftTop_.x / texWidth;
		float tex_right = (textureLeftTop_.x + textureSize_.x) / texWidth;
		float tex_top = textureLeftTop_.y / texHeight;
		float tex_bottom = (textureLeftTop_.y + textureSize_.y) / texHeight;

		vertexData_[0].texcoord = { tex_left, tex_bottom };
		vertexData_[1].texcoord = { tex_left, tex_top };
		vertexData_[2].texcoord = { tex_right, tex_bottom };
		vertexData_[3].texcoord = { tex_right, tex_top };
	}

	// --- 行列計算 ---
	transform_.scale = { 1.0f, 1.0f, 1.0f };
	transform_.rotate = { 0.0f, 0.0f, rotation_ };
	transform_.translate = { position_.x, position_.y, 0.0f };

	Math math;
	Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrix = math.makeIdentity4x4();
	Matrix4x4 projectionMatrix = math.MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
	Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));

	wvpData_->WVP = worldViewProjectionMatrix;
	wvpData_->World = worldMatrix;
	materialData_->enableLighting = false;
	materialData_->uvTransform = math.makeIdentity4x4();
}
/// <summary>
/// 描画
/// </summary>
void Sprite::Draw() {
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

/// <summary>
/// テクスチャサイズに合わせてスプライトのサイズを調整する
/// </summary>
void Sprite::AdjustTextureSize() {
	const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
	size_ = { (float)metadata.width, (float)metadata.height };
	textureSize_ = { (float)metadata.width, (float)metadata.height };
}

/// <summary>
/// テクスチャ読み込みの静的ラッパー関数 (ファイルパス省略版)
/// </summary>
uint32_t Sprite::LoadTexture(const std::string& fileName) {
	// ★★★ この便利な関数はそのまま残します！ ★★★
	const std::string baseDirectory = "resouces/sprite/";
	const std::string defaultExtension = ".png";
	const std::string fullPath = baseDirectory + fileName + defaultExtension;
	return TextureManager::GetInstance()->Load(fullPath);
}

// ▼▼▼ ここからアニメーション用の関数を一番下に追加 ▼▼▼

/// <summary>
/// アニメーションの設定
/// </summary>
void Sprite::SetAnimation(int frameCount, float duration, bool loop) {
	totalFrames_ = (frameCount > 0) ? frameCount : 1;
	frameDuration_ = duration;
	isLooping_ = loop;
	isPlaying_ = false;
	currentFrame_ = 0;
	animationTimer_ = 0.0f;

	// 1フレームのサイズを計算
	const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
	frameWidth_ = (int)metadata.width / totalFrames_;
	frameHeight_ = (int)metadata.height;

	// テクスチャの初期表示範囲とスプライト自体のサイズを1フレーム分に設定
	SetTextureRect({ 0.0f, 0.0f }, { (float)frameWidth_, (float)frameHeight_ });
	SetSize({ (float)frameWidth_, (float)frameHeight_ });
}

/// <summary>
/// アニメーションの再生を開始
/// </summary>
void Sprite::Play() {
	isPlaying_ = true;
	currentFrame_ = 0;
	animationTimer_ = 0.0f;
}

/// <summary>
/// アニメーションを停止
/// </summary>
void Sprite::Stop() {
	isPlaying_ = false;
}