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

/// <summary>Object3dのモデル描画、マテリアル、LOD、特殊マテリアル描画を担当する。</summary>
class MeshRenderer {
public:
    /// <summary>モデル描画時にGPUへ渡す座標変換行列。</summary>
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
        Matrix4x4 WorldInverseTranspose;
    };

    /// <summary>GPUへ渡す平行光源の色、向き、強度。</summary>
    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float intensity;
    };

    /// <summary>GPUへ渡すカメラのワールド位置。</summary>
    struct CameraForGPU {
        Vector3 worldPosition;
    };

    /// <summary>色、PBR、ポータル演出を含むGPUマテリアル定数。</summary>
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

    /// <summary>GPUへ渡す点光源の位置、色、減衰パラメータ。</summary>
    struct PointLight {
        Vector4 color;
        Vector3 position;
        float intensity;
        float radius;
        float decay;
        float padding[2];
    };

    /// <summary>GPUへ渡すスポットライトの位置、方向、照射角と減衰。</summary>
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

    /// <summary>ローカルフォグの密度、光散乱、カメラ逆行列などのGPU定数。</summary>
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

    /// <summary>水面と特殊マテリアルで共用する時間、変形、照明パラメータ。</summary>
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
        float topDownDetail;
        Vector3 waterLightDirection;
        float waterLightIntensity;
        Vector3 waterLightColor;
        float waterParamPadding1;
        Matrix4x4 waterInverseViewProjection;

        // 描画の切り分け専用。マテリアルやシーンには保存しません。
        float waterDebugView = 0.0f;
        float waterDebugPadding[3] = {};
    };

    /// <summary>カメラ距離で切り替えるLODの一段階。</summary>
    struct LodLevel {
        int level = 0;
        std::string modelName;
        float distance = 0.0f;
        Model* model = nullptr;
    };

