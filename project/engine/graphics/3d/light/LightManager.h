#pragma once
#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include <vector>
#include <cstddef>
#include <wrl.h>
#include <string>
#include "Light.h"

// 前方宣言
class Object3d;
class Camera;

/// <summary>
/// 平行光源・点光源・スポットライトと環境設定を共有する。
/// </summary>
class LightManager {
public:
    /// <summary>ライティングとポストエフェクトをまとめて保存する設定。</summary>
    struct EnvironmentPostSettings {
        bool bloomEnabled = true;
        int bloomQuality = 2;
        float threshold = 1.0f;
        float bloomIntensity = 0.0f;
        float spread = 1.0f;
        int enableToneMapping = 0;
        float vignetteIntensity = 0.0f;
        float vignettePower = 1.0f;
        float chromaticAberration = 0.0f;
        float filmGrainIntensity = 0.0f;
        float lutIntensity = 0.0f;
        std::string lutTexturePath = "Resources/texture/lut/soft_adventure_lut.png";
        float colorExposure = 0.0f;
        float colorContrast = 1.0f;
        float colorSaturation = 1.0f;
        float colorTemperature = 0.0f;
        float colorTint = 0.0f;
    };

    /// <summary>ライト、空、色調補正をまとめて保存・補間する設定。</summary>
    struct EnvironmentProfileState {
        DirectionalLight directionalLight{};
        Vector4 clearColor = { 0.52f, 0.68f, 0.84f, 1.0f };
        int shadowMapResolution = 2048;
        float shadowAreaSize = 80.0f;
        bool skyboxEnabled = true;
        std::string skyboxTexturePath = "Resources/output_skybox.dds";
        EnvironmentPostSettings post{};
    };

    // シェーダーで定義されている最大数と合わせる定数
    static const int kMaxPointLights = 100;
    static const int kMaxSpotLights = 100;

    /// <summary>
    /// ライトの特殊挙動モード
    /// </summary>
    enum class LightMode {
        None,       // 通常（変化なし）
        Follow,     // 特定のオブジェクトを追従する
        Flicker,    // チカチカと不規則に点滅（故障した蛍光灯など）
        SineWave,   // 周期的に明滅（呼吸のような表現）
    };

    /// <summary>
    /// 点光源のインスタンス（GPUデータ + 制御用ロジック）
    /// </summary>
    struct PointLightInstance {
        // GPUに送るデータ（位置、色、輝度など）
        MeshRenderer::PointLight data;

        // エディタ・制御用データ
        std::string name = "PointLight";   // 識別名
        LightMode mode = LightMode::None;  // 現在の挙動モード
        Object3d* target = nullptr;        // 追従対象のオブジェクト（nullptrなら追従なし）
        Vector3 offset = { 0.0f, 0.0f, 0.0f }; // 追従時の位置ズレ補正

        // アニメーション用パラメータ
        float baseIntensity = 1.0f;        // 点滅時の基準となる明るさ
        float timer = 0.0f;                // アニメーション用タイマー
        float speed = 1.0f;                // 点滅や明滅の速度
    };

    /// <summary>
    /// スポットライトのインスタンス（GPUデータ + 制御用ロジック）
    /// </summary>
    struct SpotLightInstance {
        // GPUに送るデータ
        MeshRenderer::SpotLight data;

        // エディタ・制御用データ
        std::string name = "SpotLight";
        LightMode mode = LightMode::None;
        Object3d* target = nullptr;
        Vector3 offset = { 0.0f, 0.0f, 0.0f };

        // アニメーション用パラメータ
        float baseIntensity = 1.0f;
        float timer = 0.0f;
        float speed = 1.0f;
    };

    /// <summary>
    /// 定数バッファ転送用構造体（点光源）
    /// </summary>
    struct PointLightConstData {
        MeshRenderer::PointLight lights[kMaxPointLights];
        int activeCount;   // 現在有効なライトの数
        float padding[3];  // アライメント調整
    };

