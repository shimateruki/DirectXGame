#define NOMINMAX
#include "TutorialDirector.h"

#include "BaseScene.h"
#include "BaseEnemy.h"
#include "DebugConsole.h"
#include "GimmickStageGate.h"
#include "InputManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "TextureManager.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace {
constexpr float kPromptFadeResponse = 9.0f;

float HorizontalDistance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void SetFullTextureRect(Sprite* sprite, const std::string& texturePath) {
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
    const float fitScale = (std::min)(144.0f / textureWidth, 104.0f / textureHeight);
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
        spawnEnemies_.push_back(std::move(slot));
    }

    cinematicPlayer_.Initialize(SceneManager::GetInstance());
    cinematicPlayer_.SetSequence(&cinematicSequence_);
    loaded_ = true;
    return true;
}

void TutorialDirector::Finalize() {
    if (cinematicActive_) {
        EndStepCinematic(true);
    }
    SetPromptVisible(false);
    steps_.clear();
    currentStepIndex_ = -1;
    scene_ = nullptr;
    player_ = nullptr;
    inputManager_ = nullptr;
    exitGate_ = nullptr;
    flowPath_.clear();
    spawnEnemyObjectName_ = "Tutorial_PinkSlime";
    spawnEnemyObjectNames_.clear();
    spawnEnemies_.clear();
    started_ = false;
    loaded_ = false;
    completed_ = false;
    cinematicActive_ = false;
    controlsGuideOpened_ = false;
    morphWasActiveOnStepEntry_ = false;
    carriedEnemyOnStepEntry_ = nullptr;
    promptPanel_ = {};
    promptIcons_ = {};
    promptTime_ = 0.0f;
    promptAlpha_ = 0.0f;
    promptRequestedVisible_ = false;
    completionCallback_ = {};
    cinematicSequence_.Clear();
}

