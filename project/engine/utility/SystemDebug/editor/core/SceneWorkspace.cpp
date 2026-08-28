#include "SceneWorkspace.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "Camera.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"
#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

namespace {
using json = nlohmann::json;

const ImGuiKey kBookmarkKeys[9] = {
    ImGuiKey_1, ImGuiKey_2, ImGuiKey_3,
    ImGuiKey_4, ImGuiKey_5, ImGuiKey_6,
    ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
};

bool ReadVector3(const json& value, Vector3& outValue) {
    if (!value.is_array() || value.size() != 3) {
        return false;
    }
    outValue = {
        value[0].get<float>(),
        value[1].get<float>(),
        value[2].get<float>(),
    };
    return true;
}
}

void SceneWorkspace::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    observedSceneGeneration_ = sceneManager_ ? sceneManager_->GetSceneGeneration() : 0;
    LoadBookmarks();
}

void SceneWorkspace::Finalize() {
    RestoreAllTemporaryVisibility();
    SaveBookmarks();
    editor_ = nullptr;
    sceneManager_ = nullptr;
}

void SceneWorkspace::Update() {
    if (!sceneManager_) {
        return;
    }
    const std::uint64_t generation = sceneManager_->GetSceneGeneration();
    if (generation != observedSceneGeneration_) {
        observedSceneGeneration_ = generation;
        ResetSceneRuntimeState();
    }
}

void SceneWorkspace::HandleHotkeys() {
    if (!sceneManager_ || sceneManager_->IsPlaying()) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !io.KeyCtrl || io.KeyAlt) {
        return;
    }

    for (std::size_t index = 0; index < kBookmarkCount; ++index) {
        if (!ImGui::IsKeyPressed(kBookmarkKeys[index], false)) {
            continue;
        }
        if (io.KeyShift) {
            CaptureBookmark(index);
        }
        else {
            RecallBookmark(index);
        }
        break;
    }
}

