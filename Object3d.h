#pragma once
#include "Math.h"
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include"ModelData.h"

class DirectXCommon;
class Object3dCommon;


class Object3d {
public:
    /// <summary>
    /// ������
    /// </summary>
    void Initialize();

    /// <summary>
    /// �X�V
    /// </summary>
    void Update(const class DebugCamera& camera);

    /// <summary>
    /// �`��
    /// </summary>
    void Draw();

    /// <summary>
    /// ���f���̃Z�b�g
    /// </summary>
    void SetModel(const ModelData* modelData);

    // --- �ÓI�����o ---
    static void SetCommon(Object3dCommon* common) { common_ = common; }

public: // �p�����[�^�ݒ�p
    Transform transform; // �ʒu�A��]�A�g�k

private:
    // DirectX���
    DirectXCommon* dxCommon_ = nullptr;
    // 3D�I�u�W�F�N�g���ʕ`��ݒ�
    static Object3dCommon* common_;

    // ���f���f�[�^
    const ModelData* modelData_ = nullptr;
    // �e�N�X�`���n���h��
    uint32_t textureHandle_ = 0;

    // ���_�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // WVP�s��p���\�[�X
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_ = nullptr;
    TransformationMatrix* wvpData_ = nullptr;

    // �}�e���A���p���\�[�X
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
    Material* materialDataForGPU_ = nullptr;
};