#pragma once
#include "GPUParticleSystem.h"
#include "GPUParticleConfig.h"
#include <unordered_map>
#include <map>
#include <string>
#include <memory>
#include <cstddef>
#include <vector>
#include <DirectXCommon.h>

/// <summary>GPU パーティクルのプリセット、発生、更新、描画を管理します。</summary>
class GPUParticleManager {
public:
    /// <summary>エンジンで共有する GPU パーティクル管理インスタンスを取得します。</summary>
    static GPUParticleManager* GetInstance();

    /// <summary>描画と Compute に必要な DirectX リソースを準備します。</summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>タイトルで参照するプリセットだけを読み込み、描画基盤を初期化します。</summary>
    void InitializeForTitle(DirectXCommon* dxCommon, const std::vector<std::string>& presetNames);

    /// <summary>フレーム内の更新済み状態を初期化します。</summary>
    void BeginFrame();

    /// <summary>全システムのシミュレーション時間を進めます。</summary>
    void Update(float deltaTime);

    /// <summary>Editor プレビューを指定時間だけ進めます。</summary>
    void UpdateEditorPreviewStep(float deltaTime);

    /// <summary>カメラ行列と深度情報に合わせて全 GPU パーティクルを描画します。</summary>
    void Draw(ID3D12GraphicsCommandList* commandList, const engine::Matrix4x4& viewMatrix, const engine::Matrix4x4& projectionMatrix, uint32_t dummyTexture = 0, uint32_t depthSrvHandle = 0);

    /// <summary>指定ディレクトリのプリセットを読み込みます。</summary>
    void LoadAllPresets(const std::string& directoryPath);

    /// <summary>登録済みプリセットを破棄してディレクトリから読み直します。</summary>
    void ReloadAllPresets(const std::string& directoryPath);

    /// <summary>名前で指定したプリセットのシステムを事前作成します。</summary>
    void PreloadPresetSystem(const std::string& presetName);

    /// <summary>複数プリセットのシステムを事前作成します。</summary>
    void PreloadPresetSystems(const std::vector<std::string>& presetNames);

    /// <summary>プリセットから指定位置へ粒子を発生させます。</summary>
    void Emit(const std::string& presetName, const engine::Vector3& position, const engine::Matrix4x4& emitterWorldMatrix = engine::Math::MakeIdentity4x4());

    /// <summary>速度の大きさを保ち、粒子の進行方向を指定方向へ合わせて発生させます。</summary>
    void EmitDirected(const std::string& presetName, const engine::Vector3& position, const engine::Vector3& direction,
        float speedScale = 1.0f, const engine::Matrix4x4& emitterWorldMatrix = engine::Math::MakeIdentity4x4());

    /// <summary>一時的な設定から直接粒子を発生させます。</summary>
    void EmitFromConfig(const GPUParticleConfig& config);

    // 攻撃エディターの単独配置確認。通常の粒子とはGPUバッファと時間を分離します。
    void ClearAuthoringPreview();
    bool ShowAuthoringPreview(const std::string& preset, const engine::Vector3& position,
        const engine::Vector3& direction, bool directed, float speedScale, float age);

    /// <summary>シミュレーションの時間倍率を返します。</summary>
    float GetTimeScale() const { return timeScale_; }

    /// <summary>シミュレーションの時間倍率を設定します。</summary>
    void SetTimeScale(float timeScale) { timeScale_ = timeScale; }

    /// <summary>DirectX リソースの初期化済み状態を返します。</summary>
    bool IsInitialized() const { return dxCommon_ != nullptr; }

    /// <summary>設定に必要な GPU パーティクル容量を計算します。</summary>
    static uint32_t ResolveParticleCapacity(const GPUParticleConfig& config);

    /// <summary>読み込み済みプリセットを返します。</summary>
    const std::map<std::string, GPUParticleConfig>& GetPresets() const { return presets_; }

