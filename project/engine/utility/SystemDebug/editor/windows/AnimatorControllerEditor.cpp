#define NOMINMAX
#include "AnimatorControllerEditor.h"

#include "BaseScene.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "Player.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
constexpr const char* kAnimatorDirectory = "Resources/json/animator/";
constexpr const char* kBodyAnimationDirectory = "Resources/json/animation_clip/";

const char* kEasingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" };
const char* kParameterTypeNames[] = { "Float", "Int", "Bool", "Trigger" };
const char* kConditionModeNames[] = { "Greater", "Less", "Equals", "Not Equal", "If", "If Not", "Trigger" };
}

void AnimatorControllerEditor::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    NewController();
    RefreshFiles();
}

void AnimatorControllerEditor::SetPreviewTarget(Object3d* target) {
    previewTargetName_ = target ? target->GetName() : std::string{};
}

void AnimatorControllerEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_RANDOM " Animator Controller");
    ImGui::TextDisabled("State、Transition、Parameterと補間をAssetとして編集します。");
    DrawAssetControls();
    DrawPreviewTargetControls();

    if (ImGui::BeginTabBar("AnimatorControllerTabs")) {
        if (ImGui::BeginTabItem("States")) {
            DrawStateEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Transitions")) {
            DrawTransitionEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Parameters")) {
            DrawParameterEditor();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#endif
}

void AnimatorControllerEditor::RefreshFiles() {
    files_.clear();
    bodyAnimationFiles_.clear();
    if (fs::exists(kAnimatorDirectory)) {
        for (const auto& entry : fs::directory_iterator(kAnimatorDirectory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files_.push_back(entry.path().stem().string());
            }
        }
    }
    if (fs::exists(kBodyAnimationDirectory)) {
        for (const auto& entry : fs::directory_iterator(kBodyAnimationDirectory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                bodyAnimationFiles_.push_back(entry.path().stem().string());
            }
        }
    }
    std::sort(files_.begin(), files_.end());
    std::sort(bodyAnimationFiles_.begin(), bodyAnimationFiles_.end());
}

void AnimatorControllerEditor::NewController() {
    controller_.Clear();
    AnimatorStateDefinition idle;
    idle.name = "Idle";
    controller_.states.push_back(idle);
    controller_.entryState = idle.name;
    selectedStateIndex_ = 0;
    selectedParameterIndex_ = -1;
    selectedTransitionIndex_ = -1;
}

void AnimatorControllerEditor::LoadController(const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }
    const std::string path = std::string(kAnimatorDirectory) + fileName + ".json";
    if (!controller_.Load(path)) {
        DebugConsole::GetInstance()->AddLog("Animator Controller load failed: " + path);
        return;
    }
    strncpy_s(fileNameBuffer_, sizeof(fileNameBuffer_), fileName.c_str(), _TRUNCATE);
    selectedStateIndex_ = controller_.states.empty() ? -1 : 0;
    selectedParameterIndex_ = -1;
    selectedTransitionIndex_ = -1;
}

void AnimatorControllerEditor::SaveController() {
    if (fileNameBuffer_[0] == '\0') {
        return;
    }
    controller_.name = fileNameBuffer_;
    if (controller_.entryState.empty() && !controller_.states.empty()) {
        controller_.entryState = controller_.states.front().name;
    }
    const std::string path = GetAssetPath();
    if (controller_.Save(path)) {
        DebugConsole::GetInstance()->AddLog("Animator Controller saved: " + path);
        RefreshFiles();
        if (Object3d* target = ResolvePreviewTarget()) {
            if (target->GetAnimatorControllerPath() == fileNameBuffer_ || target->GetAnimatorControllerPath() == path) {
                target->SetAnimatorController(fileNameBuffer_);
            }
            if (Player* player = dynamic_cast<Player*>(target); player && std::string(fileNameBuffer_) == "player_slime") {
                player->ReloadSlimeAnimatorController();
            }
        }
    }
}

std::string AnimatorControllerEditor::GetAssetPath() const {
    return std::string(kAnimatorDirectory) + fileNameBuffer_ + ".json";
}

