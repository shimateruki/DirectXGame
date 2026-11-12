#pragma once
#ifdef USE_IMGUI
#include "engine/editor/preview/SelectedActorSession.h"

#include "IEditable.h"
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include "Event.h"
#include "engine/utility/math/Math.h"

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <vector>

class BaseEnemy;
#include "game/system/rules/GameRule.h"
struct EnemyAttackPreviewDescriptor;
struct EnemyAttackPreviewOption;
struct EnemyAttackPreviewContext;
struct CopyAbilityPreviewDescriptor;
struct CopyAbilityPreviewAction;
class BaseScene;
namespace engine { class Object3D; }
class Player;
class SceneManager;
class ProjectWindow;

/// スライム系敵の実際のAIと攻撃演出を、選択位置で確認し、既存データの編集欄を提供するゲーム側の共通パネル。
// 関連：各PreviewRegistryの登録内容を使い、敵・コピー能力の本編処理をプレビューする。
// 主な入口：Initialize / Update / DrawImGui / Finalize
class ActorDebugPanel : public IEditable {
    friend void RegisterGameActorDebug();
public:
    /// <summary>現在のSceneを使ってプレビューを準備します。</summary>
    /// <param name="sceneManager">プレビュー対象のSceneを管理するインスタンス。</param>
    void Initialize(SceneManager* sceneManager);

    /// <summary>プレビュー用に生成したObjectと一時エフェクトを解放します。</summary>
    void Finalize();

    /// <summary>固定刻みのAI再生とプレビュー状態の更新を進めます。</summary>
    void Update(float deltaTime);

    /// <summary>攻撃の設定・再生操作をInspector内に描画します。</summary>
    void DrawImGui() override;

    /// <summary>選択中だけ下段へ表示する再生・スクラブ用タイムラインを描画します。</summary>
    void DrawTimelineWindow();

    /// 攻撃構成の階層を描画し、選択が変わったか返す。
    bool DrawAttackAuthoringHierarchy();

    /// 素材一覧への非所有参照を設定する。
    void SetAuthoringAssets(ProjectWindow* assets) { authoringAssets_ = assets; }

    /// Game View 上へ攻撃範囲と操作ガイドを重ねて描く。
    void DrawAttackOverlay(float x, float y, float width, float height);

    /// 攻撃配置用のビューポートが操作対象か返す。
    bool IsAuthoringViewportActive() const;

    std::string GetName() override { return crownMovieMode_ ? "Crown Movie Preview" : (playerCopyMode_ ? "Player Ability Preview" : "Enemy Attack Preview"); }

    bool IsEnabled() const { return enabled_; }

    bool IsPlayerCopyMode() const { return playerCopyMode_; }

    /// プレイヤーのコピー能力プレビューへ切り替える。
    void SetPlayerCopyMode(bool enabled);

    /// Inspectorの対象と同じ種類を開く。空文字はPlayer共通値です。
    void SelectStatusTarget(const std::string& type);

    bool IsCrownMovieMode() const { return crownMovieMode_; }

    /// 王冠ムービー確認モードへ切り替える。
    void SetCrownMovieMode();

    /// 再生中の攻撃と一時演出を停止する。
    void StopPreview();

    // 他のVFXツールが設定した時間倍率より後に、敵プレビューの停止状態を優先適用します。
    void ApplyEffectPlaybackState();

private:
#ifdef USE_IMGUI
    friend struct AttackPreviewRegistryRuntimeProbe;
    friend struct PreviewCompositionRuntimeProbe;
    friend struct StatusPreviewRuntimeProbe;
    friend struct AttackEditorRuntimeProbe; // 明示的な検証起動でUIと同じ操作を実行します。
#endif
    SelectedActorSession* session_ = nullptr;
    engine::Vector3 sourcePosition_{}, sourceRotation_{}, sourceScale_{1,1,1};
    std::unique_ptr<GameRule> previewRules_;
    bool effectsOwned_ = false;
    std::function<void()> restoreMovieCamera_;
    engine::Vector3 GetGroundPosition() const { return sourcePosition_; }
    // プレビュー用Actorを生成・接続する。既存のプレビューはReleaseRuntimeで先に回収する。
    void BuildRuntime(bool requestCameraRecenter = false);
    // 登録したプレビューActor・弾・演出を回収し、借用参照を無効にする。
    void ReleaseRuntime();
    float ResolveGroundHeight(const engine::Vector3& position, float fallback) const;
    // プレビューの表示・再生状態を準備する。保存用Profileの数値はここでは書き換えない。
    void BeginEffects();
    // 確認中の演出を終了し、次回の再生へ一時状態を持ち越さない。
    void EndEffects();
    ProjectWindow* authoringAssets_ = nullptr; // Editorが所有。素材サムネイルだけを借用します。

