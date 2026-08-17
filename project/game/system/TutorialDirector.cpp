#define NOMINMAX
#include "TutorialDirector.h"

#include "BaseScene.h"
#include "BaseEnemy.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "GimmickStageGate.h"
#include "InputManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "TextureManager.h"
#include "WinApp.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace {
constexpr float kPromptFadeResponse = 9.0f;
constexpr float kObjectiveFadeResponse = 10.0f;
constexpr float kObjectiveScreenMargin = 54.0f;

float HorizontalDistance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void SetFullTextureRect(Sprite* sprite, const std::string& texturePath, const Vector2& maxSize) {
    if (!sprite || texturePath.empty()) {
        return;
    }

    const uint32_t handle = Sprite::LoadTexture(texturePath);
    if (handle == 0) {
        return;
    }
    sprite->SetTextureHandle(handle);
    sprite->SetTextureName(texturePath);
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
    sprite->SetTextureRect(
        { 0.0f, 0.0f },
        { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });

    const float textureWidth = static_cast<float>((std::max)(metadata.width, size_t{ 1 }));
    const float textureHeight = static_cast<float>((std::max)(metadata.height, size_t{ 1 }));
    const float fitScale = (std::min)(maxSize.x / textureWidth, maxSize.y / textureHeight);
    sprite->SetSize({ textureWidth * fitScale, textureHeight * fitScale });
}
}

bool TutorialDirector::Initialize(
    BaseScene* scene,
    Player* player,
    InputManager* inputManager,
    const std::string& flowPath) {
    Finalize();
    scene_ = scene;
    player_ = player;
    inputManager_ = inputManager;
    flowPath_ = flowPath;

    if (!scene_ || !player_ || !inputManager_ || !LoadFlow(flowPath_)) {
        DebugConsole::GetInstance()->AddLog(
            std::string("[Tutorial] Flowの初期化に失敗しました: ") + flowPath_);
        return false;
    }

    BindPromptSprites();
    exitGate_ = dynamic_cast<GimmickStageGate*>(FindObjectByName(exitGateObjectName_));
    SetGateTransitionEnabled(false);
    spawnEnemies_.clear();
    for (const std::string& objectName : spawnEnemyObjectNames_) {
        auto* enemy = dynamic_cast<BaseEnemy*>(FindObjectByName(objectName));
        if (!enemy) {
            DebugConsole::GetInstance()->AddLog("[Tutorial] Spawn enemy not found: " + objectName);
            continue;
        }

        SpawnEnemySlot slot;
        slot.objectName = objectName;
        slot.enemy = enemy;
        slot.initialPosition = enemy->GetWorldPosition();
        slot.collisionAttribute = enemy->GetCollisionAttribute();
        slot.collisionMask = enemy->GetCollisionMask();
        const auto hpOverride = spawnEnemyMaxHpOverrides_.find(objectName);
        if (hpOverride != spawnEnemyMaxHpOverrides_.end()) {
            slot.maxHpOverride = hpOverride->second;
        }
        const auto detectionOverride = spawnEnemyDetectionRangeOverrides_.find(objectName);
        if (detectionOverride != spawnEnemyDetectionRangeOverrides_.end()) {
            slot.detectionRangeOverride = detectionOverride->second;
        }
        spawnEnemies_.push_back(std::move(slot));
    }

    // 最初の更新より前に描画されても、チュートリアル用の敵が見えない状態にします。
    SetAllSpawnEnemiesActive(false);

    cinematicPlayer_.Initialize(SceneManager::GetInstance());
    cinematicPlayer_.SetSequence(&cinematicSequence_);
    loaded_ = true;
    return true;
}

void TutorialDirector::Finalize() {
    if (cinematicActive_) {
        EndStepCinematic(true);
    }
    if (player_) {
        player_->SetTutorialCarryActionPermissions(true, true);
    }
    SetPromptVisible(false);
    SetObjectiveMarkerVisible(false);
    steps_.clear();
    currentStepIndex_ = -1;
    scene_ = nullptr;
    player_ = nullptr;
    inputManager_ = nullptr;
    exitGate_ = nullptr;
    flowPath_.clear();
    spawnEnemyObjectName_ = "Tutorial_PinkSlime";
    spawnEnemyObjectNames_.clear();
    spawnEnemyMaxHpOverrides_.clear();
    spawnEnemyDetectionRangeOverrides_.clear();
    spawnEnemies_.clear();
    started_ = false;
    loaded_ = false;
    completed_ = false;
    cinematicActive_ = false;
    controlsGuideOpened_ = false;
    stepConditionSatisfied_ = false;
    conditionSatisfiedTimer_ = 0.0f;
    morphWasActiveOnStepEntry_ = false;
    carriedEnemyOnStepEntry_ = nullptr;
    promptPanel_ = {};
    promptLabel_ = {};
    promptIcons_ = {};
    promptAlpha_ = 0.0f;
    promptRequestedVisible_ = false;
    objectiveMarkers_ = {};
    objectiveTargetNames_ = {};
    objectiveOffsetY_ = 0.75f;
    objectiveAlphas_ = {};
    objectiveAnimationTime_ = 0.0f;
    objectiveRequestedVisible_ = false;
    completionCallback_ = {};
    cinematicSequence_.Clear();
}

void TutorialDirector::Update(float deltaTime) {
    if (!loaded_ || completed_ || !player_) {
        UpdatePrompt(deltaTime);
        UpdateObjectiveMarker(deltaTime);
        return;
    }

    // 編集停止中は最初のシネマティックを開始せず、自由カメラを維持する。
    if (!started_ && deltaTime <= 0.0f) {
        UpdatePrompt(0.0f);
        UpdateObjectiveMarker(0.0f);
        return;
    }

    BeginFlowIfNeeded();
    if (currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return;
    }

    stepTimer_ += (std::max)(0.0f, deltaTime);
    UpdateCinematic(deltaTime);
    UpdatePrompt(deltaTime);
    UpdateObjectiveMarker(deltaTime);

    const Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
    if (RecoverFromWrongCarriedAction(step)) {
        return;
    }
    if (!stepConditionSatisfied_ && EvaluateCurrentStep()) {
        stepConditionSatisfied_ = true;
        conditionSatisfiedTimer_ = 0.0f;
        SetPromptVisible(false);
    }
    if (stepConditionSatisfied_) {
        conditionSatisfiedTimer_ += (std::max)(0.0f, deltaTime);
    }
    if (stepConditionSatisfied_ &&
        stepTimer_ >= step.minimumDisplayTime &&
        conditionSatisfiedTimer_ >= step.completionDelay) {
        AdvanceStep();
    }
}