Object3d* AnimatorControllerEditor::ResolvePreviewTarget() const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || previewTargetName_.empty()) {
        return nullptr;
    }
    for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == previewTargetName_) {
            return object.get();
        }
    }
    return nullptr;
}

void AnimatorControllerEditor::DrawAssetControls() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_SAVE " Controller Asset", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const char* current = fileNameBuffer_[0] ? fileNameBuffer_ : "(新規)";
    if (ImGui::BeginCombo("既存Controller", current)) {
        for (const auto& fileName : files_) {
            if (ImGui::Selectable(fileName.c_str(), fileName == fileNameBuffer_)) {
                LoadController(fileName);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("ファイル名", fileNameBuffer_, sizeof(fileNameBuffer_));
    if (ImGui::Button(ICON_FA_FILE " 新規")) {
        NewController();
        strncpy_s(fileNameBuffer_, sizeof(fileNameBuffer_), "new_animator", _TRUNCATE);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SAVE " 保存")) {
        SaveController();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " 再読込")) {
        LoadController(fileNameBuffer_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clip一覧更新")) {
        RefreshFiles();
    }
    ImGui::TextDisabled("保存先: %s", GetAssetPath().c_str());
#endif
}

void AnimatorControllerEditor::DrawPreviewTargetControls() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_CUBE " Preview / Assign", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const char* label = previewTargetName_.empty() ? "(未選択)" : previewTargetName_.c_str();
    if (ImGui::BeginCombo("Scene Object", label)) {
        if (sceneManager_ && sceneManager_->GetCurrentScene()) {
            for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
                if (object && !object->IsEditorInternal() && ImGui::Selectable(object->GetName().c_str(), object->GetName() == previewTargetName_)) {
                    previewTargetName_ = object->GetName();
                }
            }
        }
        ImGui::EndCombo();
    }
    Object3d* target = ResolvePreviewTarget();
    if (target) {
        ImGui::Text("割り当て中: %s", target->GetAnimatorControllerPath().empty() ? "(なし)" : target->GetAnimatorControllerPath().c_str());
        if (ImGui::Button("このControllerをObjectへ割り当て", ImVec2(-1.0f, 0.0f))) {
            SaveController();
            target->SetAnimatorController(fileNameBuffer_);
        }
        if (selectedStateIndex_ >= 0 && selectedStateIndex_ < static_cast<int>(controller_.states.size())) {
            if (ImGui::Button(ICON_FA_PLAY " 選択Stateを再生", ImVec2(-1.0f, 0.0f))) {
                if (!target->HasAnimatorController()) {
                    target->SetAnimatorController(fileNameBuffer_);
                }
                target->PlayAnimatorState(controller_.states[selectedStateIndex_].name);
            }
        }
    } else {
        ImGui::TextDisabled("Preview対象を選ぶと、State再生とObjectへの割り当てを確認できます。");
    }
#endif
}

