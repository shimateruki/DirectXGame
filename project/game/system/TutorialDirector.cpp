#define NOMINMAX
#include "TutorialDirector.h"

#include "BaseScene.h"
#include "BaseEnemy.h"
#include "CollisionConfig.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "GimmickStageGate.h"
#include "InputManager.h"
#include "game/ui/InputGlyphResolver.h"
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

// チュートリアルの移動距離は段差を無視したXZ平面で測ります。
float HorizontalDistance(const engine::Vector3& a, const engine::Vector3& b) {
    // 移動課題の進捗には段差の高さを含めず、地面上の距離を使う。
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void SetFullTextureRect(engine::Sprite* sprite, const std::string& texturePath, const engine::Vector2& maxSize) {
    // 案内画像を差し替える際に前のUVを残さず、枠内へ縦横比を保って収める。
    if (!sprite || texturePath.empty()) {
        return;
    }

    const uint32_t handle = engine::Sprite::LoadTexture(texturePath);
    if (handle == 0) {
        return;
    }
    sprite->SetTextureHandle(handle);
    sprite->SetTextureName(texturePath);
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
    sprite->SetTextureRect({0.0f, 0.0f}, {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});

    const float textureWidth = static_cast<float>((std::max)(metadata.width, size_t{1}));
    const float textureHeight = static_cast<float>((std::max)(metadata.height, size_t{1}));
    const float fitScale = (std::min)(maxSize.x / textureWidth, maxSize.y / textureHeight);
    sprite->SetSize({textureWidth * fitScale, textureHeight * fitScale});
}
} // namespace

// チュートリアルのJSON手順を実行します。表示、課題判定、敵の有効化をステップ遷移に合わせて同期します。

