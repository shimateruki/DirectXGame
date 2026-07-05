#pragma once
#include "Model.h"
#include "Object3dCommon.h"
#include "Transform.h"
#include "engine/utility/math/Math.h"
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

class Object3d;

/// <summary>
/// Object3dのメッシュ描画、マテリアル、LOD、特殊マテリアル描画を担当する。
/// </summary>
class MeshRenderer {
public:
    // GPUへ送る行列情報。
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
        float roughness;
        float metallic;
        int32_t enableNormalMap;
        int32_t enableEnvMap;
        float envIntensity;
        float emissive;
        float time;
        float portalClipEnabled;
        float portalClipProgress;
        Vector3 portalClipCenter;
        float portalClipEdgeWidth;
        Vector3 portalClipNormal;
        float portalClipDissolve;
        Vector4 portalClipColor;
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
        Vector4 fogColor = { 0.2f, 0.8f, 0.5f, 1.0f };
        Vector3 cameraPos;
        float fogDensity = 0.5f;
        Matrix4x4 inverseViewProj;
        float time = 0.0f;
        float edgeFade = 0.1f;
        float noiseSpeed = 0.5f;
        float noiseScale = 0.2f;
        Vector3 lightDirection;
        float scatteringIntensity = 0.4f;
        Vector3 lightColor;
        float scatteringG = 0.6f;
    };

    struct WaterParamForGPU {
        float time;
        float waveSpeed;
        float waveHeight;
        float waveFrequency;
        float flowSpeedX;
        float flowSpeedY;
        float uvOffsetX;
        float uvOffsetY;
        float effectType;
        float effectScale;
        float effectSoftness;
        float effectIntensity;
        Vector3 cameraWorldPosition;
        float billboardScale;
        float effectScaleX;
        float effectScaleY;
        float effectScaleZ;
        float waterParamPadding0;
    };

    struct LodLevel {
        int level = 0;
        std::string modelName;
        float distance = 0.0f;
        Model* model = nullptr;
    };

public:
    /// <summary>
    /// 描画対象のTransformを参照してレンダラーを作成する。
    /// </summary>
    MeshRenderer(Transform* transform);
    ~MeshRenderer() = default;

    /// <summary>
    /// GPUリソースと既定マテリアルを初期化する。
    /// </summary>
    void Initialize(Object3dCommon* common);

    /// <summary>
    /// WVP行列、ライト、マテリアル時間などを更新する。
    /// </summary>
    void Update();

    /// <summary>
    /// 現在の有効カメラに合わせて、描画用の行列とカメラ定数だけを更新する。
    /// Camera Previewなど、ゲームロジックやシェーダー時間を進めたくない描画パスで使う。
    /// </summary>
    void RefreshCameraDependentData();

    /// <summary>
    /// 通常メッシュを描画する。
    /// </summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    // モデルとLOD設定。
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

    // マテリアル設定。
    void SetColor(const Vector4& color);
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    void SetMaterialType(int32_t type);
    void SetIntensity(float intensity);

    Vector4 GetColor() const;
    int32_t GetMaterialType() const;
    BlendMode GetBlendMode() const { return blendMode_; }

    // 必要に応じてエディタや演出側から直接調整するためのデータアクセス。
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

    // 影、フォグ、水面、特殊マテリアル描画。
    void DrawShadow();
    void SetShadowCommonState();
    void DrawShadowOnly();
    void DrawLocalFog(uint32_t depthSrvHandle);
    LocalFogData* GetLocalFogData() { return localFogData_; }
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
    bool HasRequiredBuffers() const;
    void InitializeFireProxyModel();
    void InitializeGatePortalProxyModel();
    void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);
    Model* ResolveDrawModel() const;
    void UpdateUvTransform();

    // 外部オブジェクトへの参照。MeshRendererは所有しない。
    Object3dCommon* common_ = nullptr;
    Transform* transform_ = nullptr;

    // 描画モデルとLOD状態。
    Model* model_ = nullptr;
    std::string modelName_;
    int meshDrawIndex_ = -1;
    bool lodEnabled_ = true;
    std::vector<LodLevel> lodLevels_;
    mutable int activeLodLevel_ = 0;

    // 通常描画設定。
    BlendMode blendMode_ = BlendMode::kNormal;

    // 通常描画用GPUリソース。
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraForGPU* cameraData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    MaterialData* materialData_ = nullptr;

    // テクスチャとPBR補助マップ。
    std::string normalMapPath_ = "";
    uint32_t normalMapHandle_ = 0;
    std::string ormMapPath_ = "";
    uint32_t ormMapHandle_ = 0;
    std::string texturePath_ = "";
    uint32_t textureHandle_ = 0;
    Vector2 textureTiling_ = { 1.0f, 1.0f };
    bool autoTextureTiling_ = false;

    // 水面、火、影、ローカルフォグ用リソース。
    Microsoft::WRL::ComPtr<ID3D12Resource> waterParamResource_;
    WaterParamForGPU* waterParamData_ = nullptr;
    std::unique_ptr<Model> fireProxyModel_;
    std::unique_ptr<Model> gatePortalProxyModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource_;
    TransformationMatrix* shadowWvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> localFogResource_;
    LocalFogData* localFogData_ = nullptr;
    float time_ = 0.0f;

    bool isUIPreview_ = false;
};