    /// <summary>
    /// 定数バッファ転送用構造体（スポットライト）
    /// </summary>
    struct SpotLightConstData {
        MeshRenderer::SpotLight lights[kMaxSpotLights];
        int activeCount;
        float padding[3];
    };

    /// <summary>シーン全体で共有するライト管理インスタンスを取得する。</summary>
    static LightManager* GetInstance();

    /// <summary>ライト用GPUバッファを作成する。</summary>
    /// <param name="dxCommon">バッファ作成に使うDirectX管理インスタンス。</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>現在のライト・空・色調補正を保存用設定として取得する。</summary>
    EnvironmentProfileState CaptureEnvironmentProfile() const;

    /// <summary>現在の環境設定をファイルに保存する。</summary>
    bool SaveEnvironmentProfile(const std::string& filename) const;

    /// <summary>環境設定をファイルから読み込む。現在の設定には適用しない。</summary>
    bool LoadEnvironmentProfile(const std::string& filename, EnvironmentProfileState& profile) const;

    /// <summary>ファイルの環境設定を即時または時間をかけて適用する。</summary>
    bool ApplyEnvironmentProfile(const std::string& filename, float blendDuration = 0.0f);

    /// <summary>指定の環境設定を即時または時間をかけて適用する。</summary>
    void ApplyEnvironmentProfile(const EnvironmentProfileState& profile, float blendDuration = 0.0f);

    /// <summary>環境設定の補間を進める。</summary>
    void UpdateEnvironmentProfile(float deltaTime);

    /// <summary>環境設定を補間中かを返す。</summary>
    bool IsEnvironmentProfileBlending() const { return environmentBlendActive_; }

    /// <summary>環境設定の補間進捗を返す。</summary>
    float GetEnvironmentProfileBlendProgress() const;

    /// <summary>現在適用中の環境設定ファイルを返す。</summary>
    const std::string& GetCurrentEnvironmentProfileFile() const { return currentEnvironmentProfileFile_; }

    /// <summary>追従・点滅を更新し、ライトの値をGPUバッファへ反映する。</summary>
    void Update();

    // 描画側が定数バッファを参照するための取得口。
    ID3D12Resource* GetPointLightResource() { return pointLightResource_.Get(); }
    ID3D12Resource* GetSpotLightResource() { return spotLightResource_.Get(); }
    ID3D12Resource* GetDirectionalLightResource() { return directionalLightResource_.Get(); }

    // Editor は読み取り一覧から値を作り、SetPointLight/SetSpotLight で適用する。
    DirectionalLight& GetDirectionalLight() { return directionalLightData_; }
    const std::vector<PointLightInstance>& GetPointLights() const { return pointLights_; }
    const std::vector<SpotLightInstance>& GetSpotLights() const { return spotLights_; }

    /// <summary>GPUへ反映済みの有効な点光源数を返す。</summary>
    uint32_t GetActivePointLightCount() const {
        return pointLightConstData_ ? static_cast<uint32_t>(pointLightConstData_->activeCount) : 0;
    }

    /// <summary>GPUへ反映済みの有効なスポットライト数を返す。</summary>
    uint32_t GetActiveSpotLightCount() const {
        return spotLightConstData_ ? static_cast<uint32_t>(spotLightConstData_->activeCount) : 0;
    }

    /// <summary>指定カメラに対する平行光源の影用ビュー投影行列を取得する。</summary>
    const Matrix4x4& GetDirectionalShadowViewProjection(const Camera* camera);

    // シャドウマップ解像度と影を描く領域の大きさ。
    int GetShadowMapResolution() const;
    void SetShadowMapResolution(int resolution);
    float GetShadowAreaSize() const { return shadowAreaSize_; }
    void SetShadowAreaSize(float size);
    Vector4& GetSceneClearColor() { return sceneClearColor_; }

