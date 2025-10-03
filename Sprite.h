#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "Math.h"
#include <cstdint>

class DirectXCommon;

class Sprite {
public: // �����\���̂̒�`
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};
	struct Transform
	{
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};

public: // �����o�֐�
	/// <summary>
	/// ������
	/// </summary>
	void Initialize(DirectXCommon* dxCommon, uint32_t textureHandle);

	/// <summary>
	/// �X�V����
	/// </summary>
	void Update();

	/// <summary>
	/// �`��
	/// </summary>
	void Draw(ID3D12GraphicsCommandList* commandList);

	// --- �Q�b�^�[/�Z�b�^�[ ---
	const Vector2& GetPosition() const { return position_; }
	void SetPosition(const Vector2& position) { position_ = position; }

	float GetRotation() const { return rotation_; }
	void SetRotation(float rotation) { rotation_ = rotation; }

	const Vector2& GetSize() const { return size_; }
	void SetSize(const Vector2& size) { size_ = size; }

	const Vector4& GetColor() const { return materialData_->color; }
	void SetColor(const Vector4& color) { materialData_->color = color; }

private: // �����o�ϐ�
	DirectXCommon* dxCommon_ = nullptr;
	uint32_t textureHandle_ = 0;

	// ��{�I�ȍ��W�E��]�E�T�C�Y
	Vector2 position_ = { 0.0f, 0.0f };
	float rotation_ = 0.0f;
	Vector2 size_ = { 100.0f, 100.0f };

	// �����Ŏg��Transform
	Transform transform_ = { {1.0f,1.0f ,1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f,0.0f, 0.0f} };

	// ���\�[�X
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