void AnimatorControllerEditor::DrawStateEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS " State追加")) {
        AnimatorStateDefinition state;
        state.name = "State" + std::to_string(controller_.states.size() + 1);
        controller_.states.push_back(state);
        selectedStateIndex_ = static_cast<int>(controller_.states.size()) - 1;
        if (controller_.entryState.empty()) controller_.entryState = state.name;
    }
    ImGui::BeginChild("AnimatorStateList", ImVec2(190.0f, 330.0f), true);
    for (int index = 0; index < static_cast<int>(controller_.states.size()); ++index) {
        std::string label = controller_.states[index].name;
        if (controller_.entryState == controller_.states[index].name) label = "[Entry] " + label;
        if (ImGui::Selectable(label.c_str(), selectedStateIndex_ == index)) selectedStateIndex_ = index;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (selectedStateIndex_ >= 0 && selectedStateIndex_ < static_cast<int>(controller_.states.size())) {
        auto& state = controller_.states[selectedStateIndex_];
        char stateName[128]{};
        strncpy_s(stateName, sizeof(stateName), state.name.c_str(), _TRUNCATE);
        const std::string oldName = state.name;
        if (ImGui::InputText("State名", stateName, sizeof(stateName))) {
            RenameState(oldName, stateName);
        }
        Object3d* target = ResolvePreviewTarget();
        const char* clipLabel = state.clipName.empty() ? "(なし)" : state.clipName.c_str();
        if (ImGui::BeginCombo("Model Animation Clip", clipLabel)) {
            if (ImGui::Selectable("(なし)", state.clipName.empty())) state.clipName.clear();
            if (target && target->GetModel()) {
                for (const auto& animation : target->GetModel()->GetModelData().animations) {
                    if (ImGui::Selectable(animation.name.c_str(), animation.name == state.clipName)) state.clipName = animation.name;
                }
            }
            ImGui::EndCombo();
        }
        const char* bodyClipLabel = state.bodyClipName.empty() ? "(Procedural / Code)" : state.bodyClipName.c_str();
        if (ImGui::BeginCombo("Body Animation Clip", bodyClipLabel)) {
            if (ImGui::Selectable("(Procedural / Code)", state.bodyClipName.empty())) state.bodyClipName.clear();
            for (const std::string& fileName : bodyAnimationFiles_) {
                if (ImGui::Selectable(fileName.c_str(), fileName == state.bodyClipName)) {
                    state.bodyClipName = fileName;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Body Clip未設定時は従来のコードAnimationを使用します。");
        ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 8.0f, "%.2fx");
        ImGui::Checkbox("Loop", &state.loop);
        ImGui::DragFloat("Default Blend", &state.blendDuration, 0.01f, 0.0f, 5.0f, "%.3f sec");
        ImGui::Combo("Default Easing", &state.blendEasing, kEasingNames, IM_ARRAYSIZE(kEasingNames));
        ImGui::DragFloat("Event Timeline Duration", &state.eventTimelineDuration, 0.01f, 0.01f, 120.0f, "%.2f sec");
        ImGui::TextDisabled("Model/Body Clipに長さがある場合はそちらを優先します。");

        ImGui::SeparatorText("Animation Events");
        if (ImGui::Button(ICON_FA_PLUS " Event追加")) {
            state.events.push_back({});
        }
        int removeEventIndex = -1;
        for (int eventIndex = 0; eventIndex < static_cast<int>(state.events.size()); ++eventIndex) {
            AnimatorEventDefinition& event = state.events[eventIndex];
            ImGui::PushID(eventIndex);
            char eventName[128]{};
            char eventPayload[256]{};
            strncpy_s(eventName, sizeof(eventName), event.name.c_str(), _TRUNCATE);
            strncpy_s(eventPayload, sizeof(eventPayload), event.payload.c_str(), _TRUNCATE);
            ImGui::Text("Event %02d", eventIndex + 1);
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_TRASH)) {
                removeEventIndex = eventIndex;
            }
            if (ImGui::InputText("名前", eventName, sizeof(eventName))) {
                event.name = eventName;
            }
            if (ImGui::InputText("Payload", eventPayload, sizeof(eventPayload))) {
                event.payload = eventPayload;
            }
            ImGui::SliderFloat("正規化時刻", &event.normalizedTime, 0.0f, 1.0f, "%.3f");
            ImGui::TextDisabled("FeedbackCue + VFX Cue名で、VFX/SE/カメラ/振動を自動再生します。");
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeEventIndex >= 0) {
            state.events.erase(state.events.begin() + removeEventIndex);
        }

        if (ImGui::Button("Entry Stateに設定", ImVec2(-1.0f, 0.0f))) controller_.entryState = state.name;
        ImGui::BeginDisabled(controller_.states.size() <= 1);
        if (ImGui::Button(ICON_FA_TRASH " State削除", ImVec2(-1.0f, 0.0f))) {
            const std::string removedName = state.name;
            controller_.states.erase(controller_.states.begin() + selectedStateIndex_);
            controller_.transitions.erase(
                std::remove_if(controller_.transitions.begin(), controller_.transitions.end(),
                    [&removedName](const AnimatorTransitionDefinition& transition) {
                        return transition.fromState == removedName || transition.toState == removedName;
                    }),
                controller_.transitions.end());
            if (controller_.entryState == removedName) controller_.entryState = controller_.states.front().name;
            selectedStateIndex_ = std::min(selectedStateIndex_, static_cast<int>(controller_.states.size()) - 1);
        }
        ImGui::EndDisabled();
    }
    ImGui::EndGroup();
#endif
}

