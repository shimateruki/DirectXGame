#include "Sprite.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include <cassert>
#include "TextureManager.h"

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

}

/// <summary>
/// �X�V����
/// </summary>
void Sprite::Update() {
	// �X�P�[���A��]�A���s�ړ������ɍs����v�Z
	transform_.scale = { size_.x, size_.y, 1.0f };
	transform_.rotate = { 0.0f, 0.0f, rotation_ };
	transform_.translate = { position_.x, position_.y, 0.0f };

	Math math; // Math�N���X�̃C���X�^���X���쐬
	Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrix = math.makeIdentity4x4();
	Matrix4x4 projectionMatrix = math.MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
	Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));

	// �萔�o�b�t�@�ɏ�������
	wvpData_->WVP = worldViewProjectionMatrix;
	wvpData_->World = worldMatrix;
	// �F��SetColor�Œ��� materialData_->color �ɏ�������ł���̂ŁA�����ł͉������Ȃ�
	materialData_->enableLighting = false;
	materialData_->uvTransform = math.makeIdentity4x4();
}

/// <summary>
/// �`��
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