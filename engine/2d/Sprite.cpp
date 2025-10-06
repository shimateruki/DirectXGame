#include "Sprite.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/WinApp.h"
#include <cassert>
#include "engine/3d/TextureManager.h"

/// <summary>
/// ������
/// </summary>
 void Sprite::Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath) {
	// �t�@�C���p�X����e�N�X�`���n���h�����擾
uint32_t handle = TextureManager::GetInstance()->Load(textureFilePath);
// �擾�����n���h�����g���āA�����Е���Initialize�֐����Ăяo��
Initialize(dxCommon, handle);
}


void Sprite::Initialize(DirectXCommon* dxCommon, uint32_t textureHandle)
{
	assert(dxCommon);
	dxCommon_ = dxCommon;
	textureHandle_ = textureHandle; // �󂯎�����n���h�������̂܂܃����o�ϐ��ɕۑ�
	// �e�탊�\�[�X�쐬
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

	// --- ���_�f�[�^���u�T�C�Y1x1�v�̂��̂ɕύX ---
	// ����
	vertexData_[0] = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
	// ����
	vertexData_[1] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };
	// �E��
	vertexData_[2] = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
	// �E��
	vertexData_[3] = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };

	// �C���f�b�N�X�f�[�^
	indexData_[0] = 0; indexData_[1] = 1; indexData_[2] = 2;
	indexData_[3] = 1; indexData_[4] = 3; indexData_[5] = 2;

	AdjustTextureSize();

}

/// <summary>
/// �X�V����
/// </summary>
void Sprite::Update() {
	// ���_�̍��W�v�Z
	{
		// �A���J�[�|�C���g���l���������_���Ƃ̃��[�J�����W
		float left = (0.0f - anchorPoint_.x) * size_.x;
		float right = (1.0f - anchorPoint_.x) * size_.x;
		float top = (0.0f - anchorPoint_.y) * size_.y;
		float bottom = (1.0f - anchorPoint_.y) * size_.y;

		if (isFlipX_) {
			// left��right�̒l�����ւ���
			std::swap(left, right);
		}
		if (isFlipY_) {
			// top��bottom�̒l�����ւ���
			std::swap(top, bottom);
		}
	

		// �S�_�̒��_�f�[�^�ɔ��f
		vertexData_[0] = { { left, bottom, 0.0f, 1.0f } }; // ����
		vertexData_[1] = { { left, top, 0.0f, 1.0f } };    // ����
		vertexData_[2] = { { right, bottom, 0.0f, 1.0f } };// �E��
		vertexData_[3] = { { right, top, 0.0f, 1.0f } };   // �E��
	}


	// UV���W�̌v�Z
	{
		// �e�N�X�`���̃��^�f�[�^���擾
		const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
		// �e�N�X�`���S�̂̃T�C�Y
		float texWidth = (float)metadata.width;
		float texHeight = (float)metadata.height;
		// UV���W�ɕϊ�
		float tex_left = textureLeftTop_.x / texWidth;
		float tex_right = (textureLeftTop_.x + textureSize_.x) / texWidth;
		float tex_top = textureLeftTop_.y / texHeight;
		float tex_bottom = (textureLeftTop_.y + textureSize_.y) / texHeight;

		// �S�_��UV�f�[�^�ɔ��f
		vertexData_[0].texcoord = { tex_left, tex_bottom }; // ����
		vertexData_[1].texcoord = { tex_left, tex_top };    // ����
		vertexData_[2].texcoord = { tex_right, tex_bottom };// �E��
		vertexData_[3].texcoord = { tex_right, tex_top };   // �E��
	}

	// �s��̌v�Z
	transform_.scale = { 1.0f, 1.0f, 1.0f }; // ���_���W�ŃT�C�Y�����ς݂Ȃ̂�1.0��
	transform_.rotate = { 0.0f, 0.0f, rotation_ };
	transform_.translate = { position_.x, position_.y, 0.0f };

	Math math;
	Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrix = math.makeIdentity4x4();
	Matrix4x4 projectionMatrix = math.MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
	Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));

	// �萔�o�b�t�@�ɏ�������
	wvpData_->WVP = worldViewProjectionMatrix;
	wvpData_->World = worldMatrix;
	materialData_->enableLighting = false;
	materialData_->uvTransform = math.makeIdentity4x4();
}


/// <summary>
/// �`��
/// </summary>
void Sprite::Draw(ID3D12GraphicsCommandList* commandList) {
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);
	commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, materialResource_->GetGPUVirtualAddress());


	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGpu = {};
	const auto& srvHeap = dxCommon_->GetSrvDescriptorHeap();
	const auto descriptorSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGpu.ptr = srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + (descriptorSize * textureHandle_);
	commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGpu);

	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::AdjustTextureSize() {
	const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle_);
	size_ = { (float)metadata.width, (float)metadata.height };
	textureSize_ = { (float)metadata.width, (float)metadata.height };
}