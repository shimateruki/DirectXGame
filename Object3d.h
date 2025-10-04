#pragma once

#include "Math.h"
#include "TextureManager.h"
#include "Object3dCommon.h"
#include <string>
#include <vector>
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

    // ���_�f�[�^�\����
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    // �}�e���A���f�[�^�\���́i.mtl�t�@�C���̏��j
    struct MaterialData {
        std::string textureFilePath;
        uint32_t textureHandle = 0;
    };

    // ���f���f�[�^�\����
    struct ModelData {
        std::vector<VertexData> vertices;
        MaterialData material;
    };

    // �}�e���A���萔�o�b�t�@�p�\����
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float padding2[3];
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

public: // �ÓI�����o�֐�
    /// <summary>
    /// OBJ�t�@�C�����烂�f���f�[�^��ǂݍ���
    /// </summary>
    static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

private: // �ÓI�����o�֐�
    /// <summary>
    /// MTL�t�@�C������}�e���A���f�[�^��ǂݍ���
    /// </summary>
    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);


public: // �����o�֐�
    /// <summary>
    /// ������
    /// </summary>
    void Initialize(Object3dCommon* common, const std::string& modelFilePath);

    /// <summary>
    /// �X�V
    /// </summary>
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);

    /// <summary>
    /// �`��
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// �g�����X�t�H�[�����̎擾
    /// </summary>
    Transform* GetTransform() { return &transform_; }

    /// <summary>
    /// �}�e���A�����̎擾 (ImGui�ł̑���p)
    /// </summary>
    Material* GetMaterial() { return materialData_; }

    /// <summary>
    /// ���s�������̎擾 (ImGui�ł̑���p)
    /// </summary>
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

private: // �����o�ϐ�
    // ���ʕ��i�ւ̃|�C���^
    Object3dCommon* common_ = nullptr;
    // ���f���f�[�^
    ModelData modelData_{};

    // ���_�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // �}�e���A���萔�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    // ���W�ϊ��s��萔�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    // ���s�����萔�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    // ���[�J�����W
    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
};