void AnimatorControllerEditor::DrawParameterEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS " Parameter追加")) {
        AnimatorParameterDefinition parameter;
        parameter.name = "Parameter" + std::to_string(controller_.parameters.size() + 1);
        controller_.parameters.push_back(parameter);
        selectedParameterIndex_ = static_cast<int>(controller_.parameters.size()) - 1;
    }
    for (int index = 0; index < static_cast<int>(controller_.parameters.size()); ++index) {
        ImGui::PushID(index);
        if (ImGui::Selectable(controller_.parameters[index].name.c_str(), selectedParameterIndex_ == index)) selectedParameterIndex_ = index;
        ImGui::PopID();
    }
    if (selectedParameterIndex_ < 0 || selectedParameterIndex_ >= static_cast<int>(controller_.parameters.size())) return;
    auto& parameter = controller_.parameters[selectedParameterIndex_];
    char name[128]{};
    strncpy_s(name, sizeof(name), parameter.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("Parameter名", name, sizeof(name))) parameter.name = name;
    int type = static_cast<int>(parameter.type);
    if (ImGui::Combo("Type", &type, kParameterTypeNames, IM_ARRAYSIZE(kParameterTypeNames))) {
        parameter.type = static_cast<AnimatorParameterType>(type);
    }
    if (parameter.type == AnimatorParameterType::Float) ImGui::DragFloat("Default", &parameter.defaultFloat, 0.01f);
    if (parameter.type == AnimatorParameterType::Int) ImGui::DragInt("Default", &parameter.defaultInt);
    if (parameter.type == AnimatorParameterType::Bool) ImGui::Checkbox("Default", &parameter.defaultBool);
    if (Object3d* target = ResolvePreviewTarget()) {
        if (AnimatorControllerRuntime* runtime = target->GetAnimatorControllerRuntime()) {
            if (parameter.type == AnimatorParameterType::Float) {
                float value = runtime->GetFloat(parameter.name);
                if (ImGui::DragFloat("Runtime Value", &value, 0.01f)) runtime->SetFloat(parameter.name, value);
            } else if (parameter.type == AnimatorParameterType::Int) {
                int value = runtime->GetInt(parameter.name);
                if (ImGui::DragInt("Runtime Value", &value)) runtime->SetInt(parameter.name, value);
            } else if (parameter.type == AnimatorParameterType::Bool) {
                bool value = runtime->GetBool(parameter.name);
                if (ImGui::Checkbox("Runtime Value", &value)) runtime->SetBool(parameter.name, value);
            } else if (ImGui::Button("Trigger発火")) {
                runtime->SetTrigger(parameter.name);
            }
        }
    }
    if (ImGui::Button(ICON_FA_TRASH " Parameter削除")) {
        controller_.parameters.erase(controller_.parameters.begin() + selectedParameterIndex_);
        selectedParameterIndex_ = -1;
    }
#endif
}

