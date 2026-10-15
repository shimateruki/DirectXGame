#pragma once

#include "IEditable.h"
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include "Event.h"
#include "engine/utility/math/Math.h"

#include <string>
#include <cstdint>
#include <vector>

class BaseEnemy;
class BaseScene;
namespace engine { class Object3d; }
class Player;
class SceneManager;
class ProjectWindow;

/// スライム系敵の実際のAIと攻撃演出を、隔離ステージ上で確認するためのプレビューウィンドウ。
class EnemyAttackPreviewWindow : public IEditable {
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
    bool IsCrownMovieMode() const { return crownMovieMode_; }
    /// 王冠ムービー確認モードへ切り替える。
    void SetCrownMovieMode();
    /// 再生中の攻撃と一時演出を停止する。
    void StopPreview();
    // 他のVFXツールが設定した時間倍率より後に、敵プレビューの停止状態を優先適用します。
    void ApplyEffectPlaybackState();

private:
#ifdef USE_IMGUI
    friend struct AttackEditorRuntimeProbe; // 明示的な検証起動でUIと同じ操作を実行します。
#endif
    ProjectWindow* authoringAssets_ = nullptr; // Editorが所有。素材サムネイルだけを借用します。
    /// <summary>選択中の敵またはコピー能力のプレビュー用Actorがあるか調べます。</summary>
    bool HasPreviewActor() const { return crownMovieMode_ ? crownMoviePrepared_ : (playerCopyMode_ ? previewPlayer_ != nullptr : previewEnemy_ != nullptr); }
    /// <summary>コピー能力に固有の入力と動作を設定するUIを描画します。</summary>
    void DrawPlayerCopySettings();
    /// 攻撃プロファイルの数値とシーケンス設定を編集する。
    void DrawAttackProfileEditor();
    /// 選択中トラックの詳細を編集する。
    void DrawTrackInspector();
    /// シーケンストラックと再生位置を描画する。
    void DrawTrackTimeline();
    /// 編集前のシーケンスを Undo 用に記録する。
    void RememberSequenceEdit(const AttackSequenceDefinition& before);
    // 保存前の表示部品だけを隔離ステージに配置します。AI・接触判定は実行しません。
    void BeginPlacementPreview();
    void UpdatePlacementPreview();
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
    engine::Object3d* placementObject_ = nullptr; // Sceneが所有する無害な弾モデルの表示用Object。
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
    /// 王冠ムービーの対象と再生設定を表示する。
    void DrawCrownMovieSettings();
    /// 王冠ムービーの時間軸を表示する。
    void DrawCrownMovieTimeline();
    bool playerCopyMode_ = false;
    int copyTypeIndex_ = 0;
    int copyActionIndex_ = 0;
    float copyHoldTime_ = 1.5f;
    bool copyActionTriggered_ = false;
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

    /// <summary>隔離ステージに敵・ターゲットを生成し、必要ならカメラを合わせます。</summary>
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
    /// <summary>現在Sceneにプレビュー用Objectを配置できるか調べます。</summary>
    bool IsCurrentSceneReady() const;

private:
    SceneManager* sceneManager_ = nullptr;
    BaseScene* previewScene_ = nullptr;
    BaseEnemy* previewEnemy_ = nullptr;
    engine::Object3d* previewTarget_ = nullptr;
    Player* previewPlayer_ = nullptr;
    std::vector<engine::Object3d*> spawnedPreviewObjects_;

    bool enabled_ = false;
    bool playing_ = true;
    bool loop_ = true;
    bool showTarget_ = true;
    bool prismChantOnly_ = false;
    bool usePlayerTarget_ = true;
    bool previewSequenceCompleted_ = false;
    int enemyTypeIndex_ = 0;
    int profileAttackIndex_ = 0;
    bool falseKingSealBarragePreview_ = false;
    bool sealBarrageMovieCameraPreview_ = true;
    int giantPreviewModeIndex_ = 0;
    float elapsedTime_ = 0.0f;
    float simulationAccumulator_ = 0.0f;
    float playbackSpeed_ = 1.0f;
    float targetDistance_ = 6.0f;
    float loopDuration_ = 4.6f;
    float timelinePixelsPerSecond_ = 150.0f;
    bool timelineFollowPlayhead_ = true;
    bool previewHitReaction_ = false;
    bool previewHitTriggered_ = false;
    float previewHitTime_ = 1.0f;
    engine::Vector3 previewBaseScale_ = { 1.0f, 1.0f, 1.0f };
    std::vector<TimelineSample> timelineSamples_;
    EnemyAttackProfile editableProfile_;
    std::string profileStatus_;
    bool profileDirty_ = false;
    int selectedTrack_ = -1;
    std::string authoringIdentity_;
    std::string authoringAssetName_ = "new_attack";
    std::vector<AttackSequenceDefinition> authoringUndo_, authoringRedo_;
    bool authoringGestureOpen_ = false;
    int authoringDragMode_ = 0; // 0=全体移動、1=開始端、2=終了端
};
