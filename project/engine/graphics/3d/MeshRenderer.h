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

namespace engine { class Object3d; }
class Camera;

/// <summary>Object3dのモデル描画、マテリアル、LOD、特殊マテリアル描画を担当する。</summary>
class MeshRenderer {
public:
    /// <summary>モデル描画時にGPUへ渡す座標変換行列。</summary>
    struct TransformationMatrix {
        engine::Matrix4x4 WVP;
        engine::Matrix4x4 world;
        engine::Matrix4x4 WorldInverseTranspose;
    };

    /// <summary>GPUへ渡す平行光源の色、向き、強度。</summary>
    struct DirectionalLight {
        engine::Vector4 color;
        engine::Vector3 direction;
        float intensity;
    };

    /// <summary>GPUへ渡すカメラのワールド位置。</summary>
    struct CameraForGPU {
        engine::Vector3 worldPosition;
    };

    /// <summary>色、PBR、ポータル演出を含むGPUマテリアル定数。</summary>
    struct MaterialData {
        engine::Vector4 color;
        int32_t enableLighting;
        float padding1[3];

        engine::Matrix4x4 uvTransform;

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
        engine::Vector3 portalClipCenter;
        float portalClipEdgeWidth;
        engine::Vector3 portalClipNormal;
        float portalClipDissolve;
        engine::Vector4 portalClipColor;
    };

    /// <summary>演出前の材質定数・入力色・ブレンド方式を保持する。テクスチャの差し替えは含まない。</summary>
    class MaterialState {
    public:
        MaterialState() = default;

    private:
        // 復元データを外部から改変して、材質操作APIを迂回することを防ぐ。
        friend class MeshRenderer;
        MaterialData parameters_{};
        engine::Vector4 authoringColor_{1, 1, 1, 1};
        BlendMode blendMode_ = BlendMode::kNone;
        bool valid_ = false;
    };

    /// <summary>ポータル・形成・消滅で使うクリップ面の設定。</summary>
    struct PortalClipSettings {
        bool enabled = false;
        float progress = 0.0f;
        engine::Vector3 center{};
        float edgeWidth = 0.12f;
        engine::Vector3 normal{0, 0, 1};
        float dissolve = 0.18f;
        engine::Vector4 color{1.0f, 0.82f, 0.36f, 0.80f};
    };

    /// <summary>GPUへ渡す点光源の位置、色、減衰パラメータ。</summary>
    struct PointLight {
        engine::Vector4 color;
        engine::Vector3 position;
        float intensity;
        float radius;
        float decay;
        float padding[2];
    };

    /// <summary>GPUへ渡すスポットライトの位置、方向、照射角と減衰。</summary>
    struct SpotLight {
        engine::Vector4 color;
        engine::Vector3 position;
        float intensity;
        engine::Vector3 direction;
        float distance;
        float decay;
        float cosAngle;
        float cosFalloffStart;
        float padding[1];
    };

    /// <summary>ローカルフォグの密度、光散乱、カメラ逆行列などのGPU定数。</summary>
    struct LocalFogData {
        engine::Vector4 fogColor = { 0.2f, 0.8f, 0.5f, 1.0f };
        engine::Vector3 cameraPos;
        float fogDensity = 0.5f;
        engine::Matrix4x4 inverseViewProj;

        float time = 0.0f;
        float edgeFade = 0.1f;

        float noiseSpeed = 0.5f;
        float noiseScale = 0.2f;

        engine::Vector3 lightDirection;
        float scatteringIntensity = 0.4f;
        engine::Vector3 lightColor;
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

        engine::Vector3 cameraWorldPosition;
        float billboardScale;

        float effectScaleX;
        float effectScaleY;
        float effectScaleZ;
        float topDownDetail;

        engine::Vector3 waterLightDirection;
        float waterLightIntensity;
        engine::Vector3 waterLightColor;
        float waterParamPadding1;

        engine::Matrix4x4 waterInverseViewProjection;

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
    void Initialize(engine::Object3dCommon* common);

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

    /// <summary>現在の材質番号に対応する特殊描画を、通常カメラの定数で実行する。</summary>
    void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

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
    void SetColor(const engine::Vector4& color);

    /// <summary>描画時のブレンドモードを設定する。</summary>
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

    /// <summary>特殊マテリアルの種類を設定する。</summary>
    void SetMaterialType(int32_t type);
    // 通常描画の共通入力で動くShader Materialを固定IDで選ぶ。
    void SetShaderMaterialId(const std::string& id) { shaderMaterialId_ = id; }
    const std::string& GetShaderMaterialId() const { return shaderMaterialId_; }

    /// <summary>平行光源の強度を設定する。</summary>
    void SetIntensity(float intensity);

