#include "SceneValidator.h"

#include "BaseScene.h"
#include "IconsFontAwesome5.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "TextureManager.h"
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace {
constexpr int kMaxKnownMaterialType = 25;

bool IsEditorOnlyObject(const Object3d* object) {
    if (!object) return false;
    const std::string& name = object->GetName();
    return name.rfind("__Editor_", 0) == 0 || object->GetClassName() == "EditorOnly";
}

std::string NormalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}
}

void SceneValidator::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
}

void SceneValidator::DrawImGui() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        ImGui::TextDisabled("現在のシーンがありません");
        return;
    }

    ImGui::Text(ICON_FA_CHECK_CIRCLE " Scene Validator");
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_SYNC " 再チェック")) {
        Refresh();
    }
    ImGui::SameLine();
    ImGui::Checkbox("自動チェック", &autoRefresh_);

    const char* filters[] = { "すべて", "Errorのみ", "Warning以上", "Infoのみ" };
    ImGui::Combo("表示フィルタ", &selectedSeverityFilter_, filters, IM_ARRAYSIZE(filters));

    if (autoRefresh_) {
        autoRefreshTimer_ += ImGui::GetIO().DeltaTime;
        if (autoRefreshTimer_ >= 1.0f) {
            autoRefreshTimer_ = 0.0f;
            Refresh();
        }
    }

    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    for (const Issue& issue : issues_) {
        if (issue.severity == Severity::Error) ++errorCount;
        else if (issue.severity == Severity::Warning) ++warningCount;
        else ++infoCount;
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Error: %d", errorCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Warning: %d", warningCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Info: %d", infoCount);

    ImGui::Separator();

    if (issues_.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "問題は見つかっていません");
        return;
    }

    if (ImGui::BeginTable("SceneValidatorIssues", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("重要度", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("カテゴリ", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("対象", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("内容", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const Issue& issue : issues_) {
            bool show = true;
            if (selectedSeverityFilter_ == 1) show = issue.severity == Severity::Error;
            if (selectedSeverityFilter_ == 2) show = issue.severity == Severity::Error || issue.severity == Severity::Warning;
            if (selectedSeverityFilter_ == 3) show = issue.severity == Severity::Info;
            if (!show) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GetSeverityColor(issue.severity)), "%s", GetSeverityLabel(issue.severity));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(issue.category.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(issue.objectName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", issue.message.c_str());
        }

        ImGui::EndTable();
    }
#endif
}

void SceneValidator::Refresh() {
    issues_.clear();
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    auto& objects = scene->GetObjects();

    std::map<int, std::vector<const Object3d*>> eventObjects;
    std::set<int> eventIDs;

    for (const auto& objectPtr : objects) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object)) continue;

        int eventID = object->GetEventID();
        if (eventID > 0) {
            eventIDs.insert(eventID);
            eventObjects[eventID].push_back(object);
        }
    }

    for (const auto& [eventID, list] : eventObjects) {
        if (list.size() <= 1) continue;
        std::string names;
        for (const Object3d* object : list) {
            if (!names.empty()) names += ", ";
            names += object->GetName();
        }
        AddIssue(Severity::Error, list.front(), "Event ID", "Event ID " + std::to_string(eventID) + " が重複しています: " + names);
    }

    for (const auto& objectPtr : objects) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object)) continue;

        const std::string className = object->GetClassName();
        const std::string modelName = object->GetModelName();
        const std::string saveCategory = object->GetSaveCategory();
        const auto& collider = object->GetColliderConfig();

        if (object->GetTargetID() > 0 && eventIDs.count(object->GetTargetID()) == 0) {
            AddIssue(Severity::Error, object, "Target ID", "Target ID " + std::to_string(object->GetTargetID()) + " の接続先が見つかりません");
        }

        if (className != "InvisibleBox" && className != "MeshRoot" && !object->IsCameraObject()) {
            if (modelName.empty()) {
                AddIssue(Severity::Error, object, "Model", "modelName が空です");
            }
            else if (!DoesModelExist(modelName)) {
                AddIssue(Severity::Error, object, "Model", "モデルが見つかりません: " + modelName);
            }
        }

        if (saveCategory != "Object" && saveCategory != "Player" && saveCategory != "Enemy" && saveCategory != "Camera") {
            AddIssue(Severity::Warning, object, "SaveCategory", "保存カテゴリが不明です: " + saveCategory);
        }

        int materialType = object->GetMaterialType();
        if (materialType < 0 || materialType > kMaxKnownMaterialType) {
            AddIssue(Severity::Error, object, "Material", "materialType が範囲外です: " + std::to_string(materialType));
        }

        int blendMode = static_cast<int>(object->GetBlendMode());
        if (blendMode < 0 || blendMode >= static_cast<int>(BlendMode::kCountOfBlendMode)) {
            AddIssue(Severity::Error, object, "Material", "blendMode が範囲外です: " + std::to_string(blendMode));
        }

        if (collider.type == ColliderType::kNone && (object->GetCollisionAttribute() != 0 || object->GetCollisionMask() != 0)) {
            AddIssue(Severity::Warning, object, "Collision", "形状タイプが None ですが、collisionAttribute または collisionMask が残っています");
        }

        if (collider.type != ColliderType::kNone && (object->GetCollisionAttribute() == 0 || object->GetCollisionMask() == 0)) {
            AddIssue(Severity::Info, object, "Collision", "コライダー形状がありますが、衝突属性かマスクが 0 です");
        }

        if (!object->GetTexturePath().empty() && !DoesFileExist(object->GetTexturePath())) {
            AddIssue(Severity::Warning, object, "Texture", "基本画像が見つかりません: " + object->GetTexturePath());
        }

        if (!object->GetNormalMapPath().empty() && !DoesFileExist(object->GetNormalMapPath())) {
            AddIssue(Severity::Warning, object, "Texture", "Normal Map が見つかりません: " + object->GetNormalMapPath());
        }

        if (!object->GetOrmMapPath().empty() && !DoesFileExist(object->GetOrmMapPath())) {
            AddIssue(Severity::Warning, object, "Texture", "ORM Map が見つかりません: " + object->GetOrmMapPath());
        }

        if ((saveCategory == "Enemy" || !object->GetEnemyType().empty()) && !object->param_.has_value()) {
            AddIssue(Severity::Warning, object, "Parameter", "敵扱いのオブジェクトですが param がありません");
        }

        if (!object->GetGimmickType().empty() && !object->param_.has_value()) {
            AddIssue(Severity::Warning, object, "Parameter", "ギミック種別がありますが param がありません");
        }
    }
}

