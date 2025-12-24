#pragma once

#include "engine/utility/math/Math.h"
#include "TextureManager.h"
#include "ModelCommon.h"
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

class Object3d;

class Model {
public: 

    struct Node {
        Matrix4x4 localMatrix;      // このノードのローカル変換行列
        std::string name;           // ノード名
        std::vector<Node> children; // 子供のノードリスト
    };


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
        Node rootNode;
    };

    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;  
        float padding2[2]; 
    };

public: // メンバ関数
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename);
    void Update();
    /// <summary>
    /// 描画
    /// </summary>
    void Draw(ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource, ID3D12Resource* cameraResource, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>
    /// マテリアル情報の取得 (ImGuiでの操作用)
    /// </summary>
    Material* GetMaterial() { return materialData_; }

    /// <summary>
    /// テクスチャハンドルを取得
    /// </summary>
    uint32_t GetTextureHandle() const { return modelData_.material.textureHandle; }

private: // 静的メンバ関数
    static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
    static Node ReadNode(aiNode* node);
    void UpdateNodeMatrix(Node& node, const Matrix4x4& parentMatrix);

    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

private: // メンバ変数
    ModelCommon* common_ = nullptr;
    ModelData modelData_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

	Math math_;
};