void TutorialDirector::NotifyControlsGuideOpened() {
    controlsGuideOpened_ = true;
}

const std::string& TutorialDirector::GetCurrentStepId() const {
    static const std::string kNone = "none";
    if (currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return kNone;
    }
    return steps_[static_cast<size_t>(currentStepIndex_)].id;
}

void TutorialDirector::CaptureReplayState(json& state) const {
    const auto objectName = [](const Object3d* object) {
        return object ? object->GetName() : std::string{};
    };

    state = {
        { "currentStepIndex", currentStepIndex_ },
        { "stepTimer", stepTimer_ },
        { "conditionSatisfiedTimer", conditionSatisfiedTimer_ },
        { "stepStartPosition", { stepStartPosition_.x, stepStartPosition_.y, stepStartPosition_.z } },
        { "started", started_ },
        { "completed", completed_ },
        { "cinematicActive", cinematicActive_ },
        { "cinematicTime", cinematicPlayer_.GetCurrentTime() },
        { "controlsGuideOpened", controlsGuideOpened_ },
        { "stepConditionSatisfied", stepConditionSatisfied_ },
        { "morphWasActiveOnStepEntry", morphWasActiveOnStepEntry_ },
        { "carriedEnemyOnStepEntry", objectName(carriedEnemyOnStepEntry_) },
        { "playerCarriedEnemy", player_ ? objectName(player_->GetCarriedEnemy()) : std::string{} },
        { "promptAlpha", promptAlpha_ },
        { "promptRequestedVisible", promptRequestedVisible_ },
        { "objectiveTargetNames", objectiveTargetNames_ },
        { "objectiveOffsetY", objectiveOffsetY_ },
        { "objectiveAlphas", objectiveAlphas_ },
        { "objectiveAnimationTime", objectiveAnimationTime_ },
        { "objectiveRequestedVisible", objectiveRequestedVisible_ }
    };
}

void TutorialDirector::RestoreReplayState(const json& state) {
    if (!loaded_ || !state.is_object()) {
        return;
    }

    // シネマティックのカメラトラックを残さず、保存状態から組み直します。
    cinematicPlayer_.Stop(false);
    cinematicActive_ = false;
    if (player_ && player_->IsCinematicLocked()) {
        player_->EndCinematicLock(true);
    }

    const int maxStepIndex = static_cast<int>(steps_.size());
    currentStepIndex_ = (std::clamp)(state.value("currentStepIndex", -1), -1, maxStepIndex);
    stepTimer_ = (std::max)(0.0f, state.value("stepTimer", 0.0f));
    conditionSatisfiedTimer_ = (std::max)(0.0f, state.value("conditionSatisfiedTimer", 0.0f));
    if (const auto found = state.find("stepStartPosition"); found != state.end()) {
        ReadVector3(*found, stepStartPosition_);
    }
    started_ = state.value("started", false);
    completed_ = state.value("completed", false);
    controlsGuideOpened_ = state.value("controlsGuideOpened", false);
    stepConditionSatisfied_ = state.value("stepConditionSatisfied", false);
    morphWasActiveOnStepEntry_ = state.value("morphWasActiveOnStepEntry", false);
    carriedEnemyOnStepEntry_ = FindObjectByName(state.value("carriedEnemyOnStepEntry", std::string{}));

    const Step* currentStep = currentStepIndex_ >= 0 && currentStepIndex_ < maxStepIndex
        ? &steps_[static_cast<size_t>(currentStepIndex_)]
        : nullptr;
    ApplyStepPrompt(currentStep);
    ApplyStepObjective(currentStep);

    promptAlpha_ = (std::clamp)(state.value("promptAlpha", 0.0f), 0.0f, 1.0f);
    promptRequestedVisible_ = state.value("promptRequestedVisible", false);
    objectiveOffsetY_ = state.value("objectiveOffsetY", objectiveOffsetY_);
    objectiveAnimationTime_ = (std::max)(0.0f, state.value("objectiveAnimationTime", 0.0f));
    objectiveRequestedVisible_ = state.value("objectiveRequestedVisible", false);
    if (const auto found = state.find("objectiveTargetNames"); found != state.end() && found->is_array()) {
        for (size_t index = 0; index < objectiveTargetNames_.size() && index < found->size(); ++index) {
            if ((*found)[index].is_string()) {
                objectiveTargetNames_[index] = (*found)[index].get<std::string>();
            }
        }
    }
    if (const auto found = state.find("objectiveAlphas"); found != state.end() && found->is_array()) {
        for (size_t index = 0; index < objectiveAlphas_.size() && index < found->size(); ++index) {
            objectiveAlphas_[index] = (std::clamp)((*found)[index].get<float>(), 0.0f, 1.0f);
        }
    }

    if (player_) {
        if (!started_) {
            // Play In Editor停止時に、投擲・吸収の参照も編集開始前へ戻します。
            player_->ResetTutorialCarryActionState();
            player_->SetTutorialCarryActionPermissions(true, true);
        }
        else if (currentStep) {
            player_->SetTutorialCarryActionPermissions(
                currentStep->allowCarriedThrow,
                currentStep->allowCarriedAbsorb);

            const std::string carriedEnemyName = state.value("playerCarriedEnemy", std::string{});
            Object3d* savedCarriedEnemy = FindObjectByName(carriedEnemyName);
            if (savedCarriedEnemy) {
                player_->SetCarriedEnemy(savedCarriedEnemy);
            }
            else if (player_->GetCarriedEnemy()) {
                player_->ReleaseCarriedEnemy(true);
            }
        }
        else {
            player_->SetTutorialCarryActionPermissions(true, true);
        }
    }

    const bool shouldRestoreCinematic =
        currentStep && !currentStep->cinematicPath.empty() && state.value("cinematicActive", false);
    cinematicSequence_.Clear();
    if (shouldRestoreCinematic && cinematicSequence_.Load(currentStep->cinematicPath)) {
        cinematicPlayer_.SetSequence(&cinematicSequence_);
        cinematicPlayer_.Play(false);
        cinematicPlayer_.SetTime(
            (std::max)(0.0f, state.value("cinematicTime", 0.0f)),
            false);
        cinematicActive_ = cinematicPlayer_.IsPlaying();
        if (cinematicActive_ && player_ && !player_->IsCinematicLocked()) {
            player_->BeginCinematicLock();
        }
    }
}

