#pragma once

#include "DirectXCommon.h"
#include <wrl.h>

/// <summary>
/// 3D�I�u�W�F�N�g�̕`��Ɋւ�鋤�ʏ������܂Ƃ߂��N���X
/// </summary>
class Object3dCommon {
public:
    /// <summary>
    /// ����������
    /// </summary>
    /// <param name="dxCommon">DirectX�ėp�N���X�̃C���X�^���X</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 3D�I�u�W�F�N�g�`��O�̋��ʃR�}���h��ݒ肷��
    /// </summary>
    void SetGraphicsCommand();

private:
    /// <summary>
    /// ���[�g�V�O�l�`���̍쐬
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// �p�C�v���C���X�e�[�g�̍쐬
    /// </summary>
    void CreatePipelineState();

private:
    // DirectX�ėp�N���X�i�|�C���^�j
    DirectXCommon* dxCommon_ = nullptr;
    // ���[�g�V�O�l�`��
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    // �p�C�v���C���X�e�[�g
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
};