    /// <summary>選択中の敵またはコピー能力のプレビュー用Actorがあるか調べます。</summary>
    bool HasPreviewActor() const { return crownMovieMode_ ? crownMoviePrepared_ : (playerCopyMode_ ? previewPlayer_ != nullptr : previewEnemy_ != nullptr); }

    /// <summary>コピー能力に固有の入力と動作を設定するUIを描画します。</summary>
    void DrawPlayerCopySettings();
    void DrawSelectedInspector(ProjectWindow* assets);

    // ゲーム側の編集欄を描き、操作確定時だけプレビューを再生成します。
    bool DrawStatusSettings();
    bool statusTab_ = false; // 基本値と攻撃設定を同時に縦へ積まず、対象を共有して切り替えます。
    // 変更した共通ステータスを現在のプレビュー対象へ適用し直す。
    void RefreshStatusPreview();
    bool statusRefreshPending_ = false;

    /// 攻撃プロファイルの数値とシーケンス設定を編集する。
    void DrawAttackProfileEditor();

    /// 選択中トラックの詳細を編集する。
    void DrawTrackInspector();

    /// シーケンストラックと再生位置を描画する。
    void DrawTrackTimeline();

    /// 編集前のシーケンスを Undo 用に記録する。
    void RememberSequenceEdit(const AttackSequenceDefinition& before);

    // 保存前の表示部品だけを現在のステージに配置します。AI・接触判定は実行しません。
    void BeginPlacementPreview();

    void UpdatePlacementPreview();

    // 未確定のギズモ操作を履歴へ閉じ、仮配置のObjectとGPUプレビューを同時に片付けます。
    void StopPlacementPreview();

    /// 編集中の攻撃プロファイルを保存元から読み直す。
    void ReloadAuthoringProfile();

    /// 弾の発射・飛翔・着弾設定を編集する。
    void DrawProjectileInspector();

    /// 編集中のドラフトを指定再生位置でプレビューへ反映する。
    void PreviewDraft(bool keepTime);

    /// ドラフト変更のプレビュー再生成を予約する。
    void RequestDraftRefresh();

    /// 攻撃部品の配置操作を表示する。
    void DrawPlacementControls();

    /// Game View 内で攻撃部品の変換ギズモを操作する。
    void DrawPlacementGizmo(float x, float y, float width, float height);

    /// 選択部品の位置、回転、大きさを取得できたか返す。
    bool GetPlacementTransform(engine::Vector3& position, engine::Vector3& rotation, engine::Vector3& scale);

    /// 編集した変換を選択部品へ適用する。
    void ApplyPlacementTransform(const engine::Vector3& position, const engine::Vector3& rotation, const engine::Vector3& scale);

    // 術者はSceneの参照、生成Objectは攻撃データの選択。既存JSONの番号とは分けます。
    enum class AuthoringSelectionKind { Attack, Actor, Event, Track };

    AuthoringSelectionKind GetAuthoringSelectionKind() const;

    bool authoringActorSelected_ = false;
    bool hidePlacementActor_ = false;

    int placementOperation_ = 0; // 0=移動、1=回転、2=大きさ
    bool placementGizmoUsing_ = false;
    AttackSequenceDefinition placementGestureBefore_;

    engine::Object3D* placementObject_ = nullptr; // Sceneが所有する無害な弾モデルの表示用Object。
    std::string placementGpuSignature_;
    std::string placementMessage_;

    bool useDraftProfile_ = false;
    bool autoApplyDraft_ = true;

    bool draftRefreshPending_ = false;
    bool draftRefreshPlaying_ = false;
    float draftRefreshTime_ = 0;
    float draftRefreshDelay_ = 0;

    bool showAttackRadii_ = true;
    int projectileSection_ = 0; // 発射・本体・判定・軌跡・着弾の選択。

    EnemyAttackProfile draftProfile_; // 再生中は編集データと分離したスナップショットを使います。

    bool placementPreview_ = false;
    uint64_t placementScope_ = 0;
    std::string placementPreset_;
    float placementTime_ = 0.0f; // 選択部品の開始からの秒数。