void TutorialDirector::Restart() {
    if (!loaded_) {
        return;
    }
    LeaveCurrentStep();
    player_->ResetTutorialCarryActionState();
    player_->SetTutorialSafetyEnabled(true);
    player_->SetTutorialCarryActionPermissions(true, true);
    completed_ = false;
    started_ = false;
    currentStepIndex_ = -1;
    stepTimer_ = 0.0f;
    stepStartPosition_ = player_->GetWorldPosition();
    controlsGuideOpened_ = false;
    stepConditionSatisfied_ = false;
    conditionSatisfiedTimer_ = 0.0f;
    morphWasActiveOnStepEntry_ = false;
    carriedEnemyOnStepEntry_ = nullptr;
    promptAlpha_ = 0.0f;
    objectiveAlphas_ = {};
    objectiveAnimationTime_ = 0.0f;
    SetGateTransitionEnabled(false);
    SetAllSpawnEnemiesActive(false);
}

bool TutorialDirector::JumpToStep(const std::string& stepId) {
    for (size_t index = 0; index < steps_.size(); ++index) {
        if (steps_[index].id != stepId) {
            continue;
        }
        LeaveCurrentStep();
        started_ = true;
        completed_ = false;
        RestoreSpawnEnemyStateBeforeStep(static_cast<int>(index));
        EnterStep(static_cast<int>(index));
        return true;
    }
    return false;
}

