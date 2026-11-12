#pragma once
#include "EffectObject3D.h"
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

// 担当：メッシュVFXを事前読込し、生成・再生・描画と再利用を管理する。
// 関連：EffectObject3D（本体）、VfxSequencer（発火要求）、MeshEffectEditor（編集）
// 主な入口：Initialize / PreloadEffect / SpawnEffect / Update / Draw
class MeshEffectManager {
public:
    using EffectScopeId = uint64_t;
    static constexpr EffectScopeId kInvalidEffectScope = 0;

    /// <summary>共有のメッシュエフェクト管理インスタンスを取得する。</summary>
    static MeshEffectManager* GetInstance();

    /// <summary>エフェクト生成に必要なObject3dCommonを設定する。</summary>
    /// <remarks>commonは所有しないため、利用中は呼び出し側で保持する。</remarks>
    void Initialize(engine::Object3DCommon* common);

    /// <summary>1フレーム内の更新済み状態を初期化する。</summary>
    void BeginFrame();

    /// <summary>再生前にプリセットを読み込み、初回生成の負荷を減らす。</summary>
    void PreloadEffect(const std::string& jsonFilePath);

    /// <summary>エフェクトを更新し、寿命を終えたものを回収する。</summary>
    void Update(float deltaTime);

    /// <summary>エディタのステップ再生としてエフェクトを更新する。</summary>
    void UpdateEditorPreviewStep(float deltaTime);

    /// <summary>エフェクトの時間倍率を指定する。負値は0として扱う。</summary>
    void SetTimeScale(float timeScale) { timeScale_ = (std::max)(0.0f, timeScale); }

    /// <summary>現在の時間倍率を返す。</summary>
    float GetTimeScale() const { return timeScale_; }

    /// <summary>現在再生中のエフェクトを描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr);

    /// <summary>歪み描画のためにシーンカラーのコピーが必要かを返す。</summary>
    bool RequiresSceneColorCopy() const;

    /// <summary>能力などの所有者ごとにエフェクトをまとめるIDを作成する。</summary>
    EffectScopeId CreateEffectScope();

    /// <summary>指定スコープに属するエフェクトだけを停止する。</summary>
    void StopEffectScope(EffectScopeId scopeId);

    // 攻撃タイムラインが所有する表示物。管理側のdtを加算せず、指定秒の姿を描画します。
    bool StartTimelineEffect(EffectScopeId scopeId, const std::string& path);

    // durationが0以下ならプリセットの寿命を保ち、preservePresetScaleでJSONの拡縮も合成します。
    void EvaluateTimelineEffect(EffectScopeId scopeId, float time, float duration,
        const engine::Vector3& position, const engine::Vector3& rotation, const engine::Vector3& scale,
        bool preservePresetScale = false);

    /// <summary>対象Object3dに追従するメッシュエフェクトを生成する。</summary>
    void SpawnEffect(const std::string& jsonFilePath, engine::Object3D* baseObject = nullptr, const engine::Vector3& extOffset = { 0,0,0 }, const engine::Vector3& extRot = { 0,0,0 }, const engine::Vector3& extScale = { 1,1,1 });

    /// <summary>JSON側の配置値に代えて、指定のワールド座標・回転で生成する。</summary>
    void SpawnEffectAt(const std::string& jsonFilePath, const engine::Vector3& worldPos, const engine::Vector3& worldRot,
        const engine::Vector3& scale = { 1,1,1 }, float lifetimeOverride = -1.0f);

    /// <summary>指定スコープに属するエフェクトをワールド座標で生成する。</summary>
    void SpawnEffectAtScoped(EffectScopeId scopeId, const std::string& jsonFilePath,
        const engine::Vector3& worldPos, const engine::Vector3& worldRot,
        const engine::Vector3& scale = { 1,1,1 }, float lifetimeOverride = -1.0f);

    /// <summary>リング状の波紋エフェクトを発生させる。</summary>
    void SpawnRingWaveEffect(const engine::Vector3& position);

    /// <summary>円柱状のポータルエフェクトを一定時間発生させる。</summary>
    void SpawnPortalEffect(const engine::Vector3& position, float lifetime = 3.0f);

    /// <summary>初期化状態とプリロードを維持し、再生中のエフェクトだけを消す。</summary>
    void ClearActiveEffects();
#ifdef USE_IMGUI
    // 元の再生状態を退避し、確認用の生成物だけを更新・破棄する。
    void BeginEditorSession();
    // 確認用の実行状態を片付け、開始前に退避した状態へ戻す。
    void EndEditorSession();
#endif


    /// <summary>シーン切り替え時にエフェクト・プリロード・生成元参照を破棄する。</summary>
    void Clear();

    /// <summary>現在再生中のエフェクトを返す。</summary>
    const std::vector<std::unique_ptr<EffectObject3D>>& GetActiveEffects() const { return activeEffects_; }

    /// <summary>エディタで表示中のエフェクトを指定する。所有しない。</summary>
    void SetPreviewEffectForDebug(EffectObject3D* effect) { previewEffectForDebug_ = effect; }

    /// <summary>エディタで表示中のエフェクトを返す。</summary>
    EffectObject3D* GetPreviewEffectForDebug() const { return previewEffectForDebug_; }

private:
    MeshEffectManager() = default;

    ~MeshEffectManager() = default;

    MeshEffectManager(const MeshEffectManager&) = delete;

    MeshEffectManager& operator=(const MeshEffectManager&) = delete;

    // 単発の表示物だけを回収し、GPU定数バッファの再確保を避けます。
    void RecycleEffect(std::unique_ptr<EffectObject3D> effect);
    std::vector<std::unique_ptr<EffectObject3D>> recycledEffects_;
    std::unordered_set<EffectObject3D*> reusableEffects_;
    static constexpr size_t kMaxRecycledEffects = 32;

    engine::Object3DCommon* common_ = nullptr;

    // 現在再生中のエフェクトリスト
    std::vector<std::unique_ptr<EffectObject3D>> activeEffects_;
    std::unordered_map<EffectObject3D*, EffectScopeId> effectScopes_;
    std::unordered_set<EffectScopeId> timelineScopes_; // 時間軸再生から作成したエフェクト範囲ID。
    EffectScopeId nextEffectScopeId_ = 1;
    EffectObject3D* previewEffectForDebug_ = nullptr;
    std::unordered_set<std::string> preloadedEffects_;
    bool updatedThisFrame_ = false;
    float timeScale_ = 1.0f; // メッシュVFXの更新時間倍率。

    /// <summary>タイムライン編集時に各エフェクトへ加える位置と回転の補正値。</summary>
    // 関連：MeshEffectManager（この型を使う機能の本体）
    struct TimelineOffset { engine::Vector3 position, rotation; };

    std::unordered_map<EffectObject3D*, TimelineOffset> timelineOffsets_; // 時間軸編集の位置・回転補正。
#ifdef USE_IMGUI
    EffectObject3D* savedEditorDebugEffect_ = nullptr;
    bool editorSession_ = false;
    float savedEditorScale_ = 1; // 確認開始前の時間倍率。
    decltype(activeEffects_) savedEditorEffects_;
    decltype(effectScopes_) savedEditorScopes_;
    decltype(timelineScopes_) savedEditorTimelineScopes_;
    decltype(timelineOffsets_) savedEditorOffsets_;
#endif


};
