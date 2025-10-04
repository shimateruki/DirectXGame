#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "Math.h"
#include <cstdint>
#include <string>

class DirectXCommon;

class Sprite {
public: // 内部構造体の定義
	struct Transform
	{
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};

	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};
	struct Material
	{
		Vector4 color;
		int32_t enableLighting;
		float padding1[3];
		Matrix4x4 uvTransform;
		int32_t selectedLighting;
		float padding2[3];

	};
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

public: // メンバ関数
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath);
	void Initialize(DirectXCommon* dxCommon, uint32_t textureHandle);

	/// <summary>
	/// 更新処理
	/// </summary>
	void Update();

	/// <summary>
	/// 描画
	/// </summary>
	void Draw(ID3D12GraphicsCommandList* commandList);

	// --- ゲッター/セッター ---
	const Vector2& GetPosition() const { return position_; }
	void SetPosition(const Vector2& position) { position_ = position; }

	float GetRotation() const { return rotation_; }
	void SetRotation(float rotation) { rotation_ = rotation; }

	const Vector2& GetSize() const { return size_; }
	void SetSize(const Vector2& size) { size_ = size; }

	const Vector4& GetColor() const { return materialData_->color; }
	void SetColor(const Vector4& color) { materialData_->color = color; }

private: // メンバ変数
	DirectXCommon* dxCommon_ = nullptr;
	uint32_t textureHandle_ = 0;

	// 基本的な座標・回転・サイズ
	Vector2 position_ = { 0.0f, 0.0f };
	float rotation_ = 0.0f;
	Vector2 size_ = { 100.0f, 100.0f };

	// 内部で使うTransform
	Transform transform_ = { {1.0f,1.0f ,1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f,0.0f, 0.0f} };



	// リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	VertexData* vertexData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
	uint32_t* indexData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
	Material* materialData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_ = nullptr;
	TransformationMatrix* wvpData_ = nullptr;
};