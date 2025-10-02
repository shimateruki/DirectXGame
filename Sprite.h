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

	// �\���̂̒�`
	// ���_�f�[�^�\���́i�X���C�h�����j
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal; // 3D�I�u�W�F�N�g�ƃV�F�[�_�[�����L���邽�ߎc��
	};

	// �}�e���A���p�\���́i�X���C�h�����j
	struct Material {
		Vector4 color;
		int32_t enableLighting; // 3D�I�u�W�F�N�g�ƃV�F�[�_�[�����L���邽�ߎc��
		float padding[3];
		Matrix4x4 uvTransform; // 3D�I�u�W�F�N�g�ƃV�F�[�_�[�����L���邽�ߎc��
	};

	// ���W�ϊ��s��p�\���́i�X���C�h�����j
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
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

public: 
	Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };

private: // �����o�ϐ�
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