void SceneWorkspace::DrawHierarchyPanel() {
    if (!editor_ || !sceneManager_ || !ImGui::CollapsingHeader(
        ICON_FA_MAP_MARKED_ALT " シーン編集ワークスペース",
        ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const bool hasSelection = !editor_->GetSelectedObjects().empty();
    ImGui::BeginDisabled(!hasSelection || sceneManager_->IsPlaying());
    if (ImGui::Button(ICON_FA_LOW_VISION " 選択のみ表示")) {
        IsolateSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_EYE_SLASH " 選択を隠す")) {
        HideSelectionTemporarily();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(GetTemporaryHiddenCount() == 0);
    if (ImGui::Button(ICON_FA_EYE " 全表示を復元")) {
        RestoreAllTemporaryVisibility();
    }
    ImGui::EndDisabled();

    const std::size_t hiddenCount = GetTemporaryHiddenCount();
    if (hiddenCount > 0) {
        ImGui::TextColored(
            ImVec4(0.35f, 0.82f, 1.0f, 1.0f),
            "作業用に一時非表示: %zu Object%s",
            hiddenCount,
            isolationActive_ ? " / 隔離中" : "");
        ImGui::TextDisabled("Sceneの表示フラグや保存JSONには反映されません。");
    }

    if (ImGui::TreeNodeEx("Layer一時表示", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const std::string& layer : CollectSceneLayers()) {
            bool visible = hiddenLayers_.find(layer) == hiddenLayers_.end();
            const std::string label = layer + "##SceneWorkspaceLayer";
            if (ImGui::Checkbox(label.c_str(), &visible)) {
                SetLayerVisible(layer, visible);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("カメラブックマーク", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Ctrl+Shift+1～9: 保存 / Ctrl+1～9: 復帰");
        auto& bookmarks = GetCurrentBookmarks();
        if (ImGui::BeginTable(
            "SceneCameraBookmarks",
            4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("番号", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn("地点");
            ImGui::TableSetupColumn("保存", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("削除", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            for (std::size_t index = 0; index < kBookmarkCount; ++index) {
                CameraBookmark& bookmark = bookmarks[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginDisabled(!bookmark.valid);
                const std::string numberLabel = std::to_string(index + 1);
                if (ImGui::SmallButton(numberLabel.c_str())) {
                    RecallBookmark(index);
                }
                ImGui::EndDisabled();
                ImGui::TableSetColumnIndex(1);
                if (bookmark.valid) {
                    ImGui::TextUnformatted(bookmark.objectName.empty() ? "カメラ地点" : bookmark.objectName.c_str());
                }
                else {
                    ImGui::TextDisabled("未登録");
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton(ICON_FA_SAVE)) {
                    CaptureBookmark(index);
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(!bookmark.valid);
                if (ImGui::SmallButton(ICON_FA_TIMES)) {
                    ClearBookmark(index);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

void SceneWorkspace::IsolateSelection() {
    if (!editor_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }

    std::unordered_set<Object3d*> visibleObjects;
    for (Object3d* object : editor_->GetSelectedObjects()) {
        CollectHierarchy(object, visibleObjects);
    }
    if (visibleObjects.empty()) {
        return;
    }

    for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (!object || object->IsEditorInternal()) {
            continue;
        }
        const bool keepVisible = visibleObjects.find(object.get()) != visibleObjects.end();
        object->SetEditorHiddenReason(kIsolationReason, !keepVisible);
        if (keepVisible) {
            object->SetEditorHiddenReason(kLayerReason | kManualReason, false);
        }
    }
    isolationActive_ = true;
    DebugConsole::GetInstance()->AddLog("Scene Workspace: 選択Objectを隔離表示しました。");
}

void SceneWorkspace::HideSelectionTemporarily() {
    if (!editor_) {
        return;
    }
    for (Object3d* object : editor_->GetSelectedObjects()) {
        if (object) {
            object->SetEditorHiddenReason(kManualReason, true);
        }
    }
}

void SceneWorkspace::ShowSelectionTemporarily() {
    if (!editor_) {
        return;
    }
    for (Object3d* object : editor_->GetSelectedObjects()) {
        if (object) {
            object->SetEditorHiddenReason(kIsolationReason | kLayerReason | kManualReason, false);
        }
    }
}

void SceneWorkspace::RestoreAllTemporaryVisibility() {
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (object) {
                object->ClearEditorHiddenReasons();
            }
        }
    }
    isolationActive_ = false;
    hiddenLayers_.clear();
}

void SceneWorkspace::SetLayerVisible(const std::string& layer, bool visible) {
    if (visible) {
        hiddenLayers_.erase(layer);
    }
    else {
        hiddenLayers_.insert(layer);
    }

    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }
    for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (!object) {
            continue;
        }
        const std::string objectLayer = object->GetLayer().empty() ? "Default" : object->GetLayer();
        if (objectLayer == layer) {
            object->SetEditorHiddenReason(kLayerReason, !visible);
        }
    }
}

std::size_t SceneWorkspace::GetTemporaryHiddenCount() const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(
        sceneManager_->GetCurrentScene()->GetObjects().begin(),
        sceneManager_->GetCurrentScene()->GetObjects().end(),
        [](const std::unique_ptr<Object3d>& object) {
            return object && object->IsEditorTemporarilyHidden();
        }));
}

std::string SceneWorkspace::GetCurrentSceneKey() const {
    if (!sceneManager_) {
        return "scene:unknown";
    }
    const SceneLoadContext& context = sceneManager_->GetActiveSceneLoadContext();
    if (context.IsSceneAsset()) {
        return "asset:" + context.sceneAssetId;
    }
    const std::string& sceneName = sceneManager_->GetCurrentSceneName();
    return "runtime:" + (sceneName.empty() ? std::string("unknown") : sceneName);
}

std::array<SceneWorkspace::CameraBookmark, SceneWorkspace::kBookmarkCount>& SceneWorkspace::GetCurrentBookmarks() {
    return bookmarksByScene_[GetCurrentSceneKey()];
}

const std::array<SceneWorkspace::CameraBookmark, SceneWorkspace::kBookmarkCount>* SceneWorkspace::FindCurrentBookmarks() const {
    const auto found = bookmarksByScene_.find(GetCurrentSceneKey());
    return found != bookmarksByScene_.end() ? &found->second : nullptr;
}

void SceneWorkspace::CaptureBookmark(std::size_t index) {
    if (index >= kBookmarkCount || !sceneManager_ || sceneManager_->IsPlaying()) {
        return;
    }
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        return;
    }

    CameraBookmark& bookmark = GetCurrentBookmarks()[index];
    bookmark.valid = true;
    bookmark.position = camera->GetEye();
    bookmark.rotation = camera->GetRotation();
    bookmark.objectGuid.clear();
    bookmark.objectName.clear();
    if (editor_) {
        if (Object3d* selected = editor_->GetSelectedObject()) {
            bookmark.objectGuid = selected->GetPersistentGuid();
            bookmark.objectName = selected->GetName();
        }
    }
    SaveBookmarks();
    DebugConsole::GetInstance()->AddLog(
        "Scene Workspace: カメラ地点 " + std::to_string(index + 1) + " を保存しました。");
}

void SceneWorkspace::RecallBookmark(std::size_t index) {
    const auto* bookmarks = FindCurrentBookmarks();
    if (!bookmarks || index >= kBookmarkCount || !(*bookmarks)[index].valid ||
        !sceneManager_ || sceneManager_->IsPlaying()) {
        return;
    }

    const CameraBookmark& bookmark = (*bookmarks)[index];
    CameraEditor* cameraEditor = CameraEditor::GetInstance();
    cameraEditor->SetMode(CameraEditor::Mode::Editor);
    cameraEditor->SetEditorCameraTransform(bookmark.position, bookmark.rotation);
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->Update();
    }

    if (editor_ && sceneManager_->GetCurrentScene() &&
        Object3d::IsPersistentGuidValid(bookmark.objectGuid)) {
        if (Object3d* object = sceneManager_->GetCurrentScene()->FindObjectByPersistentGuid(bookmark.objectGuid)) {
            editor_->SetSelectedObject(object);
            editor_->SyncObjectSelectionToInspector();
        }
    }
    DebugConsole::GetInstance()->AddLog(
        "Scene Workspace: カメラ地点 " + std::to_string(index + 1) + " へ移動しました。");
}

void SceneWorkspace::ClearBookmark(std::size_t index) {
    if (index >= kBookmarkCount) {
        return;
    }
    GetCurrentBookmarks()[index] = {};
    SaveBookmarks();
}

void SceneWorkspace::LoadBookmarks() {
    bookmarksByScene_.clear();
    std::ifstream file(bookmarkStatePath_);
    if (!file.is_open()) {
        return;
    }
    try {
        json root;
        file >> root;
        const json& scenes = root.value("scenes", json::object());
        if (!scenes.is_object()) {
            return;
        }
        for (auto sceneIt = scenes.begin(); sceneIt != scenes.end(); ++sceneIt) {
            if (!sceneIt.value().is_array()) {
                continue;
            }
            auto& bookmarks = bookmarksByScene_[sceneIt.key()];
            const std::size_t count = (std::min)(kBookmarkCount, sceneIt.value().size());
            for (std::size_t index = 0; index < count; ++index) {
                const json& source = sceneIt.value()[index];
                if (!source.is_object() || !source.value("valid", false)) {
                    continue;
                }
                CameraBookmark bookmark;
                bookmark.valid = ReadVector3(source.value("position", json::array()), bookmark.position) &&
                    ReadVector3(source.value("rotation", json::array()), bookmark.rotation);
                bookmark.objectGuid = source.value("objectGuid", std::string{});
                bookmark.objectName = source.value("objectName", std::string{});
                bookmarks[index] = std::move(bookmark);
            }
        }
    }
    catch (...) {
        bookmarksByScene_.clear();
    }
}

void SceneWorkspace::SaveBookmarks() const {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path statePath(bookmarkStatePath_);
    fs::create_directories(statePath.parent_path(), error);
    if (error) {
        return;
    }

    json root;
    root["version"] = 1;
    root["scenes"] = json::object();
    for (const auto& [sceneKey, bookmarks] : bookmarksByScene_) {
        json sceneBookmarks = json::array();
        for (const CameraBookmark& bookmark : bookmarks) {
            sceneBookmarks.push_back({
                { "valid", bookmark.valid },
                { "position", { bookmark.position.x, bookmark.position.y, bookmark.position.z } },
                { "rotation", { bookmark.rotation.x, bookmark.rotation.y, bookmark.rotation.z } },
                { "objectGuid", bookmark.objectGuid },
                { "objectName", bookmark.objectName },
            });
        }
        root["scenes"][sceneKey] = std::move(sceneBookmarks);
    }

    std::ofstream file(statePath);
    if (file.is_open()) {
        file << root.dump(2);
    }
}

std::vector<std::string> SceneWorkspace::CollectSceneLayers() const {
    std::set<std::string> layers;
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (object && !object->IsEditorInternal()) {
                layers.insert(object->GetLayer().empty() ? "Default" : object->GetLayer());
            }
        }
    }
    return { layers.begin(), layers.end() };
}

void SceneWorkspace::CollectHierarchy(Object3d* root, std::unordered_set<Object3d*>& objects) const {
    if (!root || !objects.insert(root).second) {
        return;
    }
    for (Object3d* child : root->GetChildren()) {
        CollectHierarchy(child, objects);
    }
}

void SceneWorkspace::ResetSceneRuntimeState() {
    isolationActive_ = false;
    hiddenLayers_.clear();
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }
    for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object) {
            object->ClearEditorHiddenReasons();
        }
    }
}

#endif
