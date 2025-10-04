#include "Object3d.h"
#include "DirectXCommon.h"
#include "Object3dCommon.h"
#include "TextureManager.h"
#include "debugCamera.h"
#include "ModelData.h" // ModelData�̒�`���K�v�Ȃ���

// �ÓI�����o�ϐ��̎��̉�
Object3dCommon* Object3d::common_ = nullptr;

void Object3d::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();

    // WVP�s��p�̃��\�[�X�쐬
    wvpResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));

    // �}�e���A���p�̃��\�[�X�쐬
    materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialDataForGPU_));

    // transform�̏�����
    transform.scale = { 1.0f, 1.0f, 1.0f };
    transform.rotate = { 0.0f, 0.0f, 0.0f };
    transform.translate = { 0.0f, 0.0f, 0.0f };
}

void Object3d::Update(const DebugCamera& camera) {
    Math math;
    Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    Matrix4x4 viewMatrix = camera.GetViewMatrix();
    Matrix4x4 projectionMatrix = camera.GetProjectionMatrix();
    Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));

    wvpData_->WVP = worldViewProjectionMatrix;
    wvpData_->world = worldMatrix;
}

void Object3d::Draw() {
    assert(common_);
    assert(modelData_);

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // CBV�i�萔�o�b�t�@�r���[�j�̐ݒ�
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());
    // SRV�i�V�F�[�_�[���\�[�X�r���[�j�̐ݒ�
    commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetGPUHandle(textureHandle_));

    // VBV�i���_�o�b�t�@�r���[�j�̐ݒ�
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // �`��R�}���h
    commandList->DrawInstanced(UINT(modelData_->vertices.size()), 1, 0, 0);
}

void Object3d::SetModel(const ModelData* modelData) {
    assert(modelData);
    modelData_ = modelData;

    // ���_�o�b�t�@�̍쐬
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * modelData_->vertices.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_->vertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // ���_�f�[�^���}�b�s���O
    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, modelData_->vertices.data(), sizeof(VertexData) * modelData_->vertices.size());
    vertexResource_->Unmap(0, nullptr);

    // �e�N�X�`���̃��[�h�ƃn���h���̎擾
    textureHandle_ = TextureManager::GetInstance()->Load(modelData_->material.textureFilePath);
}