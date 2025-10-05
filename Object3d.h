#pragma once

#include "Math.h"
#include "Object3dCommon.h"
#include "Model.h" // Model�N���X���C���N���[�h
#include <wrl.h>

/// <summary>
/// �X��3D�I�u�W�F�N�g��\���N���X
/// </summary>
class Object3d {
public: // �����o�N���X�i�\���́j
    struct Transform
    {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

    // ���W�ϊ��s��萔�o�b�t�@�p�\����
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
    };

    // ���s�����萔�o�b�t�@�p�\����
    struct DirectionalLight {
        Vector4 color;   // ���C�g�̐F
        Vector3 direction; // ���C�g�̌���
        float intensity; // �P�x
    };

public: // �����o�֐�
    /// <summary>
    /// ������
    /// </summary>
    void Initialize(Object3dCommon* common);

    /// <summary>
    /// �X�V
    /// </summary>
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);

    /// <summary>
    /// �`��
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// ���f�����Z�b�g
    /// </summary>
    void SetModel(Model* model) { model_ = model; }

    /// <summary>
    /// �g�����X�t�H�[�����̎擾
    /// </summary>
    Transform* GetTransform() { return &transform_; }

    /// <summary>
    /// �}�e���A�����̎擾 (ImGui�ł̑���p)
    /// </summary>
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }

    /// <summary>
    /// ���s�������̎擾 (ImGui�ł̑���p)
    /// </summary>
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

private: // �����o�ϐ�
    // ���ʕ��i�ւ̃|�C���^
    Object3dCommon* common_ = nullptr;
    // ���f���ւ̃|�C���^
    Model* model_ = nullptr;

    // ���W�ϊ��s��萔�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    // ���s�����萔�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    // ���[�J�����W
    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
};