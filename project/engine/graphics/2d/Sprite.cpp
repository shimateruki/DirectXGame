#include "Sprite.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include <cassert>
#include "TextureManager.h"
#include "SRVManager.h"
#include "engine/graphics/core/ColorSpace.h"
#include <algorithm>

namespace {
Matrix4x4 MakeSpriteLocalMatrix(const Sprite* sprite) {
	Math math;
	Vector2 position = sprite ? sprite->GetPosition() : Vector2{ 0.0f, 0.0f };
	float rotation = sprite ? sprite->GetRotation() : 0.0f;
	return math.MakeAffineMatrix(
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, rotation },
		{ position.x, position.y, 0.0f });
}

Matrix4x4 MakeSpriteWorldMatrix(const Sprite* sprite) {
	Math math;
	Matrix4x4 localMatrix = MakeSpriteLocalMatrix(sprite);
	Sprite* parent = sprite ? sprite->GetParent() : nullptr;
	if (!parent) {
		return localMatrix;
	}
	return math.Multiply(localMatrix, MakeSpriteWorldMatrix(parent));
}

bool HasParentInChain(Sprite* sprite, const Sprite* parent) {
	for (Sprite* current = sprite; current != nullptr; current = current->GetParent()) {
		if (current == parent) {
			return true;
		}
	}
	return false;
}

uint32_t ResolveSpriteTextureHandle(uint32_t textureHandle) {
	if (textureHandle != 0) {
		return textureHandle;
	}

	TextureManager* textureManager = TextureManager::GetInstance();
	uint32_t fallback = textureManager->Load("Resources/sprite/common/white.dds", false, false);
	if (fallback == 0) {
		fallback = textureManager->Load("Resources/sprite/common/white.png", false, false);
	}
	return fallback;
}
}

Sprite::~Sprite() {
	SetParent(nullptr, false);
	for (Sprite* child : children_) {
		if (child) {
			child->parent_ = nullptr;
		}
	}
	children_.clear();
}

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

	textureHandle_ = ResolveSpriteTextureHandle(textureHandle);
	if (textureHandle_ != 0) {
		AdjustTextureSize(); // テクスチャサイズに合わせてスプライトのサイズも調整
	}
	else {
		// フォールバックも取得できない場合は描画だけ止め、初期化処理自体は継続する。
		size_ = { 1.0f, 1.0f };
		textureSize_ = { 1.0f, 1.0f };
		isVisible_ = false;
	}

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
	wvpData_->WVP = math.MakeIdentity4x4();
	wvpData_->World = math.MakeIdentity4x4();

	materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->color = ColorSpace::AuthoringToWorking(color_);
	materialData_->emissive = 1.0f;
}

/// <summary>
/// 更新処理
/// </summary>
void Sprite::Update() {
	if (materialData_) {
		materialData_->color = ColorSpace::AuthoringToWorking(color_);
	}
	//  アニメーション処理 
	if (isPlaying_) {
		// タイマーを進める
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

		// ４点の頂点データに反映
		vertexData_[0].position = { left, bottom, 0.0f, 1.0f };
		vertexData_[1].position = { left, top, 0.0f, 1.0f };
		vertexData_[2].position = { right, bottom, 0.0f, 1.0f };
		vertexData_[3].position = { right, top, 0.0f, 1.0f };
	}

	// --- UV座標計算 ---
	{
		if (textureHandle_ == 0) {
			return;
		}
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
	Math math;
	Matrix4x4 worldMatrix = MakeSpriteWorldMatrix(this);
	Matrix4x4 viewMatrix = math.MakeIdentity4x4();
	Matrix4x4 projectionMatrix = math.MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
	Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));

	wvpData_->WVP = worldViewProjectionMatrix;
	wvpData_->World = worldMatrix;
	materialData_->enableLighting = false;
	materialData_->uvTransform = math.MakeIdentity4x4();
}

void Sprite::SetColor(const Vector4& color) {
	color_ = color;
	if (materialData_) {
		materialData_->color = ColorSpace::AuthoringToWorking(color_);
	}
}
/// <summary>
/// 描画
/// </summary>
void Sprite::Draw() {
	if (!common_ || !dxCommon_) return;
	assert(common_);
	if (!isVisible_ || textureHandle_ == 0) return;
	if (!vertexResource_ || !indexResource_ || !wvpResource_ || !materialResource_) return;
	if (!vertexData_ || !materialData_) return;
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
	if (textureHandle_ == 0) {
		size_ = { 1.0f, 1.0f };
		textureSize_ = { 1.0f, 1.0f };
		return;
	}
	const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
	size_ = { (float)metadata.width, (float)metadata.height };
	textureSize_ = { (float)metadata.width, (float)metadata.height };
}

/// <summary>
/// テクスチャ読み込みの静的ラッパー関数 (ファイルパス省略版)
/// </summary>
uint32_t Sprite::LoadTexture(const std::string& fileName) {
	const std::string baseDirectory = "Resources/sprite/";
	const std::string fullPath = baseDirectory + fileName;
	return TextureManager::GetInstance()->Load(fullPath);
}


/// <summary>
/// アニメーションの設定
/// </summary>
void Sprite::SetAnimation(int frameCount, float duration, bool loop) {
	if (textureHandle_ == 0) return;

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

void Sprite::SetParent(Sprite* parent, bool keepWorldPosition) {
	if (parent == this || HasParentInChain(parent, this)) {
		return;
	}

	Vector2 worldPosition = GetWorldPosition();

	if (parent_) {
		auto& siblings = parent_->children_;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
	}

	parent_ = parent;

	if (parent_) {
		auto& siblings = parent_->children_;
		if (std::find(siblings.begin(), siblings.end(), this) == siblings.end()) {
			siblings.push_back(this);
		}
	}

	if (keepWorldPosition) {
		if (parent_) {
			Math math;
			Matrix4x4 parentWorld = MakeSpriteWorldMatrix(parent_);
			Matrix4x4 inverseParent = math.Inverse(parentWorld);
			Vector3 local = math.Transform({ worldPosition.x, worldPosition.y, 0.0f }, inverseParent);
			position_ = { local.x, local.y };
		} else {
			position_ = worldPosition;
		}
	}
}

Vector2 Sprite::GetWorldPosition() const {
	Math math;
	Vector3 world = math.Transform({ 0.0f, 0.0f, 0.0f }, MakeSpriteWorldMatrix(this));
	return { world.x, world.y };
}