    // ムービーも既存の固定刻み・エフェクト停止・下段ドッキングを共有します。
    bool crownMovieMode_ = false;
    bool crownMoviePrepared_ = false;
    bool crownDefeatMode_ = false;

    /// 王冠撃破ムービーの再生設定を表示する。
    void DrawCrownDefeatSettings();
    float GetCrownMovieDuration() const;

    /// 王冠ムービーの対象と再生設定を表示する。
    void DrawCrownMovieSettings();

    /// 王冠ムービーの時間軸を表示する。
    void DrawCrownMovieTimeline();

    bool playerCopyMode_ = false;
    std::string copyType_ = "Slime";
    std::string copyActionId_ = "acquire";
    float copyHoldTime_ = 1.5f;
    bool copyActionTriggered_ = false;

    const CopyAbilityPreviewDescriptor* GetCopyDescriptor() const;
    const CopyAbilityPreviewAction* GetCopyAction() const;
    void SelectCopyProfileAction();

    // 先の再生から遅れて届く生成を拒否する、生存期間だけを表すトークンです。
    struct SpawnSession {};
    std::shared_ptr<SpawnSession> spawnSession_;
    std::function<void(std::unique_ptr<BaseEnemy>)> MakePreviewSpawnCallback();
    void AddPreviewSpawn(std::unique_ptr<BaseEnemy> enemy);
    void PreparePreviewMetadata(BaseEnemy& enemy, const std::string& name);
    // 敵が追加生成するActorの登録先を、プレビューの寿命と結び付ける。
    void ConnectPreviewSpawns(BaseEnemy& enemy);
    EnemyAttackPreviewContext GetEnemyPreviewContext() const;

    /// 固定刻み再生から取得した、タイムラインの一時的な可視化データです。
    struct TimelineSample {
        float time = 0.0f;
        float speed = 0.0f;
        float height = 0.0f;
        float deformation = 0.0f;
        float hitReaction = 0.0f;
        float gpuParticleSystems = 0.0f;
        float cpuParticles = 0.0f;
        float meshEffects = 0.0f;
        float debrisPieces = 0.0f;
        std::string phase;
    };

    /// <summary>現在のステージに敵・ターゲットを生成し、必要ならカメラを合わせます。</summary>
    void CreatePreviewObjects(bool requestCameraRecenter = true);

    /// <summary>プレビューが所有するObjectをSceneから除去します。</summary>
    void RemovePreviewObjects();

    /// <summary>現在の攻撃設定を保って再生位置を先頭に戻します。</summary>
    void RestartPreview();

    /// <summary>固定フレームで先頭から進め、指定時刻の状態を再現します。</summary>
    void SeekPreview(float targetTime);

    /// <summary>弾・被弾など前回再生の一時状態を消去します。</summary>
    void ClearPreviewTransientState();

    /// <summary>プレビューとエフェクト側の時間倍率を同期します。</summary>
    void SetEffectSimulationScale(float timeScale);

    /// <summary>敵AIとエフェクトを固定フレームだけ進めます。</summary>
    void StepSimulation(float deltaTime);

    // 実戦と同じ第二形態のカメラカットを、固定刻みのプレビューへ反映します。
    void ApplySealBarrageMovieCamera();

    /// <summary>実プレイヤーを対象にした弾の命中を検出します。</summary>
    void ResolvePreviewProjectileHits();

    /// <summary>被弾確認用のイベントを実プレイヤーへ送ります。</summary>
    void TriggerPreviewPlayerDamage();

    /// <summary>選択中の攻撃に対応するダメージ種別と状態異常を返します。</summary>
    DamageType GetPreviewDamageType() const;

    StatusEffectApplication GetPreviewStatusEffect() const;

    /// <summary>現在の速度・高度・エフェクト数をタイムライン用に記録します。</summary>
    void RecordTimelineSample();

    /// <summary>実際の敵AIから現在フェーズの表示名を返します。</summary>
    const char* GetCurrentPhaseName() const;

    /// <summary>巨大スライムの拘束・分裂攻撃を表示中か調べます。</summary>
    bool IsGiantHookSplitPreview() const;

    /// <summary>プレビュー中の敵を地面に接地させます。</summary>
    void MaintainGrounding(BaseEnemy* enemy, float groundY);

    /// <summary>ターゲットの表示位置を実プレイヤーまたはダミーと同期します。</summary>
    void SyncTargetTransform();

