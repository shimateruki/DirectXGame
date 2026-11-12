#pragma once
#include "Model.h"
#include "Object3DCommon.h"
#include "Transform.h"
#include "engine/utility/math/Math.h"
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

namespace engine { class Object3D; }
class Camera;

// 担当：モデルの材質・見た目変形を管理し、通常・影・特殊材質を描画する。
// 関連：Object3D（所有）、Model（メッシュ）、Object3DCommon（共通描画状態）、Camera
// 主な入口：Initialize / Update / Draw / DrawSpecialMaterial / SetModel
class MeshRenderer {
public:
    /// <summary>モデル描画時にGPUへ渡す座標変換行列。</summary>
    // 関連：MeshRenderer（この型を使う機能の本体）
    struct TransformationMatrix {
        engine::Matrix4x4 wvp;
        engine::Matrix4x4 world;
        engine::Matrix4x4 worldInverseTranspose;
    };

    /// <summary>GPUへ渡す平行光源の色、向き、強度。</summary>
    // 関連：MeshRenderer（この型を使う機能の本体）
    struct DirectionalLight {
        engine::Vector4 color;
        engine::Vector3 direction;
        float intensity;
    };

    /// <summary>GPUへ渡すカメラのワールド位置。</summary>
    // 関連：MeshRenderer（この型を使う機能の本体）
    struct CameraForGpu {
        engine::Vector3 worldPosition;
        float visibilityPadding = 0;
        engine::Vector4 visibilityCenterRadius{};
        engine::Vector4 visibilityParameters{};
    };

    /// <summary>色、PBR、ポータル演出を含むGPUマテリアル定数。</summary>
    // 関連：MeshRenderer（この型を使う機能の本体）
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

    // 担当：演出前の材質定数・入力色・ブレンド方式を退避する。
    // 関連：MeshRenderer（取得・復元）。テクスチャ差し替えは退避対象に含まない。
    class MaterialState {
    public:
        MaterialState() = default;

    private:
        // 復元データを外部から改変して、材質操作APIを迂回することを防ぐ。
        friend class MeshRenderer;
        MaterialData parameters_{};
        engine::Vector4 authoringColor_{1, 1, 1, 1};
        BlendMode blendMode_ = BlendMode::None;
        bool valid_ = false;
    };

    /// <summary>ポータル・形成・消滅で使うクリップ面の設定。</summary>
    // 関連：MeshRenderer（この型を使う機能の本体）
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
    // 関連：MeshRenderer（この型を使う機能の本体）
    struct PointLight {
        engine::Vector4 color;
        engine::Vector3 position;
        float intensity;
        float radius;
        float decay;
        float padding[2];
    };

    /// <summary>GPUへ渡すスポットライトの位置、方向、照射角と減衰。</summary>
    // 関連：MeshRenderer（この型を使う機能の本体）
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
    // 関連：MeshRenderer（この型を使う機能の本体）
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
    // 関連：MeshRenderer（この型を使う機能の本体）
    struct WaterParamForGpu {
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
        engine::Vector4 visibilityCenterRadius{};
        engine::Vector4 visibilityParameters{};
        engine::Vector4 foregroundBarrierCenter{};
        engine::Vector4 foregroundBarrierParameters{};
    };

public:
    MeshRenderer(Transform* transform);

    ~MeshRenderer() = default;

    /// <summary>描画用GPUバッファと既定のマテリアルを初期化する。</summary>
    void Initialize(engine::Object3DCommon* common);
    // 所有Objectと寿命を共有する非所有参照。ゲームの材質値には保存しない。
    void SetVisibilityOwner(engine::Object3D* owner) { visibilityOwner_ = owner; }
    // 指定カメラの視界確保設定を描画用定数へ反映する。
    void UpdateCameraVisibility(Camera* camera);
    // 実画面の深度を使う追加描画。材質やプレビューの定数には書き込まない。
    void DrawVisibilitySilhouette(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>Transformとマテリアルの描画状態を更新する。</summary>
    void Update();

    // 水の時間は描画回数ではなく、シーンの経過秒数で進めます。
    void AdvanceWaterAnimation(float deltaTime);

    /// <summary>カメラに依存する描画定数だけを更新する。演出時間は進めない。</summary>
    void RefreshCameraDependentData();

    /// <summary>現在のモデルとマテリアルで通常の3Dメッシュを描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    // 演出用カメラPreview専用。通常描画のWVP/Camera定数を汚さず、別バッファで描画します。
    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex = 0);

    // 特殊マテリアルも通常描画用WVPを上書きせず、Camera Preview専用定数で描画します。
    void DrawSpecialMaterialForCamera(int materialType, Camera* camera, uint32_t depthSrvHandle, uint32_t colorSrvHandle, int previewBufferIndex = 0);

    /// <summary>現在の材質番号に対応する特殊描画を、通常カメラの定数で実行する。</summary>
    void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    // モデル設定。
    /// <summary>既存のModelを描画対象に設定する。</summary>
    void SetModel(Model* model);

    /// <summary>名前に対応するモデルを描画対象に設定する。</summary>
    void SetModel(const std::string& modelName);