bool TutorialDirector::LoadFlow(const std::string& flowPath) {
    std::ifstream file(flowPath);
    if (!file.is_open()) {
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (...) {
        return false;
    }

    if (!root.contains("steps") || !root["steps"].is_array()) {
        return false;
    }

    exitGateObjectName_ = root.value("exitGateObject", "Gimmick_StageGate");
    spawnEnemyObjectName_ = root.value("spawnEnemyObject", "Tutorial_PinkSlime");
    spawnEnemyObjectNames_.clear();
    if (root.contains("spawnEnemyObjects") && root["spawnEnemyObjects"].is_array()) {
        for (const auto& value : root["spawnEnemyObjects"]) {
            if (value.is_string()) {
                spawnEnemyObjectNames_.push_back(value.get<std::string>());
            }
        }
    }
    if (spawnEnemyObjectNames_.empty()) {
        spawnEnemyObjectNames_.push_back(spawnEnemyObjectName_);
    }
    spawnEnemyMaxHpOverrides_.clear();
    if (root.contains("spawnEnemyMaxHp") && root["spawnEnemyMaxHp"].is_object()) {
        for (const auto& [objectName, value] : root["spawnEnemyMaxHp"].items()) {
            if (value.is_number()) {
                spawnEnemyMaxHpOverrides_[objectName] = (std::max)(1.0f, value.get<float>());
            }
        }
    }
    spawnEnemyDetectionRangeOverrides_.clear();
    if (root.contains("spawnEnemyDetectionRange") && root["spawnEnemyDetectionRange"].is_object()) {
        for (const auto& [objectName, value] : root["spawnEnemyDetectionRange"].items()) {
            if (value.is_number()) {
                spawnEnemyDetectionRangeOverrides_[objectName] = (std::max)(0.0f, value.get<float>());
            }
        }
    }
    std::vector<Step> loadedSteps;
    try {
        for (const auto& item : root["steps"]) {
            if (!item.is_object()) {
                continue;
            }

            Step step;
            step.id = item.value("id", "step_" + std::to_string(loadedSteps.size()));
            step.condition = ParseCondition(item.value("condition", "timer"));
            step.action = item.value("action", "");
            step.cinematicPath = item.value("cinematic", "");
            step.promptLabel = item.value("promptLabel", "");
            step.objectiveTarget = item.value("objectiveTarget", "");
            step.threshold = item.value("threshold", 0.0f);
            step.radius = (std::max)(0.1f, item.value("radius", 3.0f));
            step.objectiveOffsetY = item.value("objectiveOffsetY", 0.75f);
            step.minimumDisplayTime = (std::max)(0.0f, item.value("minimumDisplayTime", 0.12f));
            step.completionDelay = (std::max)(0.0f, item.value("completionDelay", 0.0f));
            step.showPrompt = item.value("showPrompt", true);
            step.requireMorph = item.value("requireMorph", false);
            step.previewGateUnlock = item.value("previewGateUnlock", false);
            step.unlockGateOnComplete = item.value("unlockGateOnComplete", false);
            step.spawnEnemy = item.value("spawnEnemy", false);
            step.spawnEnemiesDormant = item.value("spawnEnemiesDormant", false);
            step.allowCarriedThrow = item.value("allowCarriedThrow", true);
            step.allowCarriedAbsorb = item.value("allowCarriedAbsorb", true);
            step.spawnEnemyObject = item.value("spawnEnemyObject", "");
            step.despawnEnemyObject = item.value("despawnEnemyObject", "");
            step.wrongActionRetryStep = item.value("wrongActionRetryStep", "");
            step.hasTargetPosition = ReadVector3(item.value("targetPosition", json::array()), step.targetPosition);
            step.hasCheckpoint = ReadVector3(item.value("checkpoint", json::array()), step.checkpointPosition);

            for (const auto& icon : item.value("promptIcons", json::array())) {
                if (icon.is_string()) {
                    step.promptIcons.push_back(icon.get<std::string>());
                }
            }
            for (const auto& objectName : item.value("objectiveTargets", json::array())) {
                if (objectName.is_string()) {
                    step.objectiveTargets.push_back(objectName.get<std::string>());
                }
            }
            for (const auto& objectName : item.value("targetEnemyObjects", json::array())) {
                if (objectName.is_string()) {
                    step.targetEnemyObjects.push_back(objectName.get<std::string>());
                }
            }
            for (const auto& objectName : item.value("spawnEnemyObjects", json::array())) {
                if (objectName.is_string()) {
                    step.spawnEnemyObjects.push_back(objectName.get<std::string>());
                }
            }
            for (const auto& objectName : item.value("despawnEnemyObjects", json::array())) {
                if (objectName.is_string()) {
                    step.despawnEnemyObjects.push_back(objectName.get<std::string>());
                }
            }
            loadedSteps.push_back(std::move(step));
        }
    } catch (...) {
        return false;
    }

    if (loadedSteps.empty()) {
        return false;
    }
    steps_ = std::move(loadedSteps);
    return true;
}

TutorialDirector::CompletionCondition TutorialDirector::ParseCondition(const std::string& conditionName) {
    if (conditionName == "cinematic_finished") return CompletionCondition::kCinematicFinished;
    if (conditionName == "move_distance") return CompletionCondition::kMoveDistance;
    if (conditionName == "action_triggered") return CompletionCondition::kActionTriggered;
    if (conditionName == "reach_position") return CompletionCondition::kReachPosition;
    if (conditionName == "carry_enemy") return CompletionCondition::kCarryEnemy;
    if (conditionName == "enemy_thrown") return CompletionCondition::kEnemyThrown;
    if (conditionName == "enemy_defeated" || conditionName == "enemies_defeated") {
        return CompletionCondition::kEnemiesDefeated;
    }
    if (conditionName == "morph_active") return CompletionCondition::kMorphActive;
    if (conditionName == "morph_released") return CompletionCondition::kMorphReleased;
    if (conditionName == "controls_guide_opened") return CompletionCondition::kControlsGuideOpened;
    return CompletionCondition::kTimer;
}

bool TutorialDirector::ReadVector3(const json& value, Vector3& destination) {
    if (!value.is_array() || value.size() < 3) {
        return false;
    }
    destination = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
    return true;
}

void TutorialDirector::BeginFlowIfNeeded() {
    if (started_ || steps_.empty()) {
        return;
    }
    SetAllSpawnEnemiesActive(false);
    started_ = true;
    EnterStep(0);
}

void TutorialDirector::EnterStep(int stepIndex) {
    if (stepIndex < 0 || stepIndex >= static_cast<int>(steps_.size()) || !player_) {
        CompleteFlow();
        return;
    }

    currentStepIndex_ = stepIndex;
    stepTimer_ = 0.0f;
    conditionSatisfiedTimer_ = 0.0f;
    stepConditionSatisfied_ = false;
    stepStartPosition_ = player_->GetWorldPosition();
    morphWasActiveOnStepEntry_ = player_->IsEnemyMorphed();
    carriedEnemyOnStepEntry_ = player_->GetCarriedEnemy();

    Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
    if (step.condition == CompletionCondition::kControlsGuideOpened) {
        controlsGuideOpened_ = false;
    }
    player_->SetTutorialCarryActionPermissions(step.allowCarriedThrow, step.allowCarriedAbsorb);
    if (step.hasCheckpoint) {
        player_->SetRespawnPosition(step.checkpointPosition);
    }
    UpdateGateStateForStep(step);
    if (!step.despawnEnemyObject.empty()) {
        SetSpawnEnemyActive(step.despawnEnemyObject, false);
    }
    for (const std::string& objectName : step.despawnEnemyObjects) {
        SetSpawnEnemyActive(objectName, false);
    }
    if (!step.spawnEnemyObject.empty()) {
        SetSpawnEnemyActive(step.spawnEnemyObject, true);
    }
    else if (step.spawnEnemy) {
        SetSpawnEnemyActive(spawnEnemyObjectName_, true);
    }
    for (const std::string& objectName : step.spawnEnemyObjects) {
        SetSpawnEnemyActive(objectName, true);
    }
    if (step.spawnEnemiesDormant) {
        SetStepSpawnEnemiesDormant(step, true);
    }
    ApplyStepPrompt(&step);
    ApplyStepObjective(&step);
    if (!step.cinematicPath.empty()) {
        BeginStepCinematic(step);
    }

    DebugConsole::GetInstance()->AddLog("[Tutorial] Step: " + step.id);
}

void TutorialDirector::LeaveCurrentStep() {
    if (cinematicActive_) {
        EndStepCinematic(true);
    }
    SetPromptVisible(false);
    ApplyStepObjective(nullptr);
}

void TutorialDirector::AdvanceStep() {
    if (currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return;
    }

    const Step completedStep = steps_[static_cast<size_t>(currentStepIndex_)];
    if (completedStep.unlockGateOnComplete) {
        SetGateTransitionEnabled(true);
    }
    LeaveCurrentStep();

    const int nextStep = currentStepIndex_ + 1;
    if (nextStep >= static_cast<int>(steps_.size())) {
        CompleteFlow();
        return;
    }
    EnterStep(nextStep);
}

bool TutorialDirector::EvaluateCurrentStep() const {
    if (!player_ || currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return false;
    }

    const Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
    if (step.requireMorph && !player_->IsEnemyMorphed()) {
        return false;
    }
    switch (step.condition) {
    case CompletionCondition::kCinematicFinished:
        return !cinematicActive_;
    case CompletionCondition::kMoveDistance:
        return HorizontalDistance(player_->GetWorldPosition(), stepStartPosition_) >= step.threshold;
    case CompletionCondition::kActionTriggered:
        return IsActionTriggered(step.action);
    case CompletionCondition::kReachPosition:
        return step.hasTargetPosition &&
            HorizontalDistance(player_->GetWorldPosition(), step.targetPosition) <= step.radius;
    case CompletionCondition::kCarryEnemy:
        return player_->GetCarriedEnemy() != nullptr;
    case CompletionCondition::kEnemyThrown: {
        auto* enemy = dynamic_cast<BaseEnemy*>(carriedEnemyOnStepEntry_);
        return enemy &&
            player_->GetCarriedEnemy() == nullptr &&
            !player_->IsEnemyMorphed() &&
            enemy->IsThrownPhysics();
    }
    case CompletionCondition::kEnemiesDefeated:
        return AreTargetEnemiesDefeated(step);
    case CompletionCondition::kMorphActive:
        return player_->IsEnemyMorphed();
    case CompletionCondition::kMorphReleased:
        return morphWasActiveOnStepEntry_ &&
            !player_->IsEnemyMorphed() &&
            !player_->IsEnemyMorphReleasing();
    case CompletionCondition::kControlsGuideOpened:
        return controlsGuideOpened_;
    case CompletionCondition::kTimer:
    default:
        return stepTimer_ >= step.threshold;
    }
}

bool TutorialDirector::AreTargetEnemiesDefeated(const Step& step) const {
    if (step.targetEnemyObjects.empty()) {
        return false;
    }

    for (const std::string& objectName : step.targetEnemyObjects) {
        Object3d* object = FindObjectByName(objectName);
        if (!object) {
            continue;
        }
        const auto* enemy = dynamic_cast<const BaseEnemy*>(object);
        if (!enemy || !enemy->isDead) {
            return false;
        }
    }
    return true;
}

bool TutorialDirector::RecoverFromWrongCarriedAction(const Step& step) {
    if (!player_ || step.wrongActionRetryStep.empty()) {
        return false;
    }

    const bool absorbedDuringThrowPractice =
        !step.allowCarriedAbsorb && player_->IsEnemyMorphed();

    bool thrownDuringAbsorbPractice = false;
    if (!step.allowCarriedThrow) {
        auto* enemy = dynamic_cast<BaseEnemy*>(carriedEnemyOnStepEntry_);
        thrownDuringAbsorbPractice = enemy &&
            player_->GetCarriedEnemy() == nullptr &&
            !player_->IsEnemyMorphed() &&
            enemy->IsThrownPhysics();
    }

    if (!absorbedDuringThrowPractice && !thrownDuringAbsorbPractice) {
        return false;
    }

    player_->ResetTutorialCarryActionState();
    DebugConsole::GetInstance()->AddLog(
        "[Tutorial] 異なる操作を検出したため工程を再試行します: " + step.id);
    return JumpToStep(step.wrongActionRetryStep);
}

bool TutorialDirector::IsActionTriggered(const std::string& action) const {
    if (!inputManager_) {
        return false;
    }
    if (action == "MouseLeft") return inputManager_->IsMouseButtonTriggered(0);
    if (action == "MouseRight") return inputManager_->IsMouseButtonTriggered(1);
    if (action == "MouseMove") {
        const Vector2 delta = inputManager_->GetMouseMoveDelta();
        return std::abs(delta.x) + std::abs(delta.y) > 0.5f;
    }
    return !action.empty() && inputManager_->IsActionTriggered(action);
}

void TutorialDirector::CompleteFlow() {
    if (completed_) {
        return;
    }
    completed_ = true;
    currentStepIndex_ = static_cast<int>(steps_.size());
    if (player_) {
        player_->SetTutorialCarryActionPermissions(true, true);
    }
    SetPromptVisible(false);
    SetGateTransitionEnabled(true);
    if (completionCallback_) {
        completionCallback_();
    }
    DebugConsole::GetInstance()->AddLog("[Tutorial] Flow completed.");
}

void TutorialDirector::BeginStepCinematic(const Step& step) {
    cinematicSequence_.Clear();
    if (!cinematicSequence_.Load(step.cinematicPath)) {
        DebugConsole::GetInstance()->AddLog(
            "[Tutorial] Cinematicの読み込みに失敗しました: " + step.cinematicPath);
        cinematicActive_ = false;
        return;
    }

    cinematicPlayer_.SetSequence(&cinematicSequence_);
    cinematicPlayer_.Play(false);
    cinematicActive_ = cinematicPlayer_.IsPlaying();
    if (cinematicActive_ && player_) {
        player_->BeginCinematicLock();
    }
    SetPromptVisible(false);
}

void TutorialDirector::EndStepCinematic(bool skipped) {
    // 自然終了時は IsPlaying() が先に false になりますが、カメラトラックは
    // Stop() を呼ぶまで残るため、必ず停止して通常カメラへブレンド復帰させます。
    cinematicPlayer_.Stop(false);
    cinematicActive_ = false;
    if (player_ && player_->IsCinematicLocked()) {
        player_->EndCinematicLock(true);
    }
    if (currentStepIndex_ >= 0 && currentStepIndex_ < static_cast<int>(steps_.size())) {
        const Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
        if (step.spawnEnemiesDormant) {
            // 登場演出中だけ休眠させ、操作開始後の投擲速度を毎フレーム消さないようにします。
            SetStepSpawnEnemiesDormant(step, false);
        }
    }
    if (skipped) {
        DebugConsole::GetInstance()->AddLog("[Tutorial] Cinematic skipped.");
    }
}

void TutorialDirector::UpdateCinematic(float deltaTime) {
    if (!cinematicActive_) {
        return;
    }

    cinematicPlayer_.Update((std::max)(0.0f, deltaTime));
    const bool canSkip = stepTimer_ >= 0.18f;
    if (canSkip && inputManager_ && inputManager_->IsActionTriggered("Jump")) {
        EndStepCinematic(true);
        return;
    }
    if (!cinematicPlayer_.IsPlaying()) {
        EndStepCinematic(false);
    }
}

void TutorialDirector::UpdateGateStateForStep(const Step& step) {
    if (!exitGate_ || !step.previewGateUnlock) {
        return;
    }
    exitGate_->SetTransitionEnabled(false);
    exitGate_->SetGateState(false, false, false, true);
    exitGate_->SetGateActivation(0.72f);
}

void TutorialDirector::SetGateTransitionEnabled(bool enabled) {
    if (!exitGate_) {
        return;
    }
    exitGate_->SetTransitionEnabled(enabled);
    exitGate_->SetGateState(false, enabled, false, false);
    exitGate_->SetGateActivation(enabled ? 1.0f : 0.0f);
    if (enabled) {
        exitGate_->TriggerEntryReaction();
    }
}

void TutorialDirector::SetSpawnEnemyActive(const std::string& objectName, bool active) {
    const auto it = std::find_if(
        spawnEnemies_.begin(),
        spawnEnemies_.end(),
        [&objectName](const SpawnEnemySlot& slot) { return slot.objectName == objectName; });
    if (it == spawnEnemies_.end()) {
        return;
    }

    SpawnEnemySlot& slot = *it;
    slot.enemy = dynamic_cast<BaseEnemy*>(FindObjectByName(objectName));
    if (!slot.enemy) {
        slot.active = false;
        return;
    }
    if (!active && player_ && player_->GetCarriedEnemy() == slot.enemy) {
        player_->ReleaseCarriedEnemy(true);
    }

    slot.active = active;
    slot.enemy->isDead = false;
    if (active) {
        // 再試行時に前回の投擲物理状態を持ち越さないよう、一時状態を初期化します。
        slot.enemy->SetCarried(true);
    }
    slot.enemy->SetCarried(false);
    slot.enemy->SetTranslate(slot.initialPosition);
    slot.enemy->SetVelocity({ 0.0f, 0.0f, 0.0f });
    if (active && slot.maxHpOverride > 0.0f) {
        if (!slot.enemy->param_.has_value()) {
            slot.enemy->param_.emplace();
        }
        slot.enemy->param_->maxHp = slot.maxHpOverride;
        slot.enemy->param_->hp = slot.maxHpOverride;
    }
    if (active && slot.detectionRangeOverride >= 0.0f) {
        if (!slot.enemy->param_.has_value()) {
            slot.enemy->param_.emplace();
        }
        slot.enemy->param_->detectionRange = slot.detectionRangeOverride;
        slot.enemy->SetDetectionRange(slot.detectionRangeOverride);
    }
    slot.enemy->SetDormant(!active);
    slot.enemy->SetIsVisible(active);
    slot.enemy->SetCollisionAttribute(active ? slot.collisionAttribute : 0);
    slot.enemy->SetCollisionMask(active ? slot.collisionMask : 0);

    if (active) {
        DebugConsole::GetInstance()->AddLog("[Tutorial] Enemy spawned: " + slot.objectName);
    }
}

void TutorialDirector::SetSpawnEnemyDormant(const std::string& objectName, bool dormant) {
    const auto it = std::find_if(
        spawnEnemies_.begin(),
        spawnEnemies_.end(),
        [&objectName](const SpawnEnemySlot& slot) { return slot.objectName == objectName; });
    if (it == spawnEnemies_.end()) {
        return;
    }

    BaseEnemy* enemy = dynamic_cast<BaseEnemy*>(FindObjectByName(objectName));
    if (enemy) {
        enemy->SetDormant(dormant);
    }
}

void TutorialDirector::SetStepSpawnEnemiesDormant(const Step& step, bool dormant) {
    if (!step.spawnEnemyObject.empty()) {
        SetSpawnEnemyDormant(step.spawnEnemyObject, dormant);
    }
    else if (step.spawnEnemy) {
        SetSpawnEnemyDormant(spawnEnemyObjectName_, dormant);
    }
    for (const std::string& objectName : step.spawnEnemyObjects) {
        SetSpawnEnemyDormant(objectName, dormant);
    }
}

void TutorialDirector::SetAllSpawnEnemiesActive(bool active) {
    for (const SpawnEnemySlot& slot : spawnEnemies_) {
        SetSpawnEnemyActive(slot.objectName, active);
    }
}

void TutorialDirector::RestoreSpawnEnemyStateBeforeStep(int stepIndex) {
    SetAllSpawnEnemiesActive(false);
    const int endIndex = (std::clamp)(stepIndex, 0, static_cast<int>(steps_.size()));
    for (int index = 0; index < endIndex; ++index) {
        const Step& step = steps_[static_cast<size_t>(index)];
        if (!step.despawnEnemyObject.empty()) {
            SetSpawnEnemyActive(step.despawnEnemyObject, false);
        }
        for (const std::string& objectName : step.despawnEnemyObjects) {
            SetSpawnEnemyActive(objectName, false);
        }
        if (!step.spawnEnemyObject.empty()) {
            SetSpawnEnemyActive(step.spawnEnemyObject, true);
        }
        else if (step.spawnEnemy) {
            SetSpawnEnemyActive(spawnEnemyObjectName_, true);
        }
        for (const std::string& objectName : step.spawnEnemyObjects) {
            SetSpawnEnemyActive(objectName, true);
        }
        if (step.spawnEnemiesDormant) {
            SetStepSpawnEnemiesDormant(step, true);
        }
    }
}

Object3d* TutorialDirector::FindObjectByName(const std::string& name) const {
    if (!scene_ || name.empty()) {
        return nullptr;
    }
    for (const auto& object : scene_->GetObjects()) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

void TutorialDirector::BindPromptSprites() {
    if (!scene_) {
        return;
    }

    auto bind = [this](const std::string& name) {
        PromptVisual visual;
        visual.sprite = scene_->GetSpriteByName(name);
        if (visual.sprite) {
            visual.basePosition = visual.sprite->GetPosition();
            visual.baseSize = visual.sprite->GetSize();
            visual.baseColor = visual.sprite->GetColor();
            visual.sprite->SetVisible(false);
        }
        return visual;
    };

    promptPanel_ = bind("tutorial_prompt_panel");
    promptPanel_.requestedVisible = true;
    promptLabel_ = bind("tutorial_prompt_label");
    for (size_t index = 0; index < promptIcons_.size(); ++index) {
        promptIcons_[index] = bind("tutorial_prompt_icon_" + std::to_string(index));
    }
    objectiveMarkers_[0] = bind("tutorial_objective_marker");
    for (size_t index = 1; index < objectiveMarkers_.size(); ++index) {
        objectiveMarkers_[index] = bind("tutorial_objective_marker_" + std::to_string(index));
    }
}

void TutorialDirector::ApplyStepPrompt(const Step* step) {
    const bool show = step && step->showPrompt &&
        (!step->promptIcons.empty() || !step->promptLabel.empty());
    SetPromptVisible(show);
    promptLabel_.requestedVisible = false;
    for (PromptVisual& visual : promptIcons_) {
        visual.requestedVisible = false;
    }
    if (!show) {
        return;
    }

    const bool hasLabel = !step->promptLabel.empty() && promptLabel_.sprite;
    if (hasLabel) {
        promptLabel_.requestedVisible = true;
        promptLabel_.sprite->SetVisible(true);
        SetFullTextureRect(promptLabel_.sprite, step->promptLabel, { 470.0f, 76.0f });
        promptLabel_.basePosition.x = step->promptIcons.empty() ? 960.0f : 1070.0f;
        promptLabel_.baseSize = promptLabel_.sprite->GetSize();
    }

    const size_t iconCount = (std::min)(step->promptIcons.size(), promptIcons_.size());
    constexpr float kPromptIconSpacing = 132.0f;
    const float iconGroupCenterX = hasLabel ? 690.0f : 960.0f;
    const float firstIconX = iconGroupCenterX - kPromptIconSpacing * static_cast<float>(iconCount - 1) * 0.5f;
    for (size_t index = 0; index < promptIcons_.size(); ++index) {
        PromptVisual& visual = promptIcons_[index];
        if (!visual.sprite) {
            continue;
        }
        const bool hasIcon = index < step->promptIcons.size();
        visual.requestedVisible = hasIcon;
        visual.sprite->SetVisible(hasIcon);
        if (hasIcon) {
            SetFullTextureRect(visual.sprite, step->promptIcons[index], { 190.0f, 118.0f });
            visual.basePosition.x = firstIconX + kPromptIconSpacing * static_cast<float>(index);
            visual.baseSize = visual.sprite->GetSize();
        }
    }
}

void TutorialDirector::UpdatePrompt(float deltaTime) {
    const float targetAlpha = promptRequestedVisible_ ? 1.0f : 0.0f;
    const float response = 1.0f - std::exp(-kPromptFadeResponse * (std::max)(0.0f, deltaTime));
    promptAlpha_ += (targetAlpha - promptAlpha_) * response;
    if (std::abs(targetAlpha - promptAlpha_) < 0.002f) {
        promptAlpha_ = targetAlpha;
    }

    const bool visible = promptAlpha_ > 0.002f;
    auto updateVisual = [this, visible](PromptVisual& visual) {
        if (!visual.sprite) {
            return;
        }
        visual.sprite->SetVisible(visible && visual.requestedVisible);
        Vector4 color = visual.baseColor;
        color.w *= promptAlpha_;
        visual.sprite->SetColor(color);
        visual.sprite->SetPosition({
            visual.basePosition.x,
            visual.basePosition.y + (1.0f - promptAlpha_) * 18.0f
        });
        visual.sprite->SetSize(visual.baseSize);
    };

    updateVisual(promptPanel_);
    updateVisual(promptLabel_);
    for (size_t index = 0; index < promptIcons_.size(); ++index) {
        PromptVisual& icon = promptIcons_[index];
        updateVisual(icon);
    }
}

void TutorialDirector::SetPromptVisible(bool visible) {
    promptRequestedVisible_ = visible;
    if (visible && promptPanel_.sprite) {
        promptPanel_.sprite->SetVisible(true);
    }
    if (!visible && promptAlpha_ <= 0.002f) {
        if (promptPanel_.sprite) promptPanel_.sprite->SetVisible(false);
        if (promptLabel_.sprite) promptLabel_.sprite->SetVisible(false);
        for (PromptVisual& icon : promptIcons_) {
            if (icon.sprite) icon.sprite->SetVisible(false);
        }
    }
}

void TutorialDirector::ApplyStepObjective(const Step* step) {
    objectiveTargetNames_ = {};
    objectiveAlphas_ = {};
    objectiveOffsetY_ = 0.75f;
    objectiveAnimationTime_ = 0.0f;

    if (step) {
        objectiveOffsetY_ = step->objectiveOffsetY;
        if (!step->objectiveTargets.empty()) {
            const size_t targetCount = (std::min)(step->objectiveTargets.size(), objectiveTargetNames_.size());
            for (size_t index = 0; index < targetCount; ++index) {
                objectiveTargetNames_[index] = step->objectiveTargets[index];
            }
        }
        else if (!step->objectiveTarget.empty()) {
            objectiveTargetNames_[0] = step->objectiveTarget;
        }
    }

    const bool hasTarget = std::any_of(
        objectiveTargetNames_.begin(),
        objectiveTargetNames_.end(),
        [this](const std::string& targetName) {
            return !targetName.empty() && FindObjectByName(targetName) != nullptr;
        });
    SetObjectiveMarkerVisible(hasTarget);
}

void TutorialDirector::UpdateObjectiveMarker(float deltaTime) {
    objectiveAnimationTime_ += (std::max)(0.0f, deltaTime);
    const float response = 1.0f - std::exp(
        -kObjectiveFadeResponse * (std::max)(0.0f, deltaTime));
    Camera* camera = objectiveRequestedVisible_ && !cinematicActive_
        ? CameraManager::GetInstance()->GetActiveCamera()
        : nullptr;
    const Matrix4x4 viewProjection = camera ? camera->GetViewProjectionMatrix() : Matrix4x4{};

    for (size_t index = 0; index < objectiveMarkers_.size(); ++index) {
        PromptVisual& marker = objectiveMarkers_[index];
        if (!marker.sprite) {
            continue;
        }

        Object3d* target = objectiveTargetNames_[index].empty()
            ? nullptr
            : FindObjectByName(objectiveTargetNames_[index]);
        bool canShow = camera && target && target->GetIsVisible();
        if (const auto* enemy = dynamic_cast<const BaseEnemy*>(target); enemy && enemy->isDead) {
            canShow = false;
        }

        Vector2 screenPosition = marker.basePosition;
        if (canShow) {
            const AABB bounds = target->GetModelWorldAABB();
            const Vector3 markerWorld = {
                (bounds.min.x + bounds.max.x) * 0.5f,
                bounds.max.y + objectiveOffsetY_,
                (bounds.min.z + bounds.max.z) * 0.5f
            };
            const float w =
                markerWorld.x * viewProjection.m[0][3] +
                markerWorld.y * viewProjection.m[1][3] +
                markerWorld.z * viewProjection.m[2][3] +
                viewProjection.m[3][3];

            if (!std::isfinite(w) || w <= 0.001f) {
                canShow = false;
            }
            else {
                const float ndcX =
                    (markerWorld.x * viewProjection.m[0][0] +
                     markerWorld.y * viewProjection.m[1][0] +
                     markerWorld.z * viewProjection.m[2][0] +
                     viewProjection.m[3][0]) / w;
                const float ndcY =
                    (markerWorld.x * viewProjection.m[0][1] +
                     markerWorld.y * viewProjection.m[1][1] +
                     markerWorld.z * viewProjection.m[2][1] +
                     viewProjection.m[3][1]) / w;

                canShow = std::isfinite(ndcX) && std::isfinite(ndcY) &&
                    ndcX >= -1.10f && ndcX <= 1.10f &&
                    ndcY >= -1.10f && ndcY <= 1.10f;
                if (canShow) {
                    const float screenWidth = static_cast<float>(WinApp::kClientWidth);
                    const float screenHeight = static_cast<float>(WinApp::kClientHeight);
                    screenPosition.x = (ndcX + 1.0f) * 0.5f * screenWidth;
                    screenPosition.y = (1.0f - ndcY) * 0.5f * screenHeight;
                    screenPosition.x = (std::clamp)(
                        screenPosition.x,
                        kObjectiveScreenMargin,
                        screenWidth - kObjectiveScreenMargin);
                    screenPosition.y = (std::clamp)(
                        screenPosition.y,
                        kObjectiveScreenMargin,
                        screenHeight - kObjectiveScreenMargin);
                }
            }
        }

        const float targetAlpha = canShow ? 1.0f : 0.0f;
        float& alpha = objectiveAlphas_[index];
        alpha += (targetAlpha - alpha) * response;
        if (std::abs(targetAlpha - alpha) < 0.002f) {
            alpha = targetAlpha;
        }

        const bool visible = alpha > 0.002f && canShow;
        marker.sprite->SetVisible(visible);
        if (!visible) {
            continue;
        }

        const float phaseOffset = static_cast<float>(index) * 0.45f;
        const float bounce = std::sin(objectiveAnimationTime_ * 4.8f + phaseOffset);
        const float pulse = 1.0f + std::sin(objectiveAnimationTime_ * 3.6f + phaseOffset) * 0.045f;
        marker.sprite->SetPosition({
            screenPosition.x,
            screenPosition.y - 8.0f - bounce * 7.0f
        });
        marker.sprite->SetSize({
            marker.baseSize.x * pulse,
            marker.baseSize.y * pulse
        });
        Vector4 color = marker.baseColor;
        color.w *= alpha;
        marker.sprite->SetColor(color);
    }
}

void TutorialDirector::SetObjectiveMarkerVisible(bool visible) {
    objectiveRequestedVisible_ = visible;
    for (size_t index = 0; index < objectiveMarkers_.size(); ++index) {
        PromptVisual& marker = objectiveMarkers_[index];
        if (marker.sprite) {
            marker.sprite->SetVisible(visible && objectiveAlphas_[index] > 0.002f);
        }
    }
}

#ifdef USE_IMGUI
void TutorialDirector::DrawImGui() {
    ImGui::Text("Flow: %s", loaded_ ? "Ready" : "Load Failed");
    ImGui::Text("Step: %d / %d", currentStepIndex_ + 1, static_cast<int>(steps_.size()));
    ImGui::Text("ID: %s", GetCurrentStepId().c_str());
    ImGui::Text("Time: %.2f sec", stepTimer_);
    ImGui::Text("Cinematic: %s", cinematicActive_ ? "Playing" : "Stopped");
    ImGui::Text("Gate: %s", completed_ ? "Unlocked" : "Locked");

    if (ImGui::Button("Flowを最初から")) {
        Restart();
    }
    ImGui::SameLine();
    if (ImGui::Button("前のStep") && !steps_.empty()) {
        JumpToStep(steps_[static_cast<size_t>((std::max)(0, currentStepIndex_ - 1))].id);
    }
    ImGui::SameLine();
    if (ImGui::Button("次のStep") && !steps_.empty()) {
        const int next = (std::min)(static_cast<int>(steps_.size()) - 1, currentStepIndex_ + 1);
        JumpToStep(steps_[static_cast<size_t>(next)].id);
    }

    if (ImGui::Button("Flow JSONを再読み込み")) {
        const std::string path = flowPath_;
        const CompletionCallback callback = completionCallback_;
        BaseScene* scene = scene_;
        Player* player = player_;
        InputManager* inputManager = inputManager_;
        Initialize(scene, player, inputManager, path);
        completionCallback_ = callback;
    }

    if (ImGui::BeginCombo("Stepへ移動", GetCurrentStepId().c_str())) {
        for (const Step& step : steps_) {
            if (ImGui::Selectable(step.id.c_str(), step.id == GetCurrentStepId())) {
                JumpToStep(step.id);
            }
        }
        ImGui::EndCombo();
    }
}
#endif