    /// <summary>プレビュー専用ObjectをScene保存対象から除外します。</summary>
    void MarkPreviewFamilyEditorInternal();

    /// <summary>選択中の敵AIと攻撃プロファイルをプレビュー用に設定します。</summary>
    void ConfigureEnemy(BaseEnemy* enemy);

    /// <summary>選択中の攻撃に合う距離と再生時間を適用します。</summary>
    void ApplyRecommendedSettings();

    /// <summary>攻撃プロファイルの読込・保存・編集破棄を行います。</summary>
    void LoadSelectedProfile();

    /// 選択攻撃の設定を保存し、必要ならプレビューを再開する。
    void SaveSelectedProfile(bool restartPreview = true);

    /// 選択攻撃の編集値を保存済み状態へ戻す。
    void ResetSelectedProfile();

    /// <summary>選択中の攻撃定義を参照します。存在しない場合はnullptrです。</summary>
    const EnemyAttackDefinition* GetSelectedAttackDefinition() const;

    EnemyAttackDefinition* GetSelectedAttackDefinition();

    /// <summary>敵と攻撃種別に合わせた初期プレビュー距離・周期を返します。</summary>
    float GetRecommendedTargetDistance() const;

    float GetRecommendedLoopDuration() const;

    /// <summary>選択中の敵の安定した型IDを返します。</summary>
    const char* GetSelectedEnemyType() const;

    const EnemyAttackPreviewDescriptor* GetPreviewDescriptor() const;
    const EnemyAttackPreviewOption* GetPreviewOption() const;
    bool IsSealBarragePreview() const;
    bool IsAirHookSplitPreview() const;

    /// <summary>現在Sceneにプレビュー用Objectを配置できるか調べます。</summary>
    bool IsCurrentSceneReady() const;

private:
    SceneManager* sceneManager_ = nullptr;
    BaseScene* previewScene_ = nullptr; // プレビュー中に参照するシーン。パネル自身は所有しない。
    BaseEnemy* previewEnemy_ = nullptr; // プレビュー生成物への借用参照。ReleaseRuntime後は無効。
    engine::Object3D* previewTarget_ = nullptr;
    Player* previewPlayer_ = nullptr; // コピー能力プレビューの借用参照。ReleaseRuntime後は無効。
    std::vector<engine::Object3D*> spawnedPreviewObjects_;

    bool enabled_ = false;
    bool playing_ = true;
    bool loop_ = true;

    bool showTarget_ = true;
    bool prismChantOnly_ = false;
    bool usePlayerTarget_ = true;
    bool previewSequenceCompleted_ = false;

    // 一覧の並び順から独立した、保存データと同じ敵種類IDです。
    std::string enemyType_ = "Slime";
    int profileAttackIndex_ = 0;

    std::string previewOptionId_; // 保存しない追加確認項目の安定ID。
    bool sealBarrageMovieCameraPreview_ = true;

    float elapsedTime_ = 0.0f; // プレビュー開始からの再生秒数。
    float simulationAccumulator_ = 0.0f; // 固定刻みのAI更新へまだ渡していない蓄積秒数。
    float playbackSpeed_ = 1.0f; // プレビュー時間の倍率。1が等速。

    float targetDistance_ = 6.0f; // 確認対象までの距離（ワールド単位）。
    float loopDuration_ = 4.6f; // プレビューを作り直す周期（秒）。

    float timelinePixelsPerSecond_ = 150.0f; // Timelineの横方向表示倍率（ピクセル／秒）。
    bool timelineFollowPlayhead_ = true;

    bool previewHitReaction_ = false;
    bool previewHitTriggered_ = false;
    float previewHitTime_ = 1.0f;

    engine::Vector3 previewBaseScale_ = { 1.0f, 1.0f, 1.0f };

    std::vector<TimelineSample> timelineSamples_;

    EnemyAttackProfile editableProfile_;
    std::string profileStatus_;
    bool profileDirty_ = false; // 編集中の攻撃Profileに未保存の変更があるか。

    int selectedTrack_ = -1; // 選択中のトラック番号。-1は未選択。

    std::string authoringIdentity_;
    std::string authoringAssetName_ = "new_attack";

    std::vector<AttackSequenceDefinition> authoringUndo_, authoringRedo_;
    bool authoringGestureOpen_ = false; // ドラッグ操作を一回のUndo履歴にまとめているか。
    int authoringDragMode_ = 0; // 0=全体移動、1=開始端、2=終了端
};

#endif