    /// <summary>描画マテリアルの色を返す。</summary>
    engine::Vector4 GetColor() const;

    /// <summary>現在のマテリアル種類を返す。</summary>
    int32_t GetMaterialType() const;

    /// <summary>現在のブレンドモードを返す。</summary>
    BlendMode GetBlendMode() const { return blendMode_; }

    /// <summary>GPU定数の読み取り専用参照。変更は材質操作APIを通す。</summary>
    const MaterialData* GetMaterialData() const { return materialData_; }

    // 演出前の材質定数・入力色・合成方式を値で退避する。テクスチャ資源の所有権は含めない。
    MaterialState CaptureMaterialState() const;
    // 有効な退避値だけ復元する。GPU材質バッファ未生成時は何もしない。
    void RestoreMaterialState(const MaterialState& state);
    // クリップ面・進行率をまとめて設定する。無効な数値と法線は実装側で補正する。
    void SetPortalClip(const PortalClipSettings& settings);
    // クリップの有効フラグだけ解除し、他の材質調整値は保持する。
    void ClearPortalClip();
    void SetMaterialTime(float time);
    // 正の有限な経過秒だけを加算する。描画時刻とゲームの経過時間を明示的に同期する。
    void AdvanceMaterialTime(float deltaTime);
    void SetShininess(float shininess);
    void SetSelectedLighting(int32_t type);

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
    void SetTextureTiling(const engine::Vector2& tiling);

    /// <summary>現在のUV繰り返し倍率を返す。</summary>
    engine::Vector2 GetTextureTiling() const { return textureTiling_; }

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

    // 専用シェーダーのプレビュー描画にも、通常の3D描画と同じ独立WVPを渡す。
    bool PreparePreviewCameraData(Camera* camera, int previewBufferIndex, ID3D12Resource*& wvpResource,
                                  ID3D12Resource*& cameraResource);

    ID3D12Resource* GetCameraResource() const { return cameraResource_.Get(); }

    /// <summary>自己発光の強度を設定する。</summary>
    void SetEmissive(float emissive);

    /// <summary>自己発光の強度を返す。</summary>
    float GetEmissive() const;

    void SetIsUIPreview(bool isPreview) { isUIPreview_ = isPreview; }

    // エディターの材質撮影だけで使う平行光源。所有権は呼び出し側にある。
    void SetPreviewDirectionalLightResource(ID3D12Resource* resource) { previewDirectionalLightResource_ = resource; }

    /// <summary>ゲーム進行用Transformを変えず、描画時だけローカル姿勢を重ねる。</summary>
    void SetVisualTransform(const engine::Vector3& scale, const engine::Vector3& rotation, const engine::Vector3& offset);

    const engine::Vector3& GetVisualScale() const { return visualScale_; }

    const engine::Vector3& GetVisualRotation() const { return visualRotation_; }

    const engine::Vector3& GetVisualOffset() const { return visualOffset_; }

    /// <summary>描画に使う最終ワールド行列。装着物も身体変形・シアー・演出姿勢へ追従させる。</summary>
    engine::Matrix4x4 GetRenderWorldMatrix() const { return BuildRenderWorldMatrix(); }

    // 指定したローカルY座標を支点に、上側だけを水平方向へ傾けます。
    void SetVisualShear(const engine::Vector3& horizontalShear, float pivotY);

    const engine::Vector3& GetVisualShear() const { return visualShear_; }

    float GetVisualShearPivotY() const { return visualShearPivotY_; }

    /// <summary>描画専用姿勢とシアーを初期値へ戻す。</summary>
    void ResetVisualTransform();

    // 共通タイムラインが所有する描画専用の追加姿勢。所有元の終了・破棄で自動解除します。
    // 通常アニメーションのSetVisualTransformとは別レイヤーなので更新順で消えません。
    void SetTimelineVisualTransform(const std::shared_ptr<engine::Matrix4x4>& pose) { timelineVisualPose_ = pose; }

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
    engine::Matrix4x4 BuildRenderWorldMatrix() const;

    // 次の描画補助関数はwvpResourceを指定し、通常描画とカメラプレビューの両方から呼ぶ。
    // depthSrvHandleはシーン深度、colorSrvHandleはシーンカラーのSRV番号。
    // waterResourceを指定した場合のみ、水面パラメータの通常バッファを差し替える。
    void DrawWaterWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, ID3D12Resource* waterResource = nullptr);

    // マグマ・氷・炎はそれぞれ専用パイプラインでモデルまたは代理モデルを描画する。
    void DrawMagmaWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    // 半透明の氷も水系ルートの定数とテクスチャ配置を共用する。
    void DrawIceWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    void DrawFireWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    // 指定パイプラインで特殊マテリアルを描画する。useProxyModelは代理形状の使用、
    // bakedTextureModeは使用するベイク済みテクスチャ組（0:既定、1:ゲート）を選ぶ。
    void DrawSpecialMaterialWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (engine::Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);

