#pragma once
#include "Model.h"
#include "Object3dCommon.h"
#include "Transform.h"
#include "engine/utility/math/Math.h"
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

class Object3d;
class Camera;

/// <summary>
/// Object3dのメッシュ描画、マテリアル、LOD、特殊マテリアル描画を担当する。
/// </summary>
// MeshRendererは、Object3dに紐づくモデル描画、マテリアル、LOD、特殊表現をまとめて扱います。
class MeshRenderer {
public:
    // GPUへ送る行列情報。
        // モデル描画時にGPUへ渡す座標変換行列です。
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

        // 色、PBR、特殊マテリアル、ポータル演出などをGPUへ渡すマテリアル情報です。
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
        Vector3 waterLightDirection;
        float waterLightIntensity;
        Vector3 waterLightColor;
        float waterParamPadding1;
    };

        // カメラ距離に応じて切り替えるLODモデルの1段階分の設定です。
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
        // 親Object3dのTransformを参照して描画できるようにします。
MeshRenderer(Transform* transform);
    ~MeshRenderer() = default;

    /// <summary>
    /// GPUリソースと既定マテリアルを初期化する。
    /// </summary>
        // 描画に必要な共通パイプラインとGPUバッファを準備します。
void Initialize(Object3dCommon* common);

    /// <summary>
    /// WVP行列、ライト、マテリアル時間などを更新する。
    /// </summary>
        // Transform、マテリアル、LOD、時間依存パラメータを更新します。
void Update();

    /// <summary>
    /// 現在の有効カメラに合わせて、描画用の行列とカメラ定数だけを更新する。
    /// Camera Previewなど、ゲームロジックやシェーダー時間を進めたくない描画パスで使う。
    /// </summary>
        // カメラ位置に依存するLOD距離やシェーダー用情報を更新します。
void RefreshCameraDependentData();

    /// <summary>
    /// 通常メッシュを描画する。
    /// </summary>
        // 通常の3Dモデル描画コマンドを発行します。
void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    // 演出用カメラPreview専用。通常描画のWVP/Camera定数を汚さず、別バッファで描画します。
    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex = 0);
    // 特殊マテリアルも通常描画用WVPを上書きせず、Camera Preview専用定数で描画します。
    void DrawSpecialMaterialForCamera(int materialType, Camera* camera, uint32_t depthSrvHandle, uint32_t colorSrvHandle, int previewBufferIndex = 0);

    // モデルとLOD設定。
        // 直接指定されたModelを描画対象として設定します。
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
        // 距離別LODモデル一覧を設定します。
void SetLodLevels(const std::vector<LodLevel>& levels);
    void ClearLodLevels();
    bool SetLodLevelDistance(int level, float distance);
        // モデル名に対応するLOD定義ファイルを読み込みます。
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
        // 法線マップを読み込み、マテリアルへ適用します。
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
        // シャドウマップ生成用の深度描画を行います。
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
      // 当たり判定やゲーム進行用Transformを変更せず、描画だけにローカル姿勢を重ねます。
      void SetVisualTransform(const Vector3& scale, const Vector3& rotation, const Vector3& offset);
      const Vector3& GetVisualScale() const { return visualScale_; }
      const Vector3& GetVisualRotation() const { return visualRotation_; }
      const Vector3& GetVisualOffset() const { return visualOffset_; }
    // 指定したローカルY座標を支点に、上側だけを水平方向へ傾けます。
    void SetVisualShear(const Vector3& horizontalShear, float pivotY);
    void ResetVisualTransform();
        // 水面系特殊マテリアルとして描画します。
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
    void DrawWindOrb(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    WaterParamForGPU* GetWaterParamData() const { return waterParamData_; }

private:
    bool HasRequiredBuffers() const;
    Matrix4x4 BuildRenderWorldMatrix() const;
    bool PreparePreviewCameraData(Camera* camera, int previewBufferIndex, ID3D12Resource*& wvpResource, ID3D12Resource*& cameraResource);
    void DrawWaterWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawMagmaWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawIceWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawFireWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawSpecialMaterialWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);
    const Matrix4x4& GetCachedWorldInverseTranspose(const Matrix4x4& worldMatrix);
    void InitializeWaterProxyModel();
    void InitializeFireProxyModel();
    void InitializeVolumetricFireProxyModel();
    void InitializeGatePortalProxyModel();
        // 特殊マテリアル描画の共通処理をまとめます。
void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);
        // LODや代理モデルを考慮して、実際に描画するModelを決定します。
Model* ResolveDrawModel() const;
    void UpdateUvTransform();

    // 外部オブジェクトへの参照。MeshRendererは所有しない。
    Object3dCommon* common_ = nullptr;
    Transform* transform_ = nullptr;
    Vector3 visualScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 visualRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 visualOffset_ = { 0.0f, 0.0f, 0.0f };
    Vector3 visualShear_ = { 0.0f, 0.0f, 0.0f };
    float visualShearPivotY_ = 0.0f;

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
    Matrix4x4 cachedWorldMatrix_ = Math::MakeIdentity4x4();
    Matrix4x4 cachedWorldInverseTranspose_ = Math::MakeIdentity4x4();
    bool worldInverseTransposeCacheValid_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraForGPU* cameraData_ = nullptr;

    // Camera Preview用の一時描画定数です。右パネルPreviewと演出用Previewを分けて、GPU実行時の上書きを防ぎます。
    static constexpr int kPreviewBufferCount = 2;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPreviewBufferCount> previewWvpResources_;
    std::array<TransformationMatrix*, kPreviewBufferCount> previewWvpData_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPreviewBufferCount> previewCameraResources_;
    std::array<CameraForGPU*, kPreviewBufferCount> previewCameraData_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    MaterialData* materialData_ = nullptr;
    Vector4 materialColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };

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
    std::unique_ptr<Model> waterProxyModel_;
    std::unique_ptr<Model> fireProxyModel_;
    std::unique_ptr<Model> volumetricFireProxyModel_;
    std::unique_ptr<Model> gatePortalProxyModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource_;
    TransformationMatrix* shadowWvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> localFogResource_;
    LocalFogData* localFogData_ = nullptr;
    float time_ = 0.0f;

    bool isUIPreview_ = false;
};