    /// <summary>自動発生エミッターを開始し、停止用 ID を返します。</summary>
    uint32_t PlayAutoEmitter(const std::string& presetName, const engine::Vector3& position);

    /// <summary>ワールド変換を指定して自動発生エミッターを開始します。</summary>
    uint32_t PlayAutoEmitter(const std::string& presetName, const engine::Vector3& position, const engine::Matrix4x4& transform);

    /// <summary>ID で指定した自動発生エミッターを停止します。</summary>
    void StopAutoEmitter(uint32_t id);

    /// <summary>すべての自動発生エミッターを停止します。</summary>
    void ClearAllAutoEmitters();

    /// <summary>Editor の時間シークに合わせ、保持中の粒子を初期状態へ戻します。</summary>
    void ResetSimulation();

    /// <summary>GPU バッファを再利用しつつ、シーン内の実行状態を消去します。</summary>
    void ClearSceneRuntime();

    /// <summary>稼働中のシステムがないか返します。</summary>
    bool IsEmpty() const;

    /// <summary>稼働中のシステム数を返します。</summary>
    int GetActiveSystemCount() const;

    /// <summary>直近の更新に要した CPU 時間をミリ秒で返します。</summary>
    float GetLastUpdateCpuTimeMs() const { return lastUpdateCpuTimeMs_; }

    /// <summary>直近の描画に要した CPU 時間をミリ秒で返します。</summary>
    float GetLastDrawCpuTimeMs() const { return lastDrawCpuTimeMs_; }

    /// <summary>設定から推定される GPU メモリ使用量を返します。</summary>
    size_t GetEstimatedMemoryBytesForConfig(const GPUParticleConfig& config) const;

    /// <summary>背景色コピーが必要な歪みパーティクルが含まれるか返します。</summary>
    bool RequiresSceneColorCopy() const;

    /// <summary>メッシュ発生用の頂点バッファとボーン SRV を設定します。</summary>
    void SetEmitterMesh(ID3D12Resource* vb, uint32_t vCount, uint32_t vStride, uint32_t boneSrvIndex) {
        meshVb_ = vb; meshVCount_ = vCount; meshVStride_ = vStride; meshBoneSrv_ = boneSrvIndex;
    }

private:
    void LoadPresetFiles(const std::string& directoryPath, const std::vector<std::string>& presetNames);

    DirectXCommon* dxCommon_ = nullptr;

    // プリセット名から設定を引き、必要な GPU システムを共有します。
    std::map<std::string, GPUParticleConfig> presets_;
    std::string loadedPresetDirectory_;

    // 同じテクスチャとブレンドモードの粒子は 1 つの GPU システムでまとめます。
    // キーは "TexturePath_BlendModeIndex" です。
    std::unordered_map<std::string, std::unique_ptr<GPUParticleSystem>> systems_;

    // 設定に対応する共有システムを取得し、なければ作成します。
    GPUParticleSystem* GetOrCreateSystem(const GPUParticleConfig& config);

    // 自動発生エミッターの再生状態と管理 ID。
    struct AutoEmitter {
        uint32_t id;
        std::string presetName;
        engine::Vector3 position;
        engine::Matrix4x4 transform;
        float timer;
    };
    std::vector<AutoEmitter> autoEmitters_;
    uint32_t nextAutoEmitterId_ = 0;
    float timeScale_ = 1.0f;
    std::unordered_map<std::string, std::unique_ptr<GPUParticleSystem>> authoringSystems_;
    bool authoringPreviewVisible_ = false;
    bool updatedThisFrame_ = false;
    float lastUpdateCpuTimeMs_ = 0.0f;
    float lastDrawCpuTimeMs_ = 0.0f;

    // メッシュデータ保持用
    ID3D12Resource* meshVb_ = nullptr;
    uint32_t meshVCount_ = 0;
    uint32_t meshVStride_ = 0;
    uint32_t meshBoneSrv_ = 0;
};
