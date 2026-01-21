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
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType; // 0:通常, 1:ガラス
        float padding2;       // パディング調整
    };
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
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
        Material* materialData = nullptr; // マップ用
    };

    struct Mesh {
        std::vector<VertexData> vertices; // このパーツの頂点たち
        uint32_t materialIndex;           // どのマテリアルを使うか？

        // バッファリソースはメッシュごとに持つ
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    };

    struct ModelData {
        std::vector<Mesh> meshes;            // メッシュのリスト 
        std::vector<MaterialData> materials; // マテリアルのリスト 
        Node rootNode;
        std::vector<Node> nodes;             // 当たり判定用の全ノードリスト
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
    void Draw(ID3D12Resource* wvpResource,
        ID3D12Resource* directionalLightResource,
        ID3D12Resource* cameraResource,
        ID3D12Resource* pointLightResource,
        ID3D12Resource* spotLightResource,
        ID3D12Resource* overrideMaterialResource = nullptr);

    /// <summary>
    /// マテリアル情報の取得 (ImGuiでの操作用)
    /// </summary>
    Material* GetMaterial() { return materialData_; }

    /// <summary>
    /// テクスチャハンドルを取得
    /// </summary>
    uint32_t GetTextureHandle() const {
        if (modelData_.materials.empty()) return 0;
        return modelData_.materials[0].textureHandle;
    }
    const ModelData& GetModelData() const { return modelData_; }
    Vector3 GetSize() const { return size_; }
    Vector3 GetCenter() const { return center_; }

private: // 静的メンバ関数
    static ModelData LoadFile(const std::string& directoryPath, const std::string& filename);
    static Node ReadNode(aiNode* node, std::vector<Node>& nodes);
    void UpdateNodeMatrix(Node& node, const Matrix4x4& parentMatrix);

    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

private: // メンバ変数
    ModelCommon* common_ = nullptr;
    ModelData modelData_{};


    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

	Math math_;
    Vector3 size_ = { 1.0f, 1.0f, 1.0f };   // デフォルトは1x1x1
    Vector3 center_ = { 0.0f, 0.0f, 0.0f }; // デフォルトは原点
};