bool TutorialDirector::Initialize(BaseScene* scene, Player* player, InputManager* inputManager,
                                  const std::string& flowPath) {
    Finalize();
    scene_ = scene;
    player_ = player;
    inputManager_ = inputManager;
    flowPath_ = flowPath;

    if (!scene_ || !player_ || !inputManager_ || !LoadFlow(flowPath_)) {
        DebugConsole::GetInstance()->AddLog(std::string("[Tutorial] Flowの初期化に失敗しました: ") + flowPath_);
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
        slot.initialPosition = enemy->GetTranslate();
        // 非表示中に保存された敵は衝突設定が0になるため、出現時の通常設定を保持します。
        slot.collisionAttribute = enemy->GetCollisionAttribute() != 0 ? enemy->GetCollisionAttribute() : CollisionCategories::Get("enemy");
        slot.collisionMask = enemy->GetCollisionMask() != 0
                                 ? enemy->GetCollisionMask()
                                 : (CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_bullet") | CollisionCategories::Get("player_attack"));
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
    // 演出中のカメラ拘束と操作制限を先に戻し、次のシーンへ持ち越さない。
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
    lockedTargetName_.clear();
    stepConditionSatisfied_ = false;
    conditionSatisfiedTimer_ = 0.0f;
    morphWasActiveOnStepEntry_ = false;
    carriedEnemyOnStepEntry_ = nullptr;
    promptPanel_ = {};
    promptLabel_ = {};
    promptIcons_ = {};
    promptAlpha_ = 0.0f;
    promptRequestedVisible_ = false;
    promptGamepadMode_ = false;
    promptBindingSignature_.clear();
    objectiveMarkers_ = {};
    objectiveTargetNames_ = {};
    objectiveOffsetY_ = 0.75f;
    objectiveAlphas_ = {};
    objectiveAnimationTime_ = 0.0f;
    objectiveRequestedVisible_ = false;
    completionCallback_ = {};
    cinematicSequence_.Clear();
}

// Play中の手順、案内表示と演出を進め、条件達成後も最短表示時間と待機時間を満たしてから次へ進みます。
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

    // Play開始まで敵を出さず、最初の有効な更新でステップ0へ入ります。
    BeginFlowIfNeeded();
    if (currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return;
    }

    stepTimer_ += (std::max)(0.0f, deltaTime);
    UpdateCinematic(deltaTime);
    // 手順が続いている間も、最後に使用した入力方式へ案内を切り替えます。
    if (!stepConditionSatisfied_ && inputManager_ &&
        (promptGamepadMode_ != inputManager_->IsGamepadMode() ||
         promptBindingSignature_ != GetInputGlyphBindingSignature())) {
        ApplyStepPrompt(&steps_[static_cast<size_t>(currentStepIndex_)]);
    }
    UpdatePrompt(deltaTime);
    UpdateObjectiveMarker(deltaTime);

    const Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
    if (RecoverFromWrongCarriedAction(step)) {
        return;
    }
    // 達成は一度だけ記録します。表示時間と達成後の待ち時間は独立して満たします。
    if (!stepConditionSatisfied_ && EvaluateCurrentStep()) {
        stepConditionSatisfied_ = true;
        conditionSatisfiedTimer_ = 0.0f;
        SetPromptVisible(false);
    }
    if (stepConditionSatisfied_) {
        conditionSatisfiedTimer_ += (std::max)(0.0f, deltaTime);
    }
    if (stepConditionSatisfied_ && stepTimer_ >= step.minimumDisplayTime &&
        conditionSatisfiedTimer_ >= step.completionDelay) {
        AdvanceStep();
    }
}

void TutorialDirector::NotifyControlsGuideOpened() {
    // 操作案内を開く課題の達成は、Sceneから通知された事実で判定する。
    controlsGuideOpened_ = true;
}

void TutorialDirector::NotifyLockOnTarget(const engine::Object3d* target) {
    // 生ポインタの寿命に依存せず、目標名だけを課題判定へ渡す。
    lockedTargetName_ = target ? target->GetName() : std::string{};
}

const std::string& TutorialDirector::GetCurrentStepId() const {
    // 開始前・完了後にも参照可能な固定IDを返す。
    static const std::string kNone = "none";
    if (currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return kNone;
    }
    return steps_[static_cast<size_t>(currentStepIndex_)].id;
}

void TutorialDirector::CaptureReplayState(json& state) const {
    // 対象物はポインタでなく名前を保存し、復元時に現シーンから解決する。
    const auto objectName = [](const engine::Object3d* object) { return object ? object->GetName() : std::string{}; };

    state = {{"currentStepIndex", currentStepIndex_},
             {"stepTimer", stepTimer_},
             {"conditionSatisfiedTimer", conditionSatisfiedTimer_},
             {"stepStartPosition", {stepStartPosition_.x, stepStartPosition_.y, stepStartPosition_.z}},
             {"started", started_},
             {"completed", completed_},
             {"cinematicActive", cinematicActive_},
             {"cinematicTime", cinematicPlayer_.GetCurrentTime()},
             {"controlsGuideOpened", controlsGuideOpened_},
             {"stepConditionSatisfied", stepConditionSatisfied_},
             {"morphWasActiveOnStepEntry", morphWasActiveOnStepEntry_},
             {"carriedEnemyOnStepEntry", objectName(carriedEnemyOnStepEntry_)},
             {"playerCarriedEnemy", player_ ? objectName(player_->GetCarriedEnemy()) : std::string{}},
             {"promptAlpha", promptAlpha_},
             {"promptRequestedVisible", promptRequestedVisible_},
             {"objectiveTargetNames", objectiveTargetNames_},
             {"objectiveOffsetY", objectiveOffsetY_},
             {"objectiveAlphas", objectiveAlphas_},
             {"objectiveAnimationTime", objectiveAnimationTime_},
             {"objectiveRequestedVisible", objectiveRequestedVisible_}};
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
        } else if (currentStep) {
            player_->SetTutorialCarryActionPermissions(currentStep->allowCarriedThrow, currentStep->allowCarriedAbsorb);

            const std::string carriedEnemyName = state.value("playerCarriedEnemy", std::string{});
            engine::Object3d* savedCarriedEnemy = FindObjectByName(carriedEnemyName);
            if (savedCarriedEnemy) {
                player_->SetCarriedEnemy(savedCarriedEnemy);
            } else if (player_->GetCarriedEnemy()) {
                player_->ReleaseCarriedEnemy(true);
            }
        } else {
            player_->SetTutorialCarryActionPermissions(true, true);
        }
    }

    const bool shouldRestoreCinematic =
        currentStep && !currentStep->cinematicPath.empty() && state.value("cinematicActive", false);
    cinematicSequence_.Clear();
    if (shouldRestoreCinematic && cinematicSequence_.Load(currentStep->cinematicPath)) {
        cinematicPlayer_.SetSequence(&cinematicSequence_);
        cinematicPlayer_.Play(false);
        cinematicPlayer_.SetTime((std::max)(0.0f, state.value("cinematicTime", 0.0f)), false);
        cinematicActive_ = cinematicPlayer_.IsPlaying();
        if (cinematicActive_ && player_ && !player_->IsCinematicLocked()) {
            player_->BeginCinematicLock();
        }
    }
}