    // ワールド行列が前回と同じなら法線変換行列のキャッシュを返す。
    const engine::Matrix4x4& GetCachedWorldInverseTranspose(const engine::Matrix4x4& worldMatrix);

    // 特殊マテリアル用の代理形状を必要になったときに作る。
    void InitializeWaterProxyModel();

    // 平面型の炎で使う代理メッシュを必要時に生成する。
    void InitializeFireProxyModel();

    void InitializeVolumetricFireProxyModel();

    // ポータルを描くための代理形状を元モデルとは別に保持する。
    void InitializeGatePortalProxyModel();

    // 結界の設定では平面ではなく円筒の代理形状を使う。
    void InitializeGateBarrierCylinderProxyModel();

    // 通常描画用WVPから共通の特殊マテリアル描画へ委譲する。
    void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (engine::Object3dCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0);

    // 現在のLODから実際に描画するモデルを選ぶ。
    Model* ResolveDrawModel() const;

    // マテリアルへ渡すUV変換行列を更新する。
    void UpdateUvTransform();

    // 描画基盤とObject本体の姿勢は借用する。以下は描画だけに加える見た目補正。
    engine::Object3dCommon* common_ = nullptr;
    Transform* transform_ = nullptr;
    engine::Vector3 visualScale_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 visualRotation_ = engine::Vector3::ZERO;
    engine::Vector3 visualOffset_ = engine::Vector3::ZERO;
    engine::Vector3 visualShear_ = engine::Vector3::ZERO;
    float visualShearPivotY_ = 0.0f;

    // タイムラインが所有する追加姿勢への弱参照。再生終了後は寿命を延ばさない。
    std::weak_ptr<engine::Matrix4x4> timelineVisualPose_;

    // ModelはModelManagerから借用し、名前・メッシュ番号・LOD選択を別に保持する。
    Model* model_ = nullptr;
    std::string modelName_;
    int meshDrawIndex_ = -1;
    bool lodEnabled_ = true;
    std::vector<LodLevel> lodLevels_;
    mutable int activeLodLevel_ = 0;

    // 通常材質を描くときの合成方式。
    BlendMode blendMode_ = BlendMode::kNormal;
    std::string shaderMaterialId_;

    // ワールド/View/Projection行列のGPUバッファとマップ済み書込先。
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    // 同じワールド行列なら法線変換行列を再計算しないためのキャッシュ。
    engine::Matrix4x4 cachedWorldMatrix_ = engine::Math::MakeIdentity4x4();
    engine::Matrix4x4 cachedWorldInverseTranspose_ = engine::Math::MakeIdentity4x4();
    bool worldInverseTransposeCacheValid_ = false;

    // 方向ライトとカメラ位置の定数バッファ。Dataポインタは対応バッファ内を指す。
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

    // マテリアル定数バッファと編集値の色。
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    MaterialData* materialData_ = nullptr;
    engine::Vector4 materialColor_ = engine::Colors::White;

    // 法線・ORM・基本画像のパスとTextureManagerのハンドルを対で保持する。
    std::string normalMapPath_ = "";
    uint32_t normalMapHandle_ = 0;
    std::string ormMapPath_ = "";
    uint32_t ormMapHandle_ = 0;
    std::string texturePath_ = "";
    uint32_t textureHandle_ = 0;

    // UVの繰り返し倍率と、モデル寸法から自動設定するかどうか。
    engine::Vector2 textureTiling_ = { 1.0f, 1.0f };
    bool autoTextureTiling_ = false;

    // 水面の描画パラメータと、特殊材質用の代理形状をMeshRendererが所有する。
    Microsoft::WRL::ComPtr<ID3D12Resource> waterParamResource_;
    WaterParamForGPU* waterParamData_ = nullptr;
    std::unique_ptr<Model> waterProxyModel_;
    std::unique_ptr<Model> fireProxyModel_;
    std::unique_ptr<Model> volumetricFireProxyModel_;
    std::unique_ptr<Model> gatePortalProxyModel_;
    std::unique_ptr<Model> gateBarrierCylinderProxyModel_;

    // 影パスとローカルフォグ用のGPU定数バッファ。
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource_;
    TransformationMatrix* shadowWvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> localFogResource_;
    LocalFogData* localFogData_ = nullptr;

    // 水・炎など時間で動く材質へ渡す経過秒数。
    float time_ = 0.0f;

    // UIプレビュー時だけ差し替える描画設定と外部ライトへの非所有参照。
    bool isUIPreview_ = false;
    ID3D12Resource* previewDirectionalLightResource_ = nullptr;
};
