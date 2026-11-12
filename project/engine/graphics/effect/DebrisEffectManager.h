#pragma once

#include "Object3DCommon.h"
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

namespace engine { class Object3DCommon; }
struct ID3D12Resource;

/// <summary>破片の発生数、飛散速度、重力、衝突、見た目をまとめたプリセット設定。</summary>
// 関連：DebrisEffectManager（この型を使う機能の本体）
struct DebrisEffectConfig {
    std::string name = "rock_burst";
    std::vector<std::string> modelNames = { "Primitives/cube" };

    int spawnCount = 18;
    // 発生点からのオフセットと各軸のばらつき半幅はワールド距離。
    engine::Vector3 spawnOffset = { 0.0f, 0.4f, 0.0f };
    engine::Vector3 spawnBoxHalfExtents = engine::Vector3::ZERO;

    engine::Vector3 baseDirection = { 0.0f, 0.0f, 1.0f };
    float horizontalSpread = 1.0f;
    float verticalMin = 0.25f;
    float verticalMax = 0.9f;

    // 初速はワールド単位/秒。下の角速度はラジアン/秒。
    float speedMin = 7.0f;
    float speedMax = 16.0f; // 初速の上限（ワールド単位/秒）。

    float angularSpeedMin = 3.0f; // 角速度の下限（ラジアン/秒）。
    float angularSpeedMax = 12.0f; // 角速度の上限（ラジアン/秒）。

    float scaleMin = 0.15f;
    float scaleMax = 0.45f;

    // 生存時間は秒。フェード開始位置は全寿命に対する fadeStartRatio の割合。
    float lifetimeMin = 1.0f;
    float lifetimeMax = 1.8f; // 破片寿命の上限（秒）。

    float gravity = 28.0f;
    float airDrag = 0.08f;
    float restitution = 0.35f;
    float friction = 0.78f;
    // 地面衝突に使うワールド高さ。地形探索済みの値を発生側から渡せる。
    float groundY = 0.0f;

    float fadeStartRatio = 0.72f;

    engine::Vector4 color = { 0.62f, 0.55f, 0.48f, 1.0f };
    int materialType = 0;
    float roughness = 0.5f;
    float metallic = 0.0f;

    bool enableEnvMap = false;
    float envIntensity = 1.0f;
    float emissive = 0.0f;

    bool collideGround = true;
    bool shrinkOnFade = true;
};

// 担当：破片のプリセット、飛散・衝突・描画と再利用プールを管理する。
// 関連：DebrisEffectConfig（設定）、Model / Object3DCommon（描画）、DebrisEffectEditor（編集）
// 主な入口：Initialize / Spawn / Update / Draw / Clear
class DebrisEffectManager {
public:
    /// <summary>エンジン全体で共有する破片管理インスタンスを返す。</summary>
    static DebrisEffectManager* GetInstance();

    /// <summary>破片描画に必要な3D共通機能とGPUリソースを準備する。</summary>
    void Initialize(engine::Object3DCommon* common);

    /// <summary>生存中の破片の移動、回転、衝突とフェードを更新する。</summary>
    void Update(float deltaTime);

    /// <summary>現在表示中の破片を光源情報とともに描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr);

    /// <summary>表示中の破片をプールへ戻す。</summary>
    void Clear();

    /// <summary>指定乱数種でエディタープレビューの破片状態を初期化する。</summary>
    void ResetEditorPreview(uint32_t randomSeed = 0xD3B1u);
#ifdef USE_IMGUI
    // 元の再生状態を退避し、確認用の生成物だけを更新・破棄する。
    void BeginEditorSession();
    // 確認用の実行状態を片付け、開始前に退避した状態へ戻す。
    void EndEditorSession();
#endif


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
    void Spawn(const std::string& presetName, const engine::Vector3& position);

    /// <summary>指定した地面高を衝突位置として破片を出す。</summary>
    void SpawnOnGround(const std::string& presetName, const engine::Vector3& position, float groundY);

    /// <summary>プリセット登録を介さず設定値から破片を出す。</summary>
    void SpawnFromConfig(const DebrisEffectConfig& config, const engine::Vector3& position);

private:
    /// <summary>飛散中の破片一個の物理状態と描画用リソース。</summary>
    // 関連：DebrisEffectManager（この型を使う機能の本体）
    struct DebrisPiece {
        Model* model = nullptr;
        engine::Vector3 position = engine::Vector3::ZERO;
        engine::Vector3 rotation = engine::Vector3::ZERO;

        Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;

        MeshRenderer::TransformationMatrix* wvpData = nullptr;
        MeshRenderer::TransformationMatrix* shadowWvpData = nullptr;
        MeshRenderer::MaterialData* materialData = nullptr;

        engine::Vector3 velocity = engine::Vector3::ZERO; // 破片の移動速度（ワールド単位/秒）。
        engine::Vector3 angularVelocity = engine::Vector3::ZERO; // 破片の角速度（ラジアン/秒）。

        float age = 0.0f;
        float lifetime = 1.0f; // 破片の寿命（秒）。

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
    engine::Object3DCommon* ResolveCommon();

    // プールに残る GPU リソースを移動して取得し、空なら新規初期化へ任せる。
    DebrisPiece AcquirePiece();

    // 指定番号の破片を稼働一覧から外し、再利用プールへ戻す。
    void RecyclePiece(size_t index);

    // 確保した破片のGPU定数を準備し、移動後の描画行列を更新する。
    bool InitializePieceResources(DebrisPiece& piece, const DebrisEffectConfig& config);

    // 通常描画と影描画へ同じ world 行列を渡し、破片の向きを一致させる。
    void UpdatePieceMatrix(DebrisPiece& piece, const engine::Matrix4x4& viewProjection, const engine::Matrix4x4* lightViewProjection);

    // プリセットの乱数範囲、方向、読み込みパスを決める。
    float RandomRange(float minValue, float maxValue);

    // 候補が一つ以下なら乱数を消費せず、配列外参照を防ぐ。
    int RandomIndex(int maxExclusive);

    // 基準方向へ水平拡散と垂直範囲を加え、最後に単位方向へ正規化する。
    engine::Vector3 RandomDirection(const DebrisEffectConfig& config);

    // パス指定はそのまま使い、短い名前だけ標準プリセットディレクトリへ解決する。
    std::string ResolvePresetPath(const std::string& presetName) const;

    engine::Object3DCommon* common_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    MeshRenderer::CameraForGpu* cameraData_ = nullptr;
    std::vector<DebrisPiece> activePieces_;
    std::vector<DebrisPiece> pooledPieces_;
    std::map<std::string, DebrisEffectConfig> presets_;
    mutable std::recursive_mutex mutex_;
    mutable std::mt19937 randomEngine_;
    float timeScale_ = 1.0f; // 破片の更新時間倍率。
#ifdef USE_IMGUI
    bool editorSession_ = false;
    float savedEditorScale_ = 1; // 単体確認を始める前の時間倍率。
    decltype(activePieces_) savedEditorPieces_;
    std::mt19937 savedEditorRandom_;
#endif

};
