#pragma once

#include "IEditable.h"
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include "Event.h"
#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

class BaseEnemy;
class BaseScene;
class Object3d;
class Player;
class SceneManager;

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
    std::string GetName() override { return playerCopyMode_ ? "Player Ability Preview" : "Enemy Attack Preview"; }
    bool IsEnabled() const { return enabled_; }
    bool IsPlayerCopyMode() const { return playerCopyMode_; }
    void SetPlayerCopyMode(bool enabled);
    // 他のVFXツールが設定した時間倍率より後に、敵プレビューの停止状態を優先適用します。
    void ApplyEffectPlaybackState();

private:
    /// <summary>選択中の敵またはコピー能力のプレビュー用Actorがあるか調べます。</summary>
    bool HasPreviewActor() const { return playerCopyMode_ ? previewPlayer_ != nullptr : previewEnemy_ != nullptr; }
    /// <summary>コピー能力に固有の入力と動作を設定するUIを描画します。</summary>
    void DrawPlayerCopySettings();
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
    void SaveSelectedProfile();
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
    Object3d* previewTarget_ = nullptr;
    Player* previewPlayer_ = nullptr;
    std::vector<Object3d*> spawnedPreviewObjects_;

    bool enabled_ = false;
    bool playing_ = true;
    bool loop_ = true;
    bool showTarget_ = true;
    bool prismChantOnly_ = false;
    bool usePlayerTarget_ = true;
    bool previewSequenceCompleted_ = false;
    int enemyTypeIndex_ = 0;
    int profileAttackIndex_ = 0;
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
    Vector3 previewBaseScale_ = { 1.0f, 1.0f, 1.0f };
    std::vector<TimelineSample> timelineSamples_;
    EnemyAttackProfile editableProfile_;
    std::string profileStatus_;
    bool profileDirty_ = false;
};
