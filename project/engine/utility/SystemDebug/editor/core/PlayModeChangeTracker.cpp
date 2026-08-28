#include "PlayModeChangeTracker.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <array>

void PlayModeChangeTracker::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
}

void PlayModeChangeTracker::Finalize() {
    Clear();
    sceneManager_ = nullptr;
    editor_ = nullptr;
}

void PlayModeChangeTracker::Clear() {
    baselineByGuid_.clear();
    candidates_.clear();
    open_ = false;
}

bool PlayModeChangeTracker::IsEligible(const Object3d* object) const {
    if (!object || object->IsEditorInternal() || object->GetPersistentGuid().empty()) return false;
    const std::string className = object->GetClassName();
    const std::string category = object->GetSaveCategory();
    // Player・Enemy等は実行時状態そのものが変わるため、停止時の差分候補へ混ぜません。
    static constexpr const char* runtimeClasses[] = {
        "Player", "Enemy", "Item", "Bullet"
    };
    if (std::any_of(std::begin(runtimeClasses), std::end(runtimeClasses), [&className](const char* value) {
        return className == value;
    })) return false;
    if (category == "Player" || category == "Enemy" || category == "Item") return false;
    return true;
}

bool PlayModeChangeTracker::IsRuntimeDrivenTransform(const Object3d* object) const {
    if (!object) return true;
    const std::string className = object->GetClassName();
    const std::string category = object->GetSaveCategory();
    return className == "Gimmick" || className == "Spawner" ||
        category == "Gimmick";
}

bool PlayModeChangeTracker::IsIdentityKey(const std::string& key) {
    return key == "guid" || key == "name" || key == "type" || key == "saveCategory" ||
        key == "parentName" || key == "parentGuid";
}

bool PlayModeChangeTracker::IsTransformKey(const std::string& key) {
    return key == "translate" || key == "scale" || key == "rotate" || key == "quaternion";
}

nlohmann::json PlayModeChangeTracker::ExtractTransform(const nlohmann::json& state) {
    nlohmann::json result = nlohmann::json::object();
    if (!state.is_object()) return result;
    for (auto iterator = state.begin(); iterator != state.end(); ++iterator) {
        if (IsTransformKey(iterator.key())) result[iterator.key()] = iterator.value();
    }
    return result;
}

nlohmann::json PlayModeChangeTracker::ExtractProperties(const nlohmann::json& state) {
    nlohmann::json result = nlohmann::json::object();
    if (!state.is_object()) return result;
    for (auto iterator = state.begin(); iterator != state.end(); ++iterator) {
        if (!IsTransformKey(iterator.key()) && !IsIdentityKey(iterator.key())) {
            result[iterator.key()] = iterator.value();
        }
    }
    return result;
}

void PlayModeChangeTracker::CaptureBaseline() {
    baselineByGuid_.clear();
    candidates_.clear();
    open_ = false;
    if (!sceneManager_ || !editor_ || sceneManager_->IsTransitioning()) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene) return;
    for (const auto& object : scene->GetObjects()) {
        if (!IsEligible(object.get())) continue;
        const std::string guid = object->EnsurePersistentGuid();
        BaselineEntry entry;
        entry.name = object->GetName();
        entry.className = object->GetClassName();
        entry.state = editor_->CaptureObjectState(object.get());
        baselineByGuid_[guid] = std::move(entry);
    }
}

void PlayModeChangeTracker::CaptureRuntimeChanges() {
    candidates_.clear();
    if (!sceneManager_ || !editor_ || baselineByGuid_.empty()) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene) return;
    for (const auto& object : scene->GetObjects()) {
        if (!IsEligible(object.get())) continue;
        const auto baseline = baselineByGuid_.find(object->GetPersistentGuid());
        if (baseline == baselineByGuid_.end()) continue;
        const nlohmann::json runtimeState = editor_->CaptureObjectState(object.get());
        const bool transformChanged = !IsRuntimeDrivenTransform(object.get()) &&
            ExtractTransform(baseline->second.state) != ExtractTransform(runtimeState);
        const bool propertiesChanged = ExtractProperties(baseline->second.state) != ExtractProperties(runtimeState);
        if (!transformChanged && !propertiesChanged) continue;

        ChangeCandidate candidate;
        candidate.guid = object->GetPersistentGuid();
        candidate.name = baseline->second.name;
        candidate.className = baseline->second.className;
        candidate.beforeState = baseline->second.state;
        candidate.runtimeState = runtimeState;
        candidate.transformChanged = transformChanged;
        candidate.propertiesChanged = propertiesChanged;
        candidate.applyTransform = transformChanged;
        candidate.applyProperties = propertiesChanged;
        candidates_.push_back(std::move(candidate));
    }
}

