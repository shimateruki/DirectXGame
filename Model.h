#pragma once

#include "Math.h"
#include "TextureManager.h"
#include "ModelCommon.h"
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

class Object3d; // Object3d��TransformationMatrix��DirectionalLight���g�p���邽�ߑO���錾

class Model {
public: // �T�u�N���X
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

public: // �����o�֐�
    /// <summary>
    /// ������
    /// </summary>
    void Initialize(ModelCommon* common, const std::string& modelFilePath);

    /// <summary>
    /// �`��
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList, ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource);

    /// <summary>
    /// �}�e���A�����̎擾 (ImGui�ł̑���p)
    /// </summary>
    Material* GetMaterial() { return materialData_; }


private: // �ÓI�����o�֐�
    /// <summary>
    /// OBJ�t�@�C�����烂�f���f�[�^��ǂݍ���
    /// </summary>
    static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

    /// <summary>
    /// MTL�t�@�C������}�e���A���f�[�^��ǂݍ���
    /// </summary>
    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);


private: // �����o�ϐ�
    // ���ʕ��i�ւ̃|�C���^
    ModelCommon* common_ = nullptr;
    // ���f���f�[�^
    ModelData modelData_{};

    // ���_�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // �}�e���A���萔�o�b�t�@
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
};