#pragma once
#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include <vector>
#include <wrl.h>
#include <string>
#include "Light.h"

// 前方宣言
class Object3d;

/// <summary>
/// ライト（点光源・スポットライト・平行光源）を一括管理するクラス
/// </summary>
class LightManager {
public:
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

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static LightManager* GetInstance();

    /// <summary>
    /// 初期化処理（バッファの生成など）
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 毎フレーム更新（追従処理やGPUへのデータ転送を行う）
    /// </summary>
    void Update();

    // --- ゲッター（リソース） ---
    ID3D12Resource* GetPointLightResource() { return pointLightResource_.Get(); }
    ID3D12Resource* GetSpotLightResource() { return spotLightResource_.Get(); }
    ID3D12Resource* GetDirectionalLightResource() { return directionalLightResource_.Get(); }

    // --- ゲッター（データ参照） ---
    DirectionalLight& GetDirectionalLight() { return directionalLightData_; }
    std::vector<PointLightInstance>& GetPointLights() { return pointLights_; }
    std::vector<SpotLightInstance>& GetSpotLights() { return spotLights_; }
    Vector4& GetSceneClearColor() { return sceneClearColor_; }
    bool IsSkyboxEnabled() const { return skyboxEnabled_; }
    void SetSkyboxEnabled(bool enabled) { skyboxEnabled_ = enabled; }
    const std::string& GetSkyboxTexturePath() const { return skyboxTexturePath_; }
    bool SetSkyboxTexturePath(const std::string& texturePath);
    uint32_t GetSkyboxTextureHandle() const { return skyboxTextureHandle_; }

    // --- ライト追加・削除 ---
    PointLightInstance* AddPointLight();
    SpotLightInstance* AddSpotLight();
    void ClearAllLights();
    void PlayPointLightPulse(
        const Vector3& position,
        const Vector4& color,
        float intensity,
        float radius,
        float duration,
        float decay = 1.0f);

    // --- ファイル保存・読み込み ---
    bool SaveState(const std::string& filename);
    bool LoadState(const std::string& filename);
    const std::string& GetCurrentStateFile() const { return currentStateFile_; }
    bool WasLastLoadSuccessful() const { return lastLoadSucceeded_; }

    uint32_t GetEnvironmentMapHandle() const { return environmentMapHandle_; }
    void SetEnvironmentMapHandle(uint32_t handle) { environmentMapHandle_ = handle; }


private:
    LightManager() = default;
    ~LightManager() = default;
    LightManager(const LightManager&) = delete;
    const LightManager& operator=(const LightManager&) = delete;

    DirectXCommon* dxCommon_ = nullptr;

    // 点光源データ
    std::vector<PointLightInstance> pointLights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLightConstData* pointLightConstData_ = nullptr;
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
    Vector4 sceneClearColor_ = { 0.52f, 0.68f, 0.84f, 1.0f };
    bool skyboxEnabled_ = true;
    std::string skyboxTexturePath_ = "Resources/output_skybox.dds";
    uint32_t skyboxTextureHandle_ = 0;
    std::string currentStateFile_ = "Resources/json/light/light_layout.json";
    bool lastLoadSucceeded_ = true;

};
