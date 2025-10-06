#pragma once

#include "engine/base/Math.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/ModelCommon.h"
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

class Object3d;

class Model {
public: // �T�u�N���X
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    struct MaterialData {
        std::string textureFilePath;
        uint32_t textureHandle = 0;
    };

    struct ModelData {
        std::vector<VertexData> vertices;
        MaterialData material;
    };

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
    void Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename);

    /// <summary>
    /// �`��
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList, ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource);

    /// <summary>
    /// �}�e���A�����̎擾 (ImGui�ł̑���p)
    /// </summary>
    Material* GetMaterial() { return materialData_; }

private: // �ÓI�����o�֐�
    static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

private: // �����o�ϐ�
    ModelCommon* common_ = nullptr;
    ModelData modelData_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
};