void TutorialDirector::Update(float deltaTime) {
    if (!loaded_ || completed_ || !player_) {
        UpdatePrompt(deltaTime);
        return;
    }

    // 編集停止中は最初のシネマティックを開始せず、自由カメラを維持する。
    if (!started_ && deltaTime <= 0.0f) {
        UpdatePrompt(0.0f);
        return;
    }

    BeginFlowIfNeeded();
    if (currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return;
    }

    stepTimer_ += (std::max)(0.0f, deltaTime);
    UpdateCinematic(deltaTime);
    UpdatePrompt(deltaTime);

    const Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
    if (stepTimer_ >= step.minimumDisplayTime && EvaluateCurrentStep()) {
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

void TutorialDirector::Restart() {
    if (!loaded_) {
        return;
    }
    LeaveCurrentStep();
    completed_ = false;
    started_ = false;
    currentStepIndex_ = -1;
    controlsGuideOpened_ = false;
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
            step.threshold = item.value("threshold", 0.0f);
            step.radius = (std::max)(0.1f, item.value("radius", 3.0f));
            step.minimumDisplayTime = (std::max)(0.0f, item.value("minimumDisplayTime", 0.12f));
            step.showPrompt = item.value("showPrompt", true);
            step.requireMorph = item.value("requireMorph", false);
            step.previewGateUnlock = item.value("previewGateUnlock", false);
            step.unlockGateOnComplete = item.value("unlockGateOnComplete", false);
            step.spawnEnemy = item.value("spawnEnemy", false);
            step.spawnEnemyObject = item.value("spawnEnemyObject", "");
            step.despawnEnemyObject = item.value("despawnEnemyObject", "");
            step.hasTargetPosition = ReadVector3(item.value("targetPosition", json::array()), step.targetPosition);
            step.hasCheckpoint = ReadVector3(item.value("checkpoint", json::array()), step.checkpointPosition);

            for (const auto& icon : item.value("promptIcons", json::array())) {
                if (icon.is_string()) {
                    step.promptIcons.push_back(icon.get<std::string>());
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
    stepStartPosition_ = player_->GetWorldPosition();
    morphWasActiveOnStepEntry_ = player_->IsEnemyMorphed();
    carriedEnemyOnStepEntry_ = player_->GetCarriedEnemy();

    Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
    if (step.hasCheckpoint) {
        player_->SetRespawnPosition(step.checkpointPosition);
    }
    UpdateGateStateForStep(step);
    if (!step.despawnEnemyObject.empty()) {
        SetSpawnEnemyActive(step.despawnEnemyObject, false);
    }
    if (!step.spawnEnemyObject.empty()) {
        SetSpawnEnemyActive(step.spawnEnemyObject, true);
    }
    else if (step.spawnEnemy) {
        SetSpawnEnemyActive(spawnEnemyObjectName_, true);
    }
    ApplyStepPrompt(&step);
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
    switch (step.condition) {
    case CompletionCondition::kCinematicFinished:
        return !cinematicActive_;
    case CompletionCondition::kMoveDistance:
        return HorizontalDistance(player_->GetWorldPosition(), stepStartPosition_) >= step.threshold;
    case CompletionCondition::kActionTriggered:
        if (step.requireMorph && !player_->IsEnemyMorphed()) {
            return false;
        }
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
    if (cinematicPlayer_.IsPlaying()) {
        cinematicPlayer_.Stop(false);
    }
    cinematicActive_ = false;
    if (player_ && player_->IsCinematicLocked()) {
        player_->EndCinematicLock(true);
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
    if (it == spawnEnemies_.end() || !it->enemy) {
        return;
    }

    SpawnEnemySlot& slot = *it;
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
    slot.enemy->SetDormant(!active);
    slot.enemy->SetIsVisible(active);
    slot.enemy->SetCollisionAttribute(active ? slot.collisionAttribute : 0);
    slot.enemy->SetCollisionMask(active ? slot.collisionMask : 0);

    if (active) {
        DebugConsole::GetInstance()->AddLog("[Tutorial] Enemy spawned: " + slot.objectName);
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
        if (!step.spawnEnemyObject.empty()) {
            SetSpawnEnemyActive(step.spawnEnemyObject, true);
        }
        else if (step.spawnEnemy) {
            SetSpawnEnemyActive(spawnEnemyObjectName_, true);
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
    for (size_t index = 0; index < promptIcons_.size(); ++index) {
        promptIcons_[index] = bind("tutorial_prompt_icon_" + std::to_string(index));
    }
}

void TutorialDirector::ApplyStepPrompt(const Step* step) {
    promptTime_ = 0.0f;
    promptAlpha_ = 0.0f;
    const bool show = step && step->showPrompt && !step->promptIcons.empty();
    SetPromptVisible(show);
    for (PromptVisual& visual : promptIcons_) {
        visual.requestedVisible = false;
    }
    if (!show) {
        return;
    }

    const size_t iconCount = (std::min)(step->promptIcons.size(), promptIcons_.size());
    constexpr float kPromptCenterX = 960.0f;
    constexpr float kPromptIconSpacing = 170.0f;
    const float firstIconX = kPromptCenterX - kPromptIconSpacing * static_cast<float>(iconCount - 1) * 0.5f;
    for (size_t index = 0; index < promptIcons_.size(); ++index) {
        PromptVisual& visual = promptIcons_[index];
        if (!visual.sprite) {
            continue;
        }
        const bool hasIcon = index < step->promptIcons.size();
        visual.requestedVisible = hasIcon;
        visual.sprite->SetVisible(hasIcon);
        if (hasIcon) {
            SetFullTextureRect(visual.sprite, step->promptIcons[index]);
            visual.basePosition.x = firstIconX + kPromptIconSpacing * static_cast<float>(index);
            visual.baseSize = visual.sprite->GetSize();
        }
    }
}

void TutorialDirector::UpdatePrompt(float deltaTime) {
    promptTime_ += (std::max)(0.0f, deltaTime);
    const float targetAlpha = promptRequestedVisible_ ? 1.0f : 0.0f;
    const float response = 1.0f - std::exp(-kPromptFadeResponse * (std::max)(0.0f, deltaTime));
    promptAlpha_ += (targetAlpha - promptAlpha_) * response;
    if (std::abs(targetAlpha - promptAlpha_) < 0.002f) {
        promptAlpha_ = targetAlpha;
    }

    const bool visible = promptAlpha_ > 0.002f;
    auto updateVisual = [this, visible](PromptVisual& visual, float pulse) {
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
        visual.sprite->SetSize({ visual.baseSize.x * pulse, visual.baseSize.y * pulse });
    };

    updateVisual(promptPanel_, 1.0f);
    for (size_t index = 0; index < promptIcons_.size(); ++index) {
        const float pulse = index == 0 ? 1.0f + std::sin(promptTime_ * 5.2f) * 0.035f : 1.0f;
        PromptVisual& icon = promptIcons_[index];
        updateVisual(icon, pulse);
    }
}

void TutorialDirector::SetPromptVisible(bool visible) {
    promptRequestedVisible_ = visible;
    if (visible && promptPanel_.sprite) {
        promptPanel_.sprite->SetVisible(true);
    }
    if (!visible && promptAlpha_ <= 0.002f) {
        if (promptPanel_.sprite) promptPanel_.sprite->SetVisible(false);
        for (PromptVisual& icon : promptIcons_) {
            if (icon.sprite) icon.sprite->SetVisible(false);
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
