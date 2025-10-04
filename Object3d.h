#pragma once

#include "Math.h"
#include "TextureManager.h"
#include "Object3dCommon.h"
#include <string>
#include <vector>
#include <wrl.h>

/// <summary>
/// 個々の3Dオブジェクトを表すクラス
/// </summary>
class Object3d {
public: // メンバクラス（構造体）
    struct Transform
    {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

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

    // 座標変換行列定数バッファ用構造体
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
    };

    // 平行光源定数バッファ用構造体
    struct DirectionalLight {
        Vector4 color;   // ライトの色
        Vector3 direction; // ライトの向き
        float intensity; // 輝度
    };

public: // 静的メンバ関数
    /// <summary>
    /// OBJファイルからモデルデータを読み込む
    /// </summary>
    static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

private: // 静的メンバ関数
    /// <summary>
    /// MTLファイルからマテリアルデータを読み込む
    /// </summary>
    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);


public: // メンバ関数
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(Object3dCommon* common, const std::string& modelFilePath);

    /// <summary>
    /// 更新
    /// </summary>
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);

    /// <summary>
    /// 描画
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// トランスフォーム情報の取得
    /// </summary>
    Transform* GetTransform() { return &transform_; }

    /// <summary>
    /// マテリアル情報の取得 (ImGuiでの操作用)
    /// </summary>
    Material* GetMaterial() { return materialData_; }

    /// <summary>
    /// 平行光源情報の取得 (ImGuiでの操作用)
    /// </summary>
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

private: // メンバ変数
    // 共通部品へのポインタ
    Object3dCommon* common_ = nullptr;
    // モデルデータ
    ModelData modelData_{};

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // マテリアル定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    // 座標変換行列定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    // 平行光源定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    // ローカル座標
    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
};