    /// <summary>現在の描画モデルを返す。</summary>
    Model* GetModel() const { return model_; }

    // 元モデル・材質・コライダーを維持して、描画と影だけを一時的な形状に差し替えます。
    // 所有者は解除までModelを保持し、別モデルへの変更時は自動解除します。
    void SetVisualModelOverride(Model* model) { visualModelOverride_ = model; }
    Model* GetVisualModelOverride() const { return visualModelOverride_; }

    /// <summary>現在の描画モデル名を返す。</summary>
    const std::string& GetModelName() const { return modelName_; }

    /// <summary>描画メッシュ番号を設定する。-1なら全メッシュ。</summary>
    void SetMeshDrawIndex(int meshIndex) { meshDrawIndex_ = meshIndex; }

    /// <summary>描画メッシュ番号を返す。</summary>
    int GetMeshDrawIndex() const { return meshDrawIndex_; }

    /// <summary>特定メッシュだけを描画する設定か返す。</summary>
    bool IsMeshDrawFiltered() const { return meshDrawIndex_ >= 0; }

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

    void SetIsUiPreview(bool isPreview) { isUiPreview_ = isPreview; }

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

    WaterParamForGpu* GetWaterParamData() const { return waterParamData_; }

private:
    engine::Object3D* visibilityOwner_ = nullptr;
    float visibilityAmount_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> silhouetteResource_;
    WaterParamForGpu* silhouetteData_ = nullptr;
    uint64_t visibilitySerial_ = 0;

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

    // 指定の変換バッファ・深度・画面色で炎を描画する。
    void DrawFireWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    // 指定パイプラインで特殊マテリアルを描画する。useProxyModelは代理形状の使用、
    // bakedTextureModeは使用するベイク済みテクスチャ組（0:既定、1:ゲート）を選ぶ。
    void DrawSpecialMaterialWithWvp(ID3D12Resource* wvpResource, uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (engine::Object3DCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0, ID3D12Resource* effectResource = nullptr);

    // ワールド行列が前回と同じなら法線変換行列のキャッシュを返す。
    const engine::Matrix4x4& GetCachedWorldInverseTranspose(const engine::Matrix4x4& worldMatrix);

    // 特殊マテリアル用の代理形状を必要になったときに作る。
    void InitializeWaterProxyModel();

    // 平面型の炎で使う代理メッシュを必要時に生成する。
    void InitializeFireProxyModel();

    // 立体炎の描画範囲に使う代理モデルを準備する。
    void InitializeVolumetricFireProxyModel();

    // ポータルを描くための代理形状を元モデルとは別に保持する。
    void InitializeGatePortalProxyModel();

    // 結界の設定では平面ではなく円筒の代理形状を使う。
    void InitializeGateBarrierCylinderProxyModel();

    // 通常描画用WVPから共通の特殊マテリアル描画へ委譲する。
    void DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (engine::Object3DCommon::*setGraphicsCommand)(), bool useProxyModel = false, int bakedTextureMode = 0, ID3D12Resource* effectResource = nullptr);

    // 表示用の差し替えがあれば優先し、実際に描画するモデルを選ぶ。
    Model* ResolveDrawModel() const;

    // マテリアルへ渡すUV変換行列を更新する。
    void UpdateUvTransform();

    // 描画基盤とObject本体の姿勢は借用する。以下は描画だけに加える見た目補正。
    engine::Object3DCommon* common_ = nullptr;
    Transform* transform_ = nullptr;
    engine::Vector3 visualScale_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 visualRotation_ = engine::Vector3::ZERO;
    engine::Vector3 visualOffset_ = engine::Vector3::ZERO;
    engine::Vector3 visualShear_ = engine::Vector3::ZERO;
    float visualShearPivotY_ = 0.0f;

    // タイムラインが所有する追加姿勢への弱参照。再生終了後は寿命を延ばさない。
    std::weak_ptr<engine::Matrix4x4> timelineVisualPose_;

    // ModelはModelManagerから借用し、名前とメッシュ番号も保持する。
    Model* model_ = nullptr;
    Model* visualModelOverride_ = nullptr;
    std::string modelName_;
    int meshDrawIndex_ = -1;

    // 通常材質を描くときの合成方式。
    BlendMode blendMode_ = BlendMode::Normal;
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
    CameraForGpu* cameraData_ = nullptr;

    // Camera Preview用の一時描画定数です。右パネルPreviewと演出用Previewを分けて、GPU実行時の上書きを防ぎます。
    static constexpr int kPreviewBufferCount = 2;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPreviewBufferCount> previewWvpResources_;
    std::array<TransformationMatrix*, kPreviewBufferCount> previewWvpData_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPreviewBufferCount> previewCameraResources_;
    std::array<CameraForGpu*, kPreviewBufferCount> previewCameraData_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPreviewBufferCount> previewWaterResources_;
    std::array<WaterParamForGpu*, kPreviewBufferCount> previewWaterData_{};

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
    WaterParamForGpu* waterParamData_ = nullptr;
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
    bool isUiPreview_ = false;
    ID3D12Resource* previewDirectionalLightResource_ = nullptr;
};
