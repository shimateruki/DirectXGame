// Model.h
#pragma once
#include <string>
#include <vector>
#include <wrl.h>

#include "Math.h"
#include "Object3dCommon.h"
#include "TextureManager.h"

class Model {
public:
    // CBuffer�p�\����
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
    };

    // ImGui����ҏW���邽�߂ɃQ�b�^�[��ǉ�
    Material* GetMaterial() { return materialData_; }

public:
    void Initialize(Object3dCommon* common, const std::string& modelFilePath);
    void Draw(ID3D12GraphicsCommandList* commandList);

private:
    // ���_�f�[�^�\����
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    // ���f���f�[�^�\����
    struct MaterialData {
        std::string textureFilePath;
        uint32_t textureHandle = 0;
    };
    struct ModelData {
        std::vector<VertexData> vertices;
        MaterialData material;
    };

    // .obj .mtl�ǂݍ��݊֐�
    ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
    MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

private:
    Object3dCommon* common_ = nullptr;
    ModelData modelData_;

    // ���_���\�[�X
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // �}�e���A�����\�[�X
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
};