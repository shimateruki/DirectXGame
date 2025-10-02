#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "Math.h"
#include <cstdint>

class DirectXCommon;

class Sprite {
public:
	struct Transform
	{
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};

	// 構造体の定義
	// 頂点データ構造体（スライド準拠）
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal; // 3Dオブジェクトとシェーダーを共有するため残す
	};

	// マテリアル用構造体（スライド準拠）
	struct Material {
		Vector4 color;
		int32_t enableLighting; // 3Dオブジェクトとシェーダーを共有するため残す
		float padding[3];
		Matrix4x4 uvTransform; // 3Dオブジェクトとシェーダーを共有するため残す
	};

	// 座標変換行列用構造体（スライド準拠）
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

public: // メンバ関数
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(DirectXCommon* dxCommon, uint32_t textureHandle);

	/// <summary>
	/// 更新処理
	/// </summary>
	void Update();

	/// <summary>
	/// 描画
	/// </summary>
	void Draw(ID3D12GraphicsCommandList* commandList);

public: 
	Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };

private: // メンバ変数
	DirectXCommon* dxCommon_ = nullptr;
	uint32_t textureHandle_ = 0;

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
	Math* math = new Math();
};