void PlayModeChangeTracker::OnSceneRestored(bool restored) {
    baselineByGuid_.clear();
    if (!restored) {
        candidates_.clear();
        open_ = false;
        return;
    }
    open_ = !candidates_.empty();
    if (open_) statusMessage_ = "Play中の編集可能な変更を検出しました。";
}

void PlayModeChangeTracker::ApplySelectedChanges() {
    if (!sceneManager_ || !editor_ || !sceneManager_->GetCurrentScene()) return;
    int appliedCount = 0;
    for (const ChangeCandidate& candidate : candidates_) {
        if ((!candidate.applyTransform || !candidate.transformChanged) &&
            (!candidate.applyProperties || !candidate.propertiesChanged)) continue;
        Object3d* object = sceneManager_->GetCurrentScene()->FindObjectByPersistentGuid(candidate.guid);
        if (!object) continue;
        const nlohmann::json beforeApply = editor_->CaptureObjectState(object);
        nlohmann::json merged = candidate.applyProperties ? candidate.runtimeState : beforeApply;

        for (auto iterator = beforeApply.begin(); iterator != beforeApply.end(); ++iterator) {
            if (IsIdentityKey(iterator.key())) merged[iterator.key()] = iterator.value();
            if (!candidate.applyTransform && IsTransformKey(iterator.key())) merged[iterator.key()] = iterator.value();
        }
        if (candidate.applyTransform) {
            for (auto iterator = candidate.runtimeState.begin(); iterator != candidate.runtimeState.end(); ++iterator) {
                if (IsTransformKey(iterator.key())) merged[iterator.key()] = iterator.value();
            }
        }

        object->ImportFromJson(merged);
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();
        editor_->RegisterObjectEdited(object, beforeApply, "Play中変更を持ち帰り");
        ++appliedCount;
    }
    statusMessage_ = std::to_string(appliedCount) + " Objectの変更を編集状態へ反映しました。";
    candidates_.clear();
    open_ = false;
}

void PlayModeChangeTracker::Draw() {
    if (!open_) return;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Play中変更の持ち帰り", &open_)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("停止前に変わった編集可能Objectだけを表示しています。Player・Enemyの状態とGimmickの実行時Transformは除外済みです。");
    ImGui::TextDisabled("Transformと設定値をObjectごとに選んで反映できます。反映操作はUndo可能です。");
    ImGui::Separator();
    ImGui::BeginChild("PlayModeChangeCandidates", ImVec2(0.0f, -52.0f), true);
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        ChangeCandidate& candidate = candidates_[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::SeparatorText(candidate.name.c_str());
        ImGui::TextDisabled("%s / %s", candidate.className.c_str(), candidate.guid.c_str());
        if (candidate.transformChanged) ImGui::Checkbox("Transformを反映", &candidate.applyTransform);
        else ImGui::TextDisabled("Transform変更なし");
        ImGui::SameLine();
        if (candidate.propertiesChanged) ImGui::Checkbox("設定値を反映", &candidate.applyProperties);
        else ImGui::TextDisabled("設定値変更なし");
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::Button("選択した変更を反映")) ApplySelectedChanges();
    ImGui::SameLine();
    if (ImGui::Button("すべて破棄")) {
        candidates_.clear();
        open_ = false;
        statusMessage_ = "Play中変更を破棄しました。";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", statusMessage_.c_str());
    ImGui::End();
}

#endif
