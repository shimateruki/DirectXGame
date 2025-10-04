#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon; // �O���錾

class Object3dCommon {
public:
    /// <summary>
    /// ������
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// �`��O�̋��ʃR�}���h�i�`��O�ɌĂԁj
    /// </summary>
    void PreDraw(ID3D12GraphicsCommandList* commandList);

private:
    /// <summary>
    /// ���[�g�V�O�l�`���̍쐬
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// �O���t�B�b�N�X�p�C�v���C���̐���
    /// </summary>
    void CreateGraphicsPipeline();

private:
    // DirectX��ՃN���X�̃|�C���^
    DirectXCommon* dxCommon_ = nullptr;
    // ���[�g�V�O�l�`��
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    // �p�C�v���C���X�e�[�g�I�u�W�F�N�g
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;
};