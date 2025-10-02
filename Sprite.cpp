#include "Sprite.h"
#include "DirectXCommon.h"
#include "WinApp.h" // �E�B���h�E�T�C�Y���擾���邽��
#include <cassert>
#include"Math.h"

/// <summary>
/// ������
/// </summary>
void Sprite::Initialize(DirectXCommon* dxCommon, uint32_t textureHandle) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    textureHandle_ = textureHandle;

    // ���_���\�[�X�쐬
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * 4);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

    // �C���f�b�N�X���\�[�X�쐬
    indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 6);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

    // �}�e���A���p���\�[�X�쐬
    materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    // WVP�p���\�[�X�쐬
    wvpResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));

    // --- ���_�ƃC���f�b�N�X�̃f�[�^���������� ---
    // ����
    vertexData_[0] = { { 0.0f, 360.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
    // �E��
    vertexData_[1] = { { 640.0f, 360.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
    // ����
    vertexData_[2] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };
    // �E��
    vertexData_[3] = { { 640.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} };

    // �C���f�b�N�X�f�[�^
    indexData_[0] = 0; indexData_[1] = 2; indexData_[2] = 1;
    indexData_[3] = 2; indexData_[4] = 3; indexData_[5] = 1;
}

/// <summary>
/// �X�V����
/// </summary>
void Sprite::Update() {
    // �s��̌v�Z
    Matrix4x4 worldMatrix =math-> MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrix = math->makeIdentity4x4();
    Matrix4x4 projectionMatrix = math->MakeOrthographicMatrix(0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 100.0f);
    Matrix4x4 worldViewProjectionMatrix = math->Multiply(worldMatrix, math-> Multiply(viewMatrix, projectionMatrix));

    // �萔�o�b�t�@�ɏ�������
    wvpData_->WVP = worldViewProjectionMatrix;
    wvpData_->World = worldMatrix;
    materialData_->color = color_;
    // 3D�I�u�W�F�N�g�ƃV�F�[�_�[�����L���Ă���̂ŁA���C�e�B���O�𖳌���
    materialData_->enableLighting = false;
    materialData_->uvTransform = math->makeIdentity4x4();
}

/// <summary>
/// �`��
/// </summary>
void Sprite::Draw(ID3D12GraphicsCommandList* commandList) {
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // 3D�I�u�W�F�N�g�ƃ��[�g�V�O�l�`�������L���Ă��邽�߁A�C���f�b�N�X�����킹��
    // material -> rootParmeters[0]
    // wvp -> rootParmeters[1]
    // texture -> rootParmeters[2]
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

    // SRV�̃n���h�����擾
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGpu = {};
    const auto& srvHeap = dxCommon_->GetSrvDescriptorHeap();
    const auto descriptorSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    textureSrvHandleGpu.ptr = srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + (descriptorSize * textureHandle_);
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGpu);

    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}