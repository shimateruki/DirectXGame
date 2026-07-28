#pragma once

#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "engine/utility/math/Math.h"
#include "json.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
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
        kMorphActive,
        kMorphReleased,
        kControlsGuideOpened,
    };

    struct Step {
        std::string id;
        CompletionCondition condition = CompletionCondition::kTimer;
        std::string action;
        std::string cinematicPath;
        std::vector<std::string> promptIcons;
        Vector3 targetPosition = { 0.0f, 0.0f, 0.0f };
        Vector3 checkpointPosition = { 0.0f, 0.0f, 0.0f };
        float threshold = 0.0f;
        float radius = 3.0f;
        float minimumDisplayTime = 0.12f;
        bool hasTargetPosition = false;
        bool hasCheckpoint = false;
        bool showPrompt = true;
        bool requireMorph = false;
        bool previewGateUnlock = false;
        bool unlockGateOnComplete = false;
        bool spawnEnemy = false;
        std::string spawnEnemyObject;
        std::string despawnEnemyObject;
    };

    struct SpawnEnemySlot {
        std::string objectName;
        BaseEnemy* enemy = nullptr;
        Vector3 initialPosition = { 0.0f, 0.0f, 0.0f };
        uint32_t collisionAttribute = 0;
        uint32_t collisionMask = 0;
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
    bool IsActionTriggered(const std::string& action) const;
    void CompleteFlow();

    void BeginStepCinematic(const Step& step);
    void EndStepCinematic(bool skipped);
    void UpdateCinematic(float deltaTime);
    void UpdateGateStateForStep(const Step& step);
    void SetGateTransitionEnabled(bool enabled);
    void SetSpawnEnemyActive(const std::string& objectName, bool active);
    void SetAllSpawnEnemiesActive(bool active);
    void RestoreSpawnEnemyStateBeforeStep(int stepIndex);
    Object3d* FindObjectByName(const std::string& name) const;

    void BindPromptSprites();
    void ApplyStepPrompt(const Step* step);
    void UpdatePrompt(float deltaTime);
    void SetPromptVisible(bool visible);

private:
    BaseScene* scene_ = nullptr;
    Player* player_ = nullptr;
    InputManager* inputManager_ = nullptr;
    GimmickStageGate* exitGate_ = nullptr;

    std::string flowPath_;
    std::string exitGateObjectName_ = "Gimmick_StageGate";
    std::string spawnEnemyObjectName_ = "Tutorial_PinkSlime";
    std::vector<std::string> spawnEnemyObjectNames_;
    std::vector<SpawnEnemySlot> spawnEnemies_;
    std::vector<Step> steps_;
    int currentStepIndex_ = -1;
    float stepTimer_ = 0.0f;
    Vector3 stepStartPosition_ = { 0.0f, 0.0f, 0.0f };
    bool started_ = false;
    bool loaded_ = false;
    bool completed_ = false;
    bool cinematicActive_ = false;
    bool controlsGuideOpened_ = false;
    bool morphWasActiveOnStepEntry_ = false;
    Object3d* carriedEnemyOnStepEntry_ = nullptr;

    CinematicSequence cinematicSequence_;
    CinematicPlayer cinematicPlayer_;

    PromptVisual promptPanel_;
    std::array<PromptVisual, 3> promptIcons_{};
    float promptTime_ = 0.0f;
    float promptAlpha_ = 0.0f;
    bool promptRequestedVisible_ = false;

    CompletionCallback completionCallback_;
};
