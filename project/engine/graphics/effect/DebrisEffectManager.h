#pragma once

#include "Object3dCommon.h"
#include "MeshRenderer.h"
#include "Model.h"
#include "engine/utility/math/Math.h"

#include <map>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include <wrl.h>

class Object3dCommon;
struct ID3D12Resource;

/// <summary>破片の発生数、飛散速度、重力、衝突、見た目をまとめたプリセット設定。</summary>
struct DebrisEffectConfig {
    std::string name = "rock_burst";
    std::vector<std::string> modelNames = { "Primitives/cube" };
    int spawnCount = 18;
    Vector3 spawnOffset = { 0.0f, 0.4f, 0.0f };
    Vector3 spawnBoxHalfExtents = { 0.0f, 0.0f, 0.0f };
    Vector3 baseDirection = { 0.0f, 0.0f, 1.0f };
    float horizontalSpread = 1.0f;
    float verticalMin = 0.25f;
    float verticalMax = 0.9f;
    float speedMin = 7.0f;
    float speedMax = 16.0f;
    float angularSpeedMin = 3.0f;
    float angularSpeedMax = 12.0f;
    float scaleMin = 0.15f;
    float scaleMax = 0.45f;
    float lifetimeMin = 1.0f;
    float lifetimeMax = 1.8f;
    float gravity = 28.0f;
    float airDrag = 0.08f;
    float restitution = 0.35f;
    float friction = 0.78f;
    float groundY = 0.0f;
    float fadeStartRatio = 0.72f;
    Vector4 color = { 0.62f, 0.55f, 0.48f, 1.0f };
    int materialType = 0;
    float roughness = 0.5f;
    float metallic = 0.0f;
    bool enableEnvMap = false;
    float envIntensity = 1.0f;
    float emissive = 0.0f;
    bool collideGround = true;
    bool shrinkOnFade = true;
};

/// <summary>破片のプリセット、発生・更新・描画と再利用プールを管理する。</summary>
class DebrisEffectManager {
public:
    /// <summary>エンジン全体で共有する破片管理インスタンスを返す。</summary>
    static DebrisEffectManager* GetInstance();

    /// <summary>破片描画に必要な3D共通機能とGPUリソースを準備する。</summary>
    void Initialize(Object3dCommon* common);

    /// <summary>生存中の破片の移動、回転、衝突とフェードを更新する。</summary>
    void Update(float deltaTime);

    /// <summary>現在表示中の破片を光源情報とともに描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr);

    /// <summary>表示中の破片をプールへ戻す。</summary>
    void Clear();

    /// <summary>指定乱数種でエディタープレビューの破片状態を初期化する。</summary>
    void ResetEditorPreview(uint32_t randomSeed = 0xD3B1u);

    /// <summary>破片更新に掛ける時間倍率を設定する。</summary>
    void SetTimeScale(float timeScale) { timeScale_ = (std::max)(0.0f, timeScale); }

    /// <summary>現在の時間倍率を返す。</summary>
    float GetTimeScale() const { return timeScale_; }

    /// <summary>表示中の破片数を返す。</summary>
    int GetActivePieceCount() const;

    /// <summary>指定フォルダ内の破片プリセットJSONを一括で読み込む。</summary>
    void LoadAllPresets(const std::string& directoryPath);

    /// <summary>単一の破片設定をファイルから読み込む。</summary>
    bool LoadConfig(const std::string& filePath, DebrisEffectConfig& outConfig) const;

    /// <summary>破片設定をファイルに保存する。</summary>
    bool SaveConfig(const std::string& filePath, const DebrisEffectConfig& config) const;

    /// <summary>名前付きの破片プリセットを登録する。</summary>
    void RegisterPreset(const std::string& presetName, const DebrisEffectConfig& config);

    /// <summary>登録済みの破片プリセット一覧を返す。</summary>
    const std::map<std::string, DebrisEffectConfig>& GetPresets() const { return presets_; }

    /// <summary>指定プリセットの破片リソースを事前に準備する。</summary>
    void PrewarmPreset(const std::string& presetName);

    /// <summary>複数プリセットの破片リソースを事前に準備する。</summary>
    void PrewarmPresets(const std::vector<std::string>& presetNames);

    /// <summary>プリセット名と発生位置を指定して破片を出す。</summary>
    void Spawn(const std::string& presetName, const Vector3& position);

    /// <summary>指定した地面高を衝突位置として破片を出す。</summary>
    void SpawnOnGround(const std::string& presetName, const Vector3& position, float groundY);

    /// <summary>プリセット登録を介さず設定値から破片を出す。</summary>
    void SpawnFromConfig(const DebrisEffectConfig& config, const Vector3& position);

private:
    /// <summary>飛散中の破片一個の物理状態と描画用リソース。</summary>
    struct DebrisPiece {
        Model* model = nullptr;
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
        MeshRenderer::TransformationMatrix* wvpData = nullptr;
        MeshRenderer::TransformationMatrix* shadowWvpData = nullptr;
        MeshRenderer::MaterialData* materialData = nullptr;
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };
        Vector3 angularVelocity = { 0.0f, 0.0f, 0.0f };
        float age = 0.0f;
        float lifetime = 1.0f;
        float baseScale = 1.0f;
        float currentScale = 1.0f;
        float gravity = 28.0f;
        float airDrag = 0.08f;
        float restitution = 0.35f;
        float friction = 0.78f;
        float groundY = 0.0f;
        float fadeStartRatio = 0.72f;
        bool collideGround = true;
        bool shrinkOnFade = true;
        bool sleeping = false;
        bool matrixDirty = true;
    };

    DebrisEffectManager();

    // 描画基盤を解決し、プールから破片を確保・返却する。
    Object3dCommon* ResolveCommon();
    DebrisPiece AcquirePiece();
    void RecyclePiece(size_t index);

    // 確保した破片のGPU定数を準備し、移動後の描画行列を更新する。
    bool InitializePieceResources(DebrisPiece& piece, const DebrisEffectConfig& config);
    void UpdatePieceMatrix(DebrisPiece& piece, const Matrix4x4& viewProjection, const Matrix4x4* lightViewProjection);

    // プリセットの乱数範囲、方向、読み込みパスを決める。
    float RandomRange(float minValue, float maxValue);
    int RandomIndex(int maxExclusive);
    Vector3 RandomDirection(const DebrisEffectConfig& config);
    std::string ResolvePresetPath(const std::string& presetName) const;

    Object3dCommon* common_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    MeshRenderer::CameraForGPU* cameraData_ = nullptr;
    std::vector<DebrisPiece> activePieces_;
    std::vector<DebrisPiece> pooledPieces_;
    std::map<std::string, DebrisEffectConfig> presets_;
    mutable std::recursive_mutex mutex_;
    mutable std::mt19937 randomEngine_;
    float timeScale_ = 1.0f;
};
