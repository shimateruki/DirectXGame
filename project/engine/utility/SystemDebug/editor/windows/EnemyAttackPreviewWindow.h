#pragma once

#include "IEditable.h"
#include "EnemyAttackProfile.h"
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
    void Initialize(SceneManager* sceneManager);
    void Finalize();
    void Update(float deltaTime);

    void DrawImGui() override;
    /// 選択中だけ下段へ表示する、横長の再生・スクラブ用タイムラインを描画する。
    void DrawTimelineWindow();
    std::string GetName() override { return "Enemy Attack Preview"; }
    bool IsEnabled() const { return enabled_; }
    // 他のVFXツールが設定した時間倍率より後に、敵プレビューの停止状態を優先適用します。
    void ApplyEffectPlaybackState();

private:
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

    void CreatePreviewObjects(bool requestCameraRecenter = true);
    void RemovePreviewObjects();
    void RestartPreview();
    void SeekPreview(float targetTime);
    void ClearPreviewTransientState();
    void SetEffectSimulationScale(float timeScale);
    void StepSimulation(float deltaTime);
    void ResolvePreviewProjectileHits();
    void TriggerPreviewPlayerDamage();
    DamageType GetPreviewDamageType() const;
    StatusEffectApplication GetPreviewStatusEffect() const;
    void RecordTimelineSample();
    const char* GetCurrentPhaseName() const;
    bool IsGiantHookSplitPreview() const;
    void MaintainGrounding(BaseEnemy* enemy, float groundY);
    void SyncTargetTransform();
    void MarkPreviewFamilyEditorInternal();
    void ConfigureEnemy(BaseEnemy* enemy);
    void ApplyRecommendedSettings();
    void LoadSelectedProfile();
    void SaveSelectedProfile();
    void ResetSelectedProfile();
    const EnemyAttackDefinition* GetSelectedAttackDefinition() const;
    EnemyAttackDefinition* GetSelectedAttackDefinition();
    float GetRecommendedTargetDistance() const;
    float GetRecommendedLoopDuration() const;
    const char* GetSelectedEnemyType() const;
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
