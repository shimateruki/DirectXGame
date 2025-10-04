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
    // CBuffer用構造体
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
    };

    // ImGuiから編集するためにゲッターを追加
    Material* GetMaterial() { return materialData_; }

public:
    void Initialize(Object3dCommon* common, const std::string& modelFilePath);
    void Draw(ID3D12GraphicsCommandList* commandList);

private:
    // 頂点データ構造体
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    // モデルデータ構造体
    struct MaterialData {
        std::string textureFilePath;
        uint32_t textureHandle = 0;
    };
    struct ModelData {
        std::vector<VertexData> vertices;
        MaterialData material;
    };

    // .obj .mtl読み込み関数
    ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
    MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

private:
    Object3dCommon* common_ = nullptr;
    ModelData modelData_;

    // 頂点リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // マテリアルリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
};