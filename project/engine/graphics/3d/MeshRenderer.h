#pragma once
#include "Object3dCommon.h"
#include "Model.h"
#include "Transform.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <memory>
#include <string>
#include <vector>

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
        float time;                // 4 byte (時間アニメーション用)
        float padding2[2];         // 8 byte (16バイト境界に合わせるためのダミー)
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
    struct WaterParamForGPU {
        float time;
        float waveSpeed;
        float waveHeight;
        float waveFrequency;
        float flowSpeedX;    // X方向への流れる速さ
        float flowSpeedY;    // Y(Z)方向への流れる速さ
        float uvOffsetX;
        float uvOffsetY;
        float effectType;
        float effectScale;
        float effectSoftness;
        float effectIntensity;
        Vector3 cameraWorldPosition;
        float billboardScale;
    };

    struct LodLevel {
        int level = 0;
        std::string modelName;
        float distance = 0.0f;
        Model* model = nullptr;
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

    // --- アクセッサ (Setters) ---
    void SetModel(Model* model);
    void SetModel(const std::string& modelName);
    Model* GetModel() const { return model_; }
    const std::string& GetModelName() const { return modelName_; }
    void SetMeshDrawIndex(int meshIndex) { meshDrawIndex_ = meshIndex; }
    int GetMeshDrawIndex() const { return meshDrawIndex_; }
    bool IsMeshDrawFiltered() const { return meshDrawIndex_ >= 0; }

    void SetLodEnabled(bool enabled) { lodEnabled_ = enabled; }
    bool IsLodEnabled() const { return lodEnabled_; }
    bool HasLodLevels() const { return !lodLevels_.empty(); }
    const std::vector<LodLevel>& GetLodLevels() const { return lodLevels_; }
    void SetLodLevels(const std::vector<LodLevel>& levels);
    void ClearLodLevels();
    bool SetLodLevelDistance(int level, float distance);
    bool LoadLodManifestForModel(const std::string& modelName);
    int GetActiveLodLevel() const;
    std::string GetActiveModelName() const;
    float GetCameraDistanceToObject() const;

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
    void SetTextureTiling(const Vector2& tiling);
    Vector2 GetTextureTiling() const { return textureTiling_; }
    void SetAutoTextureTiling(bool enabled);
    bool GetAutoTextureTiling() const { return autoTextureTiling_; }

    void SetEnableLighting(bool enable) { if (materialData_) materialData_->enableLighting = enable ? 1 : 0; }
    bool GetEnableLighting() const { return materialData_ ? (materialData_->enableLighting != 0) : false; }

    void SetEnableEnvMap(bool enable);
    bool GetEnableEnvMap() const;
    void SetEnvIntensity(float intensity);
    float GetEnvIntensity() const;

    void DrawShadow();

    void SetShadowCommonState(); // 共通設定のみ
    void DrawShadowOnly();      // 描画実行のみ
    void DrawLocalFog(uint32_t depthSrvHandle);
    LocalFogData* GetLocalFogData() { return localFogData_; } // 後でエディタから操作するため
    ID3D12Resource* GetWvpResource() const { return wvpResource_.Get(); }
    ID3D12Resource* GetCameraResource() const { return cameraResource_.Get(); }
    void SetEmissive(float emissive);
    float GetEmissive() const;
    void SetIsUIPreview(bool isPreview) { isUIPreview_ = isPreview; }
    void DrawWater(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle); 
    void DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle);   
    void DrawFire(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawLaser(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawSlimeGel(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawShockwave(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawLiquidContact(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawDamageCrack(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawUpdraft(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawStunBind(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawCloud(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawGatePortal(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    WaterParamForGPU* GetWaterParamData() const { return waterParamData_; }
private:
    void InitializeFireProxyModel();
    void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);
    Model* ResolveDrawModel() const;
    void UpdateUvTransform();

    // 依存オブジェクト
    Object3dCommon* common_ = nullptr;
    Transform* transform_ = nullptr; // 位置情報元

    // モデル
    Model* model_ = nullptr;
    std::string modelName_;
    int meshDrawIndex_ = -1;

    bool lodEnabled_ = true;
    std::vector<LodLevel> lodLevels_;
    mutable int activeLodLevel_ = 0;

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
    Vector2 textureTiling_ = { 1.0f, 1.0f };
    bool autoTextureTiling_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> waterParamResource_;
    WaterParamForGPU* waterParamData_ = nullptr;
    std::unique_ptr<Model> fireProxyModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource_;
    TransformationMatrix* shadowWvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> localFogResource_;
    LocalFogData* localFogData_ = nullptr;
    float time_ = 0.0f;

    bool isUIPreview_ = false;
};