    /// <summary>保持しているシーンのクリア色を描画設定へ反映する。</summary>
    void ApplySceneClearColor();
    bool IsSkyboxEnabled() const { return skyboxEnabled_; }
    void SetSkyboxEnabled(bool enabled) { skyboxEnabled_ = enabled; }
    const std::string& GetSkyboxTexturePath() const { return skyboxTexturePath_; }

    /// <summary>スカイボックスのテクスチャを指定し、読み込みに成功したかを返す。</summary>
    bool SetSkyboxTexturePath(const std::string& texturePath);
    uint32_t GetSkyboxTextureHandle() const { return skyboxTextureHandle_; }

    // ライトの追加・削除と一時的な点光源演出。
    bool AddPointLight();

    /// <summary>新しいスポットライトを追加する。上限に達した場合は false。</summary>
    bool AddSpotLight();

    bool SetPointLight(std::size_t index, const PointLightInstance& value);
    bool SetSpotLight(std::size_t index, const SpotLightInstance& value);
    bool RemovePointLight(std::size_t index);
    bool RemoveSpotLight(std::size_t index);
    void ClearPointLights();
    void ClearSpotLights();

    /// <summary>登録された点光源、スポットライト、一時点光源をすべて削除する。</summary>
    void ClearAllLights();
    void PlayPointLightPulse(
        const Vector3& position,
        const Vector4& color,
        float intensity,
        float radius,
        float duration,
        float decay = 1.0f);

    // ライト配置の保存・読み込み。
    bool SaveState(const std::string& filename);

    /// <summary>ライト配置をファイルから読み込み、現在の配置へ適用する。</summary>
    bool LoadState(const std::string& filename);
    const std::string& GetCurrentStateFile() const { return currentStateFile_; }
    bool WasLastLoadSuccessful() const { return lastLoadSucceeded_; }

    uint32_t GetEnvironmentMapHandle() const { return environmentMapHandle_; }
    void SetEnvironmentMapHandle(uint32_t handle) { environmentMapHandle_ = handle; }


private:

    EnvironmentProfileState environmentBlendFrom_{};
    EnvironmentProfileState environmentBlendTo_{};
    float environmentBlendElapsed_ = 0.0f;
    float environmentBlendDuration_ = 0.0f;
    bool environmentBlendActive_ = false;
    bool environmentBlendDiscreteApplied_ = false;
    std::string currentEnvironmentProfileFile_;
    LightManager() = default;
    ~LightManager() = default;
    LightManager(const LightManager&) = delete;
    const LightManager& operator=(const LightManager&) = delete;

    DirectXCommon* dxCommon_ = nullptr;

    // 点光源データ
    std::vector<PointLightInstance> pointLights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLightConstData* pointLightConstData_ = nullptr;

    /// <summary>寿命付き点光源の基準強度と経過時間。</summary>
    struct TransientPointLight {
        MeshRenderer::PointLight data;
        float baseIntensity = 1.0f;
        float age = 0.0f;
        float duration = 0.2f;
    };
    std::vector<TransientPointLight> transientPointLights_;

    // スポットライトデータ
    std::vector<SpotLightInstance> spotLights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    SpotLightConstData* spotLightConstData_ = nullptr;

    // 平行光源（太陽）データ
    DirectionalLight directionalLightData_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    uint32_t environmentMapHandle_ = 0;
    float shadowAreaSize_ = 80.0f;
    bool shadowMatrixCacheValid_ = false;
    Vector3 cachedShadowEye_{};
    Vector3 cachedShadowDirection_{};
    float cachedShadowAreaSize_ = 0.0f;
    Vector4 sceneClearColor_ = { 0.52f, 0.68f, 0.84f, 1.0f };
    bool skyboxEnabled_ = true;
    std::string skyboxTexturePath_ = "Resources/output_skybox.dds";
    uint32_t skyboxTextureHandle_ = 0;
    std::string currentStateFile_ = "Resources/json/light/light_layout.json";
    bool lastLoadSucceeded_ = true;

};