void AnimatorControllerEditor::DrawTransitionEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS " Transition追加")) {
        AnimatorTransitionDefinition transition;
        transition.fromState = selectedStateIndex_ >= 0 && selectedStateIndex_ < static_cast<int>(controller_.states.size())
            ? controller_.states[selectedStateIndex_].name : "*";
        for (const auto& state : controller_.states) {
            if (state.name != transition.fromState) { transition.toState = state.name; break; }
        }
        controller_.transitions.push_back(transition);
        selectedTransitionIndex_ = static_cast<int>(controller_.transitions.size()) - 1;
    }
    for (int index = 0; index < static_cast<int>(controller_.transitions.size()); ++index) {
        const auto& transition = controller_.transitions[index];
        const std::string label = (transition.fromState.empty() || transition.fromState == "*" ? "Any State" : transition.fromState)
            + " -> " + transition.toState;
        ImGui::PushID(index);
        if (ImGui::Selectable(label.c_str(), selectedTransitionIndex_ == index)) selectedTransitionIndex_ = index;
        ImGui::PopID();
    }
    if (selectedTransitionIndex_ < 0 || selectedTransitionIndex_ >= static_cast<int>(controller_.transitions.size())) return;
    auto& transition = controller_.transitions[selectedTransitionIndex_];
    const char* fromLabel = transition.fromState.empty() || transition.fromState == "*" ? "Any State" : transition.fromState.c_str();
    if (ImGui::BeginCombo("From", fromLabel)) {
        if (ImGui::Selectable("Any State", transition.fromState.empty() || transition.fromState == "*")) transition.fromState = "*";
        for (const auto& state : controller_.states) {
            if (ImGui::Selectable(state.name.c_str(), transition.fromState == state.name)) transition.fromState = state.name;
        }
        ImGui::EndCombo();
    }
    const char* toLabel = transition.toState.empty() ? "(未設定)" : transition.toState.c_str();
    if (ImGui::BeginCombo("To", toLabel)) {
        for (const auto& state : controller_.states) {
            if (ImGui::Selectable(state.name.c_str(), transition.toState == state.name)) transition.toState = state.name;
        }
        ImGui::EndCombo();
    }
    ImGui::DragFloat("Duration", &transition.duration, 0.01f, 0.0f, 5.0f, "%.3f sec");
    ImGui::Combo("Easing", &transition.easing, kEasingNames, IM_ARRAYSIZE(kEasingNames));
    ImGui::Checkbox("Has Exit Time", &transition.hasExitTime);
    if (transition.hasExitTime) ImGui::SliderFloat("Exit Time", &transition.exitTime, 0.0f, 1.0f, "%.2f normalized");

    ImGui::SeparatorText("Conditions");
    if (ImGui::Button("Condition追加")) transition.conditions.push_back({});
    for (int index = 0; index < static_cast<int>(transition.conditions.size()); ++index) {
        auto& condition = transition.conditions[index];
        ImGui::PushID(1000 + index);
        const char* parameterLabel = condition.parameter.empty() ? "(未設定)" : condition.parameter.c_str();
        if (ImGui::BeginCombo("Parameter", parameterLabel)) {
            for (const auto& parameter : controller_.parameters) {
                if (ImGui::Selectable(parameter.name.c_str(), condition.parameter == parameter.name)) condition.parameter = parameter.name;
            }
            ImGui::EndCombo();
        }
        int mode = static_cast<int>(condition.mode);
        if (ImGui::Combo("Mode", &mode, kConditionModeNames, IM_ARRAYSIZE(kConditionModeNames))) condition.mode = static_cast<AnimatorConditionMode>(mode);
        if (condition.mode == AnimatorConditionMode::Greater || condition.mode == AnimatorConditionMode::Less ||
            condition.mode == AnimatorConditionMode::Equals || condition.mode == AnimatorConditionMode::NotEqual) {
            ImGui::DragFloat("Threshold", &condition.threshold, 0.01f);
        }
        if (ImGui::Button("Condition削除")) {
            transition.conditions.erase(transition.conditions.begin() + index);
            ImGui::PopID();
            --index;
            continue;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (ImGui::Button(ICON_FA_TRASH " Transition削除")) {
        controller_.transitions.erase(controller_.transitions.begin() + selectedTransitionIndex_);
        selectedTransitionIndex_ = -1;
    }
#endif
}

void AnimatorControllerEditor::RenameState(const std::string& oldName, const std::string& newName) {
    if (selectedStateIndex_ < 0 || selectedStateIndex_ >= static_cast<int>(controller_.states.size()) || newName.empty()) {
        return;
    }
    controller_.states[selectedStateIndex_].name = newName;
    if (controller_.entryState == oldName) controller_.entryState = newName;
    for (auto& transition : controller_.transitions) {
        if (transition.fromState == oldName) transition.fromState = newName;
        if (transition.toState == oldName) transition.toState = newName;
    }
}
