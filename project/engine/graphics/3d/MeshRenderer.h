#pragma once
#include "Object3dCommon.h"
#include "Model.h"
#include "Transform.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <memory>

class Object3d; // 前方宣言

class MeshRenderer {
public:
    // 定数バッファ用構造体 (Object3dから移植)
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
        Matrix4x4 WorldInverseTranspose;
    };
    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float intensity;
    };
    struct CameraForGPU {
        Vector3 worldPosition;
    };
    struct MaterialData {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType;
        float roughness;           // 4 byte (粗さ: 0.0=ツルツル, 1.0=ザラザラ)
        float metallic;            // 4 byte (金属度: 0.0=非金属, 1.0=金属)
        int32_t enableNormalMap;
        int32_t enableEnvMap;      // 4 byte (環境マップ有効化)
        float envIntensity;        // 4 byte (環境マップ強度)
        float emissive;            // 4 byte (自己発光の強さ。1.0で光らない)
        float padding2[3];         // 12 byte (16バイト境界に合わせるためのダミー)
    };

    struct PointLight {
        Vector4 color;
        Vector3 position;
        float intensity;
        float radius;
        float decay;
        float padding[2];
    };

    struct SpotLight {
        Vector4 color;
        Vector3 position;
        float intensity;
        Vector3 direction;
        float distance;
        float decay;
        float cosAngle;
        float cosFalloffStart;
        float padding[1];
    };
    struct LocalFogData {
        Vector4 fogColor = { 0.2f, 0.8f, 0.5f, 1.0f }; // デフォルトは毒沼のような緑色
        Vector3 cameraPos;
        float fogDensity = 0.5f;
        Matrix4x4 inverseViewProj;
        float time = 0.0f;       // 経過時間
        float edgeFade = 0.1f;   // 箱のフチのボケ具合
        float noiseSpeed = 0.5f; // 揺らぐスピード
        float noiseScale = 0.2f; // 模様の細かさ
        Vector3 lightDirection;                        // 12 byte
        float scatteringIntensity = 0.4f;              // 4 byte (光の明るさ)
        Vector3 lightColor;                            // 12 byte
        float scatteringG = 0.6f;                      // 4 byte (光の芯の強さ: 0.0~0.99)
    };
    struct OutlineData {
        Vector3 localMin = { -0.5f, -0.5f, -0.5f };
        float thickness = 0.025f;
        Vector3 localMax = { 0.5f, 0.5f, 0.5f };
        float padding = 0.0f;
    };
public:
    // コンストラクタ: 描画対象のTransformを受け取る
    MeshRenderer(Transform* transform);
    ~MeshRenderer() = default;

    // 初期化
    void Initialize(Object3dCommon* common);

    // 更新 (WVP行列計算など)
    void Update();

    // 描画
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    void DrawOutline();

    // --- アクセッサ (Setters) ---
    void SetModel(Model* model) { model_ = model; }
    void SetModel(const std::string& modelName);
    Model* GetModel() const { return model_; }
    const std::string& GetModelName() const { return modelName_; }

    void SetColor(const Vector4& color);
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    void SetMaterialType(int32_t type);
    void SetIntensity(float intensity);

    // ゲッター
    Vector4 GetColor() const;
    int32_t GetMaterialType() const;
    BlendMode GetBlendMode() const { return blendMode_; }

    // マテリアルやライトデータへの直接アクセス（必要に応じて）
    MaterialData* GetMaterialData() { return materialData_; }
    DirectionalLight* GetLightData() { return directionalLightData_; }
    void SetMetallic(float metallic);
    void SetRoughness(float roughness);
    float GetMetallic() const;
    float GetRoughness() const;
    void SetEnableNormalMap(bool enable);
    bool GetEnableNormalMap() const;
    void SetNormalMap(const std::string& texturePath);
    std::string GetNormalMapPath() const { return normalMapPath_; }
    uint32_t GetNormalMapHandle() const { return normalMapHandle_; }
    void SetOrmMap(const std::string& texturePath);
    std::string GetOrmMapPath() const { return ormMapPath_; }
    uint32_t GetOrmMapHandle() const { return ormMapHandle_; }

    void SetTexture(const std::string& texturePath);
    std::string GetTexturePath() const { return texturePath_; }
    uint32_t GetTextureHandle() const { return textureHandle_; }

    void SetEnableEnvMap(bool enable);
    bool GetEnableEnvMap() const;
    void SetEnvIntensity(float intensity);
    float GetEnvIntensity() const;

    void DrawShadow(); 
    void DrawLocalFog(uint32_t depthSrvHandle);
    LocalFogData* GetLocalFogData() { return localFogData_; } // 後でエディタから操作するため
    ID3D12Resource* GetWvpResource() const { return wvpResource_.Get(); }
    ID3D12Resource* GetCameraResource() const { return cameraResource_.Get(); }
    void SetEmissive(float emissive);
    float GetEmissive() const;
    void SetIsUIPreview(bool isPreview) { isUIPreview_ = isPreview; }
    void SetUVTransform(const Matrix4x4& mat) { if (materialData_) materialData_->uvTransform = mat; }
private:
    // 依存オブジェクト
    Object3dCommon* common_ = nullptr;
    Transform* transform_ = nullptr; // 位置情報元

    // モデル
    Model* model_ = nullptr;
    std::string modelName_;

    // 描画設定
    BlendMode blendMode_ = BlendMode::kNormal;

    // --- DirectXリソース (Object3dから移動) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraForGPU* cameraData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    MaterialData* materialData_ = nullptr;

    std::string normalMapPath_ = "";
    uint32_t normalMapHandle_ = 0;
    std::string ormMapPath_ = "";
    uint32_t ormMapHandle_ = 0;

    std::string texturePath_ = "";
    uint32_t textureHandle_ = 0;


    Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource_;
    TransformationMatrix* shadowWvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> localFogResource_;
    LocalFogData* localFogData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> outlineResource_;
    OutlineData* outlineData_ = nullptr;
    float time_ = 0.0f;

    bool isUIPreview_ = false;
};
