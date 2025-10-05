#pragma once

#include "Math.h"
#include "TextureManager.h"
#include "ModelCommon.h"
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

class Object3d; // Object3dのTransformationMatrixとDirectionalLightを使用するため前方宣言

class Model {
public: // サブクラス
    // 頂点データ構造体
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    // マテリアルデータ構造体（.mtlファイルの情報）
    struct MaterialData {
        std::string textureFilePath;
        uint32_t textureHandle = 0;
    };

    // モデルデータ構造体
    struct ModelData {
        std::vector<VertexData> vertices;
        MaterialData material;
    };

    // マテリアル定数バッファ用構造体
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float padding2[3];
    };

public: // メンバ関数
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(ModelCommon* common, const std::string& modelFilePath);

    /// <summary>
    /// 描画
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList, ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource);

    /// <summary>
    /// マテリアル情報の取得 (ImGuiでの操作用)
    /// </summary>
    Material* GetMaterial() { return materialData_; }


private: // 静的メンバ関数
    /// <summary>
    /// OBJファイルからモデルデータを読み込む
    /// </summary>
    static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

    /// <summary>
    /// MTLファイルからマテリアルデータを読み込む
    /// </summary>
    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);


private: // メンバ変数
    // 共通部品へのポインタ
    ModelCommon* common_ = nullptr;
    // モデルデータ
    ModelData modelData_{};

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // マテリアル定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
};