public:
    /// <summary>描画対象のTransformを参照してレンダラーを作る。</summary>
    MeshRenderer(Transform* transform);
    ~MeshRenderer() = default;

    /// <summary>描画用GPUバッファと既定のマテリアルを初期化する。</summary>
    void Initialize(Object3dCommon* common);

    /// <summary>Transform、LOD、マテリアルの描画状態を更新する。</summary>
    void Update();

    // 水の時間は描画回数ではなく、シーンの経過秒数で進めます。
    void AdvanceWaterAnimation(float deltaTime);

    /// <summary>カメラに依存するLODと描画定数だけを更新する。演出時間は進めない。</summary>
    void RefreshCameraDependentData();

    /// <summary>現在のモデルとマテリアルで通常の3Dメッシュを描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    // 演出用カメラPreview専用。通常描画のWVP/Camera定数を汚さず、別バッファで描画します。
    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex = 0);

    // 特殊マテリアルも通常描画用WVPを上書きせず、Camera Preview専用定数で描画します。
    void DrawSpecialMaterialForCamera(int materialType, Camera* camera, uint32_t depthSrvHandle, uint32_t colorSrvHandle, int previewBufferIndex = 0);

    // モデルとLOD設定。
    /// <summary>既存のModelを描画対象に設定する。</summary>
    void SetModel(Model* model);

    /// <summary>名前に対応するモデルを描画対象に設定する。</summary>
    void SetModel(const std::string& modelName);

    /// <summary>現在の描画モデルを返す。</summary>
    Model* GetModel() const { return model_; }

    /// <summary>現在の描画モデル名を返す。</summary>
    const std::string& GetModelName() const { return modelName_; }

    /// <summary>描画メッシュ番号を設定する。-1なら全メッシュ。</summary>
    void SetMeshDrawIndex(int meshIndex) { meshDrawIndex_ = meshIndex; }

    /// <summary>描画メッシュ番号を返す。</summary>
    int GetMeshDrawIndex() const { return meshDrawIndex_; }

    /// <summary>特定メッシュだけを描画する設定か返す。</summary>
    bool IsMeshDrawFiltered() const { return meshDrawIndex_ >= 0; }

    /// <summary>カメラ距離に応じたLOD切り替えを有効化する。</summary>
    void SetLodEnabled(bool enabled) { lodEnabled_ = enabled; }

    /// <summary>LOD切り替えが有効か返す。</summary>
    bool IsLodEnabled() const { return lodEnabled_; }

    /// <summary>LOD段階が登録されているか返す。</summary>
    bool HasLodLevels() const { return !lodLevels_.empty(); }

    /// <summary>登録済みLOD段階を参照する。</summary>
    const std::vector<LodLevel>& GetLodLevels() const { return lodLevels_; }

    /// <summary>カメラ距離ごとのLODモデル一覧を設定する。</summary>
    void SetLodLevels(const std::vector<LodLevel>& levels);

    /// <summary>LOD段階の登録をすべて解除する。</summary>
    void ClearLodLevels();

    /// <summary>指定段階への切替距離を変更する。</summary>
    bool SetLodLevelDistance(int level, float distance);

    /// <summary>モデル名に対応するLOD定義ファイルを読み込む。</summary>
    bool LoadLodManifestForModel(const std::string& modelName);

    /// <summary>現在選択されているLOD段階を返す。</summary>
    int GetActiveLodLevel() const;

    /// <summary>現在描画するLODモデル名を返す。</summary>
    std::string GetActiveModelName() const;

    /// <summary>LOD判定で使うカメラとオブジェクトの距離を返す。</summary>
    float GetCameraDistanceToObject() const;

    // マテリアル設定。
    /// <summary>描画マテリアルの色を設定する。</summary>
    void SetColor(const Vector4& color);

    /// <summary>描画時のブレンドモードを設定する。</summary>
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

    /// <summary>特殊マテリアルの種類を設定する。</summary>
    void SetMaterialType(int32_t type);

    /// <summary>平行光源の強度を設定する。</summary>
    void SetIntensity(float intensity);

    /// <summary>描画マテリアルの色を返す。</summary>
    Vector4 GetColor() const;

    /// <summary>現在のマテリアル種類を返す。</summary>
    int32_t GetMaterialType() const;

    /// <summary>現在のブレンドモードを返す。</summary>
    BlendMode GetBlendMode() const { return blendMode_; }

    // 必要に応じてエディタや演出側から直接調整するためのデータアクセス。
    /// <summary>GPUへ送るマテリアル定数の編集用ポインタを返す。</summary>
    MaterialData* GetMaterialData() { return materialData_; }

    /// <summary>平行光源定数の編集用ポインタを返す。</summary>
    DirectionalLight* GetLightData() { return directionalLightData_; }

    /// <summary>金属度を設定する。</summary>
    void SetMetallic(float metallic);

    /// <summary>表面の粗さを設定する。</summary>
    void SetRoughness(float roughness);

    /// <summary>現在の金属度を返す。</summary>
    float GetMetallic() const;

    /// <summary>現在の表面の粗さを返す。</summary>
    float GetRoughness() const;

    /// <summary>法線マップの使用を切り替える。</summary>
    void SetEnableNormalMap(bool enable);

    /// <summary>法線マップの使用状態を返す。</summary>
    bool GetEnableNormalMap() const;

    /// <summary>法線マップを読み込み、マテリアルへ適用する。</summary>
    void SetNormalMap(const std::string& texturePath);

    /// <summary>現在の法線マップのパスを返す。</summary>
    std::string GetNormalMapPath() const { return normalMapPath_; }

    /// <summary>法線マップのSRV番号を返す。</summary>
    uint32_t GetNormalMapHandle() const { return normalMapHandle_; }

    /// <summary>ORMマップを読み込んで設定する。</summary>
    void SetOrmMap(const std::string& texturePath);

    /// <summary>現在のORMマップのパスを返す。</summary>
    std::string GetOrmMapPath() const { return ormMapPath_; }

    /// <summary>ORMマップのSRV番号を返す。</summary>
    uint32_t GetOrmMapHandle() const { return ormMapHandle_; }

    /// <summary>ベーステクスチャを読み込んで設定する。</summary>
    void SetTexture(const std::string& texturePath);

    /// <summary>現在のベーステクスチャのパスを返す。</summary>
    std::string GetTexturePath() const { return texturePath_; }

    /// <summary>ベーステクスチャのSRV番号を返す。</summary>
    uint32_t GetTextureHandle() const { return textureHandle_; }

    /// <summary>UVの繰り返し倍率を設定する。</summary>
    void SetTextureTiling(const Vector2& tiling);

    /// <summary>現在のUV繰り返し倍率を返す。</summary>
    Vector2 GetTextureTiling() const { return textureTiling_; }

    /// <summary>モデル寸法に応じたUV繰り返しを切り替える。</summary>
    void SetAutoTextureTiling(bool enabled);

    /// <summary>自動UV繰り返しが有効か返す。</summary>
    bool GetAutoTextureTiling() const { return autoTextureTiling_; }

    void SetEnableLighting(bool enable) { if (materialData_) materialData_->enableLighting = enable ? 1 : 0; }
    bool GetEnableLighting() const { return materialData_ ? (materialData_->enableLighting != 0) : false; }

    /// <summary>環境マップ反射の使用を切り替える。</summary>
    void SetEnableEnvMap(bool enable);

    /// <summary>環境マップ反射の使用状態を返す。</summary>
    bool GetEnableEnvMap() const;

    /// <summary>環境マップ反射の強度を設定する。</summary>
    void SetEnvIntensity(float intensity);

    /// <summary>環境マップ反射の強度を返す。</summary>
    float GetEnvIntensity() const;

    // 影、フォグ、水面、特殊マテリアル描画。
    /// <summary>シャドウマップ生成用の深度描画を行う。</summary>
    void DrawShadow();

    /// <summary>影描画の共通パイプライン状態を設定する。</summary>
    void SetShadowCommonState();

    /// <summary>現在のモデルを影マップへ描画する。</summary>
    void DrawShadowOnly();

    /// <summary>シーン深度を参照してローカルフォグを描画する。</summary>
    void DrawLocalFog(uint32_t depthSrvHandle);
    LocalFogData* GetLocalFogData() { return localFogData_; }
    ID3D12Resource* GetWvpResource() const { return wvpResource_.Get(); }
    ID3D12Resource* GetCameraResource() const { return cameraResource_.Get(); }

    /// <summary>自己発光の強度を設定する。</summary>
    void SetEmissive(float emissive);

    /// <summary>自己発光の強度を返す。</summary>
    float GetEmissive() const;
    void SetIsUIPreview(bool isPreview) { isUIPreview_ = isPreview; }
    // エディターの材質撮影だけで使う平行光源。所有権は呼び出し側にある。
    void SetPreviewDirectionalLightResource(ID3D12Resource* resource) { previewDirectionalLightResource_ = resource; }

    /// <summary>ゲーム進行用Transformを変えず、描画時だけローカル姿勢を重ねる。</summary>
    void SetVisualTransform(const Vector3& scale, const Vector3& rotation, const Vector3& offset);
    const Vector3& GetVisualScale() const { return visualScale_; }
    const Vector3& GetVisualRotation() const { return visualRotation_; }
    const Vector3& GetVisualOffset() const { return visualOffset_; }

    // 指定したローカルY座標を支点に、上側だけを水平方向へ傾けます。
    void SetVisualShear(const Vector3& horizontalShear, float pivotY);
    const Vector3& GetVisualShear() const { return visualShear_; }
    float GetVisualShearPivotY() const { return visualShearPivotY_; }

    /// <summary>描画専用姿勢とシアーを初期値へ戻す。</summary>
    void ResetVisualTransform();

    // 共通タイムラインが所有する描画専用の追加姿勢。所有元の終了・破棄で自動解除します。
    // 通常アニメーションのSetVisualTransformとは別レイヤーなので更新順で消えません。
    void SetTimelineVisualTransform(const std::shared_ptr<Matrix4x4>& pose) { timelineVisualPose_ = pose; }

    // 以下の特殊マテリアル描画は、depthSrvHandle にシーン深度、colorSrvHandle にシーンカラーのSRV番号を受け取る。
    // 個々の関数は対応する描画パイプラインを選び、必要なGPUバッファがなければ描画をスキップする。
    /// <summary>水面マテリアルで描画する。</summary>
    void DrawWater(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>マグママテリアルで描画する。</summary>
    void DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>氷マテリアルで描画する。</summary>
    void DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>炎マテリアルで描画する。</summary>
    void DrawFire(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>レーザーマテリアルで描画する。</summary>
    void DrawLaser(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>スライムゲルの特殊マテリアルで描画する。</summary>
    void DrawSlimeGel(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>衝撃波の特殊マテリアルで描画する。</summary>
    void DrawShockwave(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>液体接触の特殊マテリアルで描画する。</summary>
    void DrawLiquidContact(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>ダメージ亀裂の特殊マテリアルで描画する。</summary>
    void DrawDamageCrack(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>上昇気流の特殊マテリアルで描画する。</summary>
    void DrawUpdraft(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>スタン拘束の特殊マテリアルで描画する。</summary>
    void DrawStunBind(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>王冠解放の特殊マテリアルで描画する。</summary>
    void DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>毒胞子の特殊マテリアルで描画する。</summary>
    void DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>雲の特殊マテリアルで描画する。</summary>
    void DrawCloud(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>ゲートポータルの特殊マテリアルで描画する。</summary>
    void DrawGatePortal(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>風の球の特殊マテリアルで描画する。</summary>
    void DrawWindOrb(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    WaterParamForGPU* GetWaterParamData() const { return waterParamData_; }

private:
    // 描画に必要な行列、マテリアル、カメラのGPUバッファがそろっているか確認する。
    bool HasRequiredBuffers() const;

    // ゲーム進行用Transformに描画専用の姿勢補正を重ねたワールド行列を作る。
    Matrix4x4 BuildRenderWorldMatrix() const;

    // プレビュー用の独立したWVP・カメラ定数を更新し、リソースを出力する。
    // カメラやバッファ番号が無効な場合はfalseを返す。
    bool PreparePreviewCameraData(Camera* camera, int previewBufferIndex, ID3D12Resource*& wvpResource, ID3D12Resource*& cameraResource);

    // 次の描画補助関数はwvpResourceを指定し、通常描画とカメラプレビューの両方から呼ぶ。
    // depthSrvHandleはシーン深度、colorSrvHandleはシーンカラーのSRV番号。
    // waterResourceを指定した場合のみ、水面パラメータの通常バッファを差し替える。
    void DrawWaterWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, ID3D12Resource* waterResource = nullptr);

    // マグマ・氷・炎はそれぞれ専用パイプラインでモデルまたは代理モデルを描画する。
    void DrawMagmaWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawIceWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawFireWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    // 指定パイプラインで特殊マテリアルを描画する。useProxyModelは代理形状の使用、
    // bakedTextureModeは使用するベイク済みテクスチャ組（0:既定、1:ゲート）を選ぶ。
    void DrawSpecialMaterialWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);

    // ワールド行列が前回と同じなら法線変換行列のキャッシュを返す。
    const Matrix4x4& GetCachedWorldInverseTranspose(const Matrix4x4& worldMatrix);

    // 特殊マテリアル用の代理形状を必要になったときに作る。
    void InitializeWaterProxyModel();
    void InitializeFireProxyModel();
    void InitializeVolumetricFireProxyModel();
    void InitializeGatePortalProxyModel();
    void InitializeGateBarrierCylinderProxyModel();

    // 通常描画用WVPから共通の特殊マテリアル描画へ委譲する。
    void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);

    // 現在のLODから実際に描画するモデルを選ぶ。
    Model* ResolveDrawModel() const;

    // マテリアルへ渡すUV変換行列を更新する。
    void UpdateUvTransform();

    // 外部オブジェクトへの参照。MeshRendererは所有しない。
    Object3dCommon* common_ = nullptr;
    Transform* transform_ = nullptr;
    Vector3 visualScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 visualRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 visualOffset_ = { 0.0f, 0.0f, 0.0f };
    Vector3 visualShear_ = { 0.0f, 0.0f, 0.0f };
    float visualShearPivotY_ = 0.0f;
    std::weak_ptr<Matrix4x4> timelineVisualPose_;

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
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPreviewBufferCount> previewWaterResources_;
    std::array<WaterParamForGPU*, kPreviewBufferCount> previewWaterData_{};

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
    std::unique_ptr<Model> gateBarrierCylinderProxyModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource_;
    TransformationMatrix* shadowWvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> localFogResource_;
    LocalFogData* localFogData_ = nullptr;
    float time_ = 0.0f;

    bool isUIPreview_ = false;
    ID3D12Resource* previewDirectionalLightResource_ = nullptr;
};