void TutorialDirector::Restart() {
    // 再挑戦では敵の出現、ゲート、運搬状態を手順開始前へ戻す。
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
    // 途中再開時は先行手順の敵状態を再適用してから目標ステップへ入る。
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
    // JSONの手順を一時配列で検証し、全体を読めたときだけ実行データへ移す。
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
            for (const auto& icon : item.value("promptIconsGamepad", json::array())) {
                if (icon.is_string()) {
                    step.promptIconsGamepad.push_back(icon.get<std::string>());
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
    // 未知の条件は時間待ちへフォールバックし、読込時に無効な列挙値を作らない。
    if (conditionName == "cinematic_finished")
        return CompletionCondition::kCinematicFinished;
    if (conditionName == "move_distance")
        return CompletionCondition::kMoveDistance;
    if (conditionName == "action_triggered")
        return CompletionCondition::kActionTriggered;
    if (conditionName == "target_locked")
        return CompletionCondition::kTargetLocked;
    if (conditionName == "reach_position")
        return CompletionCondition::kReachPosition;
    if (conditionName == "carry_enemy")
        return CompletionCondition::kCarryEnemy;
    if (conditionName == "enemy_thrown")
        return CompletionCondition::kEnemyThrown;
    if (conditionName == "enemy_defeated" || conditionName == "enemies_defeated") {
        return CompletionCondition::kEnemiesDefeated;
    }
    if (conditionName == "morph_active")
        return CompletionCondition::kMorphActive;
    if (conditionName == "morph_released")
        return CompletionCondition::kMorphReleased;
    if (conditionName == "controls_guide_opened")
        return CompletionCondition::kControlsGuideOpened;
    return CompletionCondition::kTimer;
}

bool TutorialDirector::ReadVector3(const json& value, engine::Vector3& destination) {
    // 3成分未満なら既存値を保持し、壊れた座標で復元しない。
    if (!value.is_array() || value.size() < 3) {
        return false;
    }
    destination = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    return true;
}

void TutorialDirector::BeginFlowIfNeeded() {
    // 最初の更新までは敵を隠し、Play開始時だけ手順0へ入る。
    if (started_ || steps_.empty()) {
        return;
    }
    SetAllSpawnEnemiesActive(false);
    started_ = true;
    EnterStep(0);
}

// 手順開始時の状態を記録し、敵・ゲート・案内・カメラ演出をこの手順の設定へ切り替えます。
// 無効な番号やPlayer不在の場合はFlowを完了扱いにします。
void TutorialDirector::EnterStep(int stepIndex) {
    if (stepIndex < 0 || stepIndex >= static_cast<int>(steps_.size()) || !player_) {
        CompleteFlow();
        return;
    }

    // 達成条件に使う「開始時」の姿勢と運搬対象は、敵の出現を切り替える前に保存します。
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
    // 前ステップの敵を片付けてから今回の敵を有効化し、同時に複数課題が成立しないようにします。
    if (!step.despawnEnemyObject.empty()) {
        SetSpawnEnemyActive(step.despawnEnemyObject, false);
    }
    for (const std::string& objectName : step.despawnEnemyObjects) {
        SetSpawnEnemyActive(objectName, false);
    }
    if (!step.spawnEnemyObject.empty()) {
        SetSpawnEnemyActive(step.spawnEnemyObject, true);
    } else if (step.spawnEnemy) {
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
    // ステップ固有のシネマティックと案内表示を次へ持ち越さない。
    if (cinematicActive_) {
        EndStepCinematic(true);
    }
    SetPromptVisible(false);
    ApplyStepObjective(nullptr);
}

void TutorialDirector::AdvanceStep() {
    // 達成時のゲート解放を先に反映し、最後の手順ならFlowを完了する。
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

// コピー形態の共通条件を確認した後、手順固有の行動・距離・対象条件を評価します。
bool TutorialDirector::EvaluateCurrentStep() const {
    if (!player_ || currentStepIndex_ < 0 || currentStepIndex_ >= static_cast<int>(steps_.size())) {
        return false;
    }

    const Step& step = steps_[static_cast<size_t>(currentStepIndex_)];
    // 手順ごとの条件より先に、コピー形態という共通の前提を確認します。
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
    case CompletionCondition::kTargetLocked:
        return !step.objectiveTarget.empty() && lockedTargetName_ == step.objectiveTarget;
    case CompletionCondition::kReachPosition:
        return step.hasTargetPosition &&
               HorizontalDistance(player_->GetWorldPosition(), step.targetPosition) <= step.radius;
    case CompletionCondition::kCarryEnemy:
        return player_->GetCarriedEnemy() != nullptr;
    case CompletionCondition::kEnemyThrown: {
        auto* enemy = dynamic_cast<BaseEnemy*>(carriedEnemyOnStepEntry_);
        return enemy && player_->GetCarriedEnemy() == nullptr && !player_->IsEnemyMorphed() && enemy->IsThrownPhysics();
    }
    case CompletionCondition::kEnemiesDefeated:
        return AreTargetEnemiesDefeated(step);
    case CompletionCondition::kMorphActive:
        return player_->IsEnemyMorphed();
    case CompletionCondition::kMorphReleased:
        return morphWasActiveOnStepEntry_ && !player_->IsEnemyMorphed() && !player_->IsEnemyMorphReleasing();
    case CompletionCondition::kControlsGuideOpened:
        return controlsGuideOpened_;
    case CompletionCondition::kTimer:
    default:
        return stepTimer_ >= step.threshold;
    }
}

bool TutorialDirector::AreTargetEnemiesDefeated(const Step& step) const {
    // 対象に残っている敵がすべて死亡したときだけ課題を達成する。
    if (step.targetEnemyObjects.empty()) {
        return false;
    }

    for (const std::string& objectName : step.targetEnemyObjects) {
        engine::Object3d* object = FindObjectByName(objectName);
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

// 吸収と投擲の練習を取り違えると対象が失われるため、保持状態を戻して指定ステップから再試行します。
bool TutorialDirector::RecoverFromWrongCarriedAction(const Step& step) {
    // 運搬中に誤った操作をした場合は、定義された再試行ステップへ戻して状態を復元する。
    if (!player_ || step.wrongActionRetryStep.empty()) {
        return false;
    }

    const bool absorbedDuringThrowPractice = !step.allowCarriedAbsorb && player_->IsEnemyMorphed();

    bool thrownDuringAbsorbPractice = false;
    if (!step.allowCarriedThrow) {
        auto* enemy = dynamic_cast<BaseEnemy*>(carriedEnemyOnStepEntry_);
        thrownDuringAbsorbPractice =
            enemy && player_->GetCarriedEnemy() == nullptr && !player_->IsEnemyMorphed() && enemy->IsThrownPhysics();
    }

    if (!absorbedDuringThrowPractice && !thrownDuringAbsorbPractice) {
        return false;
    }

    player_->ResetTutorialCarryActionState();
    DebugConsole::GetInstance()->AddLog("[Tutorial] 異なる操作を検出したため工程を再試行します: " + step.id);
    return JumpToStep(step.wrongActionRetryStep);
}

bool TutorialDirector::IsActionTriggered(const std::string& action) const {
    // マウス操作の課題名を共通Actionへ変換し、パッド操作でも達成可能にする。
    if (!inputManager_) {
        return false;
    }
    if (action == "MouseLeft")
        return inputManager_->IsActionTriggered("PrimaryAttack");
    if (action == "MouseRight")
        return inputManager_->IsActionTriggered("SecondaryAction");
    if (action == "MouseMove") {
        const engine::Vector2 delta = inputManager_->GetMouseMoveDelta();
        const engine::Vector2 stick = inputManager_->GetRightStick();
        return std::abs(delta.x) + std::abs(delta.y) > 0.5f || std::abs(stick.x) + std::abs(stick.y) > 0.25f;
    }
    return !action.empty() && inputManager_->IsActionTriggered(action);
}

void TutorialDirector::CompleteFlow() {
    // 完了通知と出口解放を一度だけ行い、操作制限を解除する。
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
    // 手順ごとの演出を読み、再生中はプレイヤーの操作を拘束する。
    cinematicSequence_.Clear();
    if (!cinematicSequence_.Load(step.cinematicPath)) {
        DebugConsole::GetInstance()->AddLog("[Tutorial] Cinematicの読み込みに失敗しました: " + step.cinematicPath);
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
    // 開始直後の決定入力で飛ばさないよう、短い猶予の後だけスキップを受け付ける。
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
    // 予告表示は可能にしても、課題完了前のステージ遷移は許可しない。
    if (!exitGate_ || !step.previewGateUnlock) {
        return;
    }
    exitGate_->SetTransitionEnabled(false);
    exitGate_->SetGateState(false, false, false, true);
    exitGate_->SetGateActivation(0.72f);
}

void TutorialDirector::SetGateTransitionEnabled(bool enabled) {
    // 遷移可否と門の見た目を同時に更新し、解放時は反応演出を起動する。
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

// 課題用の敵を初期位置と物理状態へ戻してから表示を切り替え、運搬中なら先に手放します。
void TutorialDirector::SetSpawnEnemyActive(const std::string& objectName, bool active) {
    const auto it = std::find_if(spawnEnemies_.begin(), spawnEnemies_.end(),
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
    slot.enemy->SetVelocity({0.0f, 0.0f, 0.0f});
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
    // 出現位置と当たり判定を同じフレームで確定します。
    slot.enemy->UpdateLocalMatrix();
    slot.enemy->UpdateWorldMatrix(false);

    if (active) {
        DebugConsole::GetInstance()->AddLog("[Tutorial] Enemy spawned: " + slot.objectName);
    }
}

void TutorialDirector::SetSpawnEnemyDormant(const std::string& objectName, bool dormant) {
    // 登場演出中のみ敵AIを止め、位置や表示状態は変えない。
    const auto it = std::find_if(spawnEnemies_.begin(), spawnEnemies_.end(),
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
    // 手順に紐づく単体・複数の敵へ同じ休眠状態を適用する。
    if (!step.spawnEnemyObject.empty()) {
        SetSpawnEnemyDormant(step.spawnEnemyObject, dormant);
    } else if (step.spawnEnemy) {
        SetSpawnEnemyDormant(spawnEnemyObjectName_, dormant);
    }
    for (const std::string& objectName : step.spawnEnemyObjects) {
        SetSpawnEnemyDormant(objectName, dormant);
    }
}

void TutorialDirector::SetAllSpawnEnemiesActive(bool active) {
    // 最初のPlay前やリスタート時に、管理対象の敵を一括で隠す。
    for (const SpawnEnemySlot& slot : spawnEnemies_) {
        SetSpawnEnemyActive(slot.objectName, active);
    }
}

// ステップを飛ばして再開するとき、先行ステップの出現・退場を順に再適用して敵の状態を再構成します。
void TutorialDirector::RestoreSpawnEnemyStateBeforeStep(int stepIndex) {
    // 指定ステップまでの出現・消去履歴を再適用し、途中再開でも敵の状態を一致させる。
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
        } else if (step.spawnEnemy) {
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

engine::Object3d* TutorialDirector::FindObjectByName(const std::string& name) const {
    // 名前から現在のシーン内で解決し、前回シーンのポインタを使わない。
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
    // Sceneが所有するSpriteの初期位置・色・大きさを退避して、手順ごとに再利用する。
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
    // 固定操作表から入力機器に合う画像を選び、ゲーム内操作と案内を一致させる。
    promptGamepadMode_ = inputManager_ && inputManager_->IsGamepadMode();
    promptBindingSignature_ = GetInputGlyphBindingSignature();
    std::vector<InputGlyphSpec> glyphs;
    if (step && !step->promptIcons.empty()) {
        if (step->promptIcons[0] == "ui/input/combo/combo_wasd.png") {
            if (promptGamepadMode_) glyphs.push_back({"ui/input/controller/pad_l_stick.png"});
            else glyphs.push_back({"ui/input/combo/combo_wasd.png"});
        } else {
            for (size_t i = 0; i < step->promptIcons.size(); ++i) {
                InputGlyphSpec glyph = ResolveInputGlyph(step->promptIcons[i], promptGamepadMode_);
                if (promptGamepadMode_ && glyph.path == step->promptIcons[i] &&
                    i < step->promptIconsGamepad.size()) {
                    glyph = {step->promptIconsGamepad[i]};
                }
                glyphs.push_back(std::move(glyph));
            }
        }
    } else if (step && promptGamepadMode_) {
        for (const std::string& path : step->promptIconsGamepad) glyphs.push_back({path});
    }
    const bool show = step && step->showPrompt && (!glyphs.empty() || !step->promptLabel.empty());
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
        const bool fourKeys = glyphs.size() >= 4;
        SetFullTextureRect(promptLabel_.sprite, step->promptLabel, {fourKeys ? 360.0f : 470.0f, 76.0f});
        promptLabel_.basePosition.x = glyphs.empty() ? 960.0f : (fourKeys ? 1140.0f : 1070.0f);
        promptLabel_.baseSize = promptLabel_.sprite->GetSize();
    }

    const size_t iconCount = (std::min)(glyphs.size(), promptIcons_.size());
    const float kPromptIconSpacing = iconCount >= 4 ? 90.0f : 132.0f;
    const float iconGroupCenterX = iconCount >= 4 ? 745.0f : (hasLabel ? 690.0f : 960.0f);
    const float firstIconX = iconGroupCenterX - kPromptIconSpacing * static_cast<float>(iconCount - 1) * 0.5f;
    for (size_t index = 0; index < promptIcons_.size(); ++index) {
        PromptVisual& visual = promptIcons_[index];
        if (!visual.sprite) {
            continue;
        }
        const bool hasIcon = index < iconCount;
        visual.requestedVisible = hasIcon;
        visual.sprite->SetVisible(hasIcon);
        if (hasIcon) {
            const InputGlyphSpec& glyph = glyphs[index];
            SetFullTextureRect(visual.sprite, glyph.path, {iconCount >= 4 ? 90.0f : 190.0f, 118.0f});
            if (glyph.useCrop) {
                visual.sprite->SetTextureRect({glyph.cropX, glyph.cropY}, {glyph.width, glyph.height});
                const float fit = (std::min)(visual.sprite->GetSize().x / glyph.width,
                                            visual.sprite->GetSize().y / glyph.height);
                visual.sprite->SetSize({glyph.width * fit, glyph.height * fit});
            }
            visual.basePosition.x = firstIconX + kPromptIconSpacing * static_cast<float>(index);
            visual.baseSize = visual.sprite->GetSize();
        }
    }
}

void TutorialDirector::UpdatePrompt(float deltaTime) {
    // 指数応答で表示と非表示を補間し、フレーム時間が変わっても速度を揃える。
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
        engine::Vector4 color = visual.baseColor;
        color.w *= promptAlpha_;
        visual.sprite->SetColor(color);
        visual.sprite->SetPosition({visual.basePosition.x, visual.basePosition.y + (1.0f - promptAlpha_) * 18.0f});
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
    // フェードアウト中は即座に非表示にせず、透明になってからSpriteを隠す。
    promptRequestedVisible_ = visible;
    if (visible && promptPanel_.sprite) {
        promptPanel_.sprite->SetVisible(true);
    }
    if (!visible && promptAlpha_ <= 0.002f) {
        if (promptPanel_.sprite)
            promptPanel_.sprite->SetVisible(false);
        if (promptLabel_.sprite)
            promptLabel_.sprite->SetVisible(false);
        for (PromptVisual& icon : promptIcons_) {
            if (icon.sprite)
                icon.sprite->SetVisible(false);
        }
    }
}

void TutorialDirector::ApplyStepObjective(const Step* step) {
    // 課題の目標名を入れ替え、シーンに実在する対象だけマーカーを表示する。
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
        } else if (!step->objectiveTarget.empty()) {
            objectiveTargetNames_[0] = step->objectiveTarget;
        }
    }

    const bool hasTarget =
        std::any_of(objectiveTargetNames_.begin(), objectiveTargetNames_.end(), [this](const std::string& targetName) {
            return !targetName.empty() && FindObjectByName(targetName) != nullptr;
        });
    SetObjectiveMarkerVisible(hasTarget);
}

void TutorialDirector::UpdateObjectiveMarker(float deltaTime) {
    // 目標の頭上を画面座標へ投影し、背面や画面外ではマーカーを消す。
    objectiveAnimationTime_ += (std::max)(0.0f, deltaTime);
    const float response = 1.0f - std::exp(-kObjectiveFadeResponse * (std::max)(0.0f, deltaTime));
    Camera* camera =
        objectiveRequestedVisible_ && !cinematicActive_ ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;
    const engine::Matrix4x4 viewProjection = camera ? camera->GetViewProjectionMatrix() : engine::Matrix4x4{};

    for (size_t index = 0; index < objectiveMarkers_.size(); ++index) {
        PromptVisual& marker = objectiveMarkers_[index];
        if (!marker.sprite) {
            continue;
        }

        engine::Object3d* target =
            objectiveTargetNames_[index].empty() ? nullptr : FindObjectByName(objectiveTargetNames_[index]);
        bool canShow = camera && target && target->GetIsVisible();
        if (const auto* enemy = dynamic_cast<const BaseEnemy*>(target); enemy && enemy->isDead) {
            canShow = false;
        }

        engine::Vector2 screenPosition = marker.basePosition;
        if (canShow) {
            const AABB bounds = target->GetModelWorldAABB();
            const engine::Vector3 markerWorld = {(bounds.min.x + bounds.max.x) * 0.5f, bounds.max.y + objectiveOffsetY_,
                                         (bounds.min.z + bounds.max.z) * 0.5f};
            const float w = markerWorld.x * viewProjection.m[0][3] + markerWorld.y * viewProjection.m[1][3] +
                            markerWorld.z * viewProjection.m[2][3] + viewProjection.m[3][3];

            if (!std::isfinite(w) || w <= 0.001f) {
                canShow = false;
            } else {
                const float ndcX = (markerWorld.x * viewProjection.m[0][0] + markerWorld.y * viewProjection.m[1][0] +
                                    markerWorld.z * viewProjection.m[2][0] + viewProjection.m[3][0]) /
                                   w;
                const float ndcY = (markerWorld.x * viewProjection.m[0][1] + markerWorld.y * viewProjection.m[1][1] +
                                    markerWorld.z * viewProjection.m[2][1] + viewProjection.m[3][1]) /
                                   w;

                canShow = std::isfinite(ndcX) && std::isfinite(ndcY) && ndcX >= -1.10f && ndcX <= 1.10f &&
                          ndcY >= -1.10f && ndcY <= 1.10f;
                if (canShow) {
                    const float screenWidth = static_cast<float>(WinApp::kClientWidth);
                    const float screenHeight = static_cast<float>(WinApp::kClientHeight);
                    screenPosition.x = (ndcX + 1.0f) * 0.5f * screenWidth;
                    screenPosition.y = (1.0f - ndcY) * 0.5f * screenHeight;
                    screenPosition.x =
                        (std::clamp)(screenPosition.x, kObjectiveScreenMargin, screenWidth - kObjectiveScreenMargin);
                    screenPosition.y =
                        (std::clamp)(screenPosition.y, kObjectiveScreenMargin, screenHeight - kObjectiveScreenMargin);
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
        marker.sprite->SetPosition({screenPosition.x, screenPosition.y - 8.0f - bounce * 7.0f});
        marker.sprite->SetSize({marker.baseSize.x * pulse, marker.baseSize.y * pulse});
        engine::Vector4 color = marker.baseColor;
        color.w *= alpha;
        marker.sprite->SetColor(color);
    }
}

void TutorialDirector::SetObjectiveMarkerVisible(bool visible) {
    // 表示要求とフェード中の透明度を両方満たすマーカーだけ描く。
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
    // 実行中の手順と演出を確認し、開発時は任意の手順へ移動できる。
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
