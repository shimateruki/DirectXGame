#pragma once

#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "engine/utility/math/Math.h"
#include "json.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class BaseScene;
class BaseEnemy;
class GimmickStageGate;
class InputManager;
class Object3d;
class Player;
class Sprite;

// チュートリアルの手順、短時間演出、操作表示、チェックポイントをJSONから進行します。
class TutorialDirector {
public:
    using CompletionCallback = std::function<void()>;

    bool Initialize(
        BaseScene* scene,
        Player* player,
        InputManager* inputManager,
        const std::string& flowPath = "Resources/json/tutorial/tutorial_flow.json");
    void Finalize();
    void Update(float deltaTime);

    void NotifyControlsGuideOpened();
    void SetCompletionCallback(CompletionCallback callback) { completionCallback_ = std::move(callback); }

    bool IsLoaded() const { return loaded_; }
    bool IsCompleted() const { return completed_; }
    bool IsCinematicPlaying() const { return cinematicActive_; }
    int GetCurrentStepIndex() const { return currentStepIndex_; }
    const std::string& GetCurrentStepId() const;

    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

    void Restart();
    bool JumpToStep(const std::string& stepId);

#ifdef USE_IMGUI
    void DrawImGui();
#endif

private:
    enum class CompletionCondition {
        kTimer,
        kCinematicFinished,
        kMoveDistance,
        kActionTriggered,
        kReachPosition,
        kCarryEnemy,
        kEnemyThrown,
        kEnemiesDefeated,
        kMorphActive,
        kMorphReleased,
        kControlsGuideOpened,
    };

    struct Step {
        std::string id;
        CompletionCondition condition = CompletionCondition::kTimer;
        std::string action;
        std::string cinematicPath;
        std::string promptLabel;
        std::string objectiveTarget;
        std::vector<std::string> promptIcons;
        std::vector<std::string> objectiveTargets;
        std::vector<std::string> targetEnemyObjects;
        std::vector<std::string> spawnEnemyObjects;
        std::vector<std::string> despawnEnemyObjects;
        Vector3 targetPosition = { 0.0f, 0.0f, 0.0f };
        Vector3 checkpointPosition = { 0.0f, 0.0f, 0.0f };
        float threshold = 0.0f;
        float radius = 3.0f;
        float objectiveOffsetY = 0.75f;
        float minimumDisplayTime = 0.12f;
        float completionDelay = 0.0f;
        bool hasTargetPosition = false;
        bool hasCheckpoint = false;
        bool showPrompt = true;
        bool requireMorph = false;
        bool previewGateUnlock = false;
        bool unlockGateOnComplete = false;
        bool spawnEnemy = false;
        bool spawnEnemiesDormant = false;
        bool allowCarriedThrow = true;
        bool allowCarriedAbsorb = true;
        std::string spawnEnemyObject;
        std::string despawnEnemyObject;
        std::string wrongActionRetryStep;
    };

    struct SpawnEnemySlot {
        std::string objectName;
        BaseEnemy* enemy = nullptr;
        Vector3 initialPosition = { 0.0f, 0.0f, 0.0f };
        uint32_t collisionAttribute = 0;
        uint32_t collisionMask = 0;
        float maxHpOverride = -1.0f;
        float detectionRangeOverride = -1.0f;
        bool active = false;
    };

    struct PromptVisual {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool requestedVisible = false;
    };

    bool LoadFlow(const std::string& flowPath);
    static CompletionCondition ParseCondition(const std::string& conditionName);
    static bool ReadVector3(const nlohmann::json& value, Vector3& destination);

    void BeginFlowIfNeeded();
    void EnterStep(int stepIndex);
    void LeaveCurrentStep();
    void AdvanceStep();
    bool EvaluateCurrentStep() const;
    bool AreTargetEnemiesDefeated(const Step& step) const;
    bool RecoverFromWrongCarriedAction(const Step& step);
    bool IsActionTriggered(const std::string& action) const;
    void CompleteFlow();

    void BeginStepCinematic(const Step& step);
    void EndStepCinematic(bool skipped);
    void UpdateCinematic(float deltaTime);
    void UpdateGateStateForStep(const Step& step);
    void SetGateTransitionEnabled(bool enabled);
    void SetSpawnEnemyActive(const std::string& objectName, bool active);
    void SetSpawnEnemyDormant(const std::string& objectName, bool dormant);
    void SetStepSpawnEnemiesDormant(const Step& step, bool dormant);
    void SetAllSpawnEnemiesActive(bool active);
    void RestoreSpawnEnemyStateBeforeStep(int stepIndex);
    Object3d* FindObjectByName(const std::string& name) const;

    void BindPromptSprites();
    void ApplyStepPrompt(const Step* step);
    void UpdatePrompt(float deltaTime);
    void SetPromptVisible(bool visible);
    void ApplyStepObjective(const Step* step);
    void UpdateObjectiveMarker(float deltaTime);
    void SetObjectiveMarkerVisible(bool visible);

private:
    BaseScene* scene_ = nullptr;
    Player* player_ = nullptr;
    InputManager* inputManager_ = nullptr;
    GimmickStageGate* exitGate_ = nullptr;

    std::string flowPath_;
    std::string exitGateObjectName_ = "Gimmick_StageGate";
    std::string spawnEnemyObjectName_ = "Tutorial_PinkSlime";
    std::vector<std::string> spawnEnemyObjectNames_;
    std::unordered_map<std::string, float> spawnEnemyMaxHpOverrides_;
    std::unordered_map<std::string, float> spawnEnemyDetectionRangeOverrides_;
    std::vector<SpawnEnemySlot> spawnEnemies_;
    std::vector<Step> steps_;
    int currentStepIndex_ = -1;
    float stepTimer_ = 0.0f;
    float conditionSatisfiedTimer_ = 0.0f;
    Vector3 stepStartPosition_ = { 0.0f, 0.0f, 0.0f };
    bool started_ = false;
    bool loaded_ = false;
    bool completed_ = false;
    bool cinematicActive_ = false;
    bool controlsGuideOpened_ = false;
    bool stepConditionSatisfied_ = false;
    bool morphWasActiveOnStepEntry_ = false;
    Object3d* carriedEnemyOnStepEntry_ = nullptr;

    CinematicSequence cinematicSequence_;
    CinematicPlayer cinematicPlayer_;

    PromptVisual promptPanel_;
    PromptVisual promptLabel_;
    std::array<PromptVisual, 3> promptIcons_{};
    float promptAlpha_ = 0.0f;
    bool promptRequestedVisible_ = false;

    std::array<PromptVisual, 3> objectiveMarkers_{};
    std::array<std::string, 3> objectiveTargetNames_{};
    float objectiveOffsetY_ = 0.75f;
    std::array<float, 3> objectiveAlphas_{};
    float objectiveAnimationTime_ = 0.0f;
    bool objectiveRequestedVisible_ = false;

    CompletionCallback completionCallback_;
};