void SceneValidator::AddIssue(Severity severity, const Object3d* object, const std::string& category, const std::string& message) {
    Issue issue;
    issue.severity = severity;
    issue.objectName = object ? object->GetName() : "(Scene)";
    issue.category = category;
    issue.message = message;
    issues_.push_back(std::move(issue));
}

bool SceneValidator::DoesModelExist(const std::string& modelName) const {
    if (modelName.empty()) return false;

    std::vector<std::string> loadedNames = ModelManager::GetInstance()->GetLoadedModelNames();
    if (std::find(loadedNames.begin(), loadedNames.end(), modelName) != loadedNames.end()) return true;

    std::filesystem::path keyPath(modelName);
    std::filesystem::path directory = std::filesystem::path("Resources/3DModel") / keyPath;
    std::string stem = keyPath.filename().string();

    static const char* extensions[] = { ".obj", ".gltf", ".glb" };
    for (const char* extension : extensions) {
        if (std::filesystem::exists(directory / (stem + extension))) return true;
    }

    return false;
}

bool SceneValidator::DoesFileExist(const std::string& path) const {
    if (path.empty()) return true;
    std::string normalized = NormalizePath(path);
    if (std::filesystem::exists(normalized)) return true;

    uint32_t handle = TextureManager::GetInstance()->GetSrvHandle(normalized);
    return handle != 0;
}

const char* SceneValidator::GetSeverityLabel(Severity severity) const {
    switch (severity) {
    case Severity::Error:
        return "Error";
    case Severity::Warning:
        return "Warning";
    case Severity::Info:
    default:
        return "Info";
    }
}

unsigned int SceneValidator::GetSeverityColor(Severity severity) const {
    switch (severity) {
    case Severity::Error:
        return IM_COL32(255, 80, 80, 255);
    case Severity::Warning:
        return IM_COL32(255, 210, 70, 255);
    case Severity::Info:
    default:
        return IM_COL32(110, 180, 255, 255);
    }
}
