#define NOMINMAX
#include "SceneSavePreview.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

#ifdef USE_IMGUI
#include "IconsFontAwesome5.h"
#include "imgui.h"
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
bool ReadJsonFile(const std::string& path, json& outJson) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        if (file.peek() == std::ifstream::traits_type::eof()) {
            outJson = json::object();
        }
        else {
            file >> outJson;
        }
        return true;
    }
    catch (...) {
        outJson = json::object();
        return false;
    }
}

bool HasObjectList(const json& value) {
    return value.is_object() && value.contains("objects") && value["objects"].is_array();
}

std::string JoinPath(const std::string& parent, const std::string& child) {
    if (parent.empty()) return child;
    return parent + "." + child;
}

std::string MakeArrayPath(const std::string& parent, size_t index) {
    std::ostringstream oss;
    oss << parent << "[" << index << "]";
    return oss.str();
}
}

void SceneSavePreview::Build(const std::vector<SceneSerializer::SaveTarget>& targets, const std::string& title) {
    targets_ = targets;
    title_ = title;
    savedFilesLabel_.clear();
    fileDiffs_.clear();
    summary_ = {};

    for (const auto& target : targets_) {
        if (!savedFilesLabel_.empty()) {
            savedFilesLabel_ += ", ";
        }
        savedFilesLabel_ += target.label;
        fileDiffs_.push_back(BuildFileDiff(target));
    }

    UpdateSummary();
}

void SceneSavePreview::Open() {
    isOpen_ = true;
    requestOpenPopup_ = true;
}

void SceneSavePreview::Close() {
    isOpen_ = false;
    requestOpenPopup_ = false;
}

SceneSavePreview::FileDiff SceneSavePreview::BuildFileDiff(const SceneSerializer::SaveTarget& target) const {
    FileDiff diff;
    diff.target = target;
    diff.oldFileExists = fs::exists(target.path);
    diff.hasObjectList = HasObjectList(target.data);

    json oldJson = json::object();
    if (diff.oldFileExists) {
        diff.oldJsonValid = ReadJsonFile(target.path, oldJson);
        if (!diff.oldJsonValid) {
            diff.fileDetails.push_back("既存JSONの読み込みに失敗しました。保存すると現在の内容で上書きします。");
        }
    }
    else {
        diff.oldJsonValid = true;
        diff.fileDetails.push_back("新規ファイルとして作成されます。");
    }

    if (diff.hasObjectList) {
        BuildObjectDiff(diff, oldJson, target.data);
    }
    else {
        BuildFileLevelDiff(diff, oldJson, target.data);
    }

    return diff;
}

void SceneSavePreview::BuildObjectDiff(FileDiff& diff, const json& oldJson, const json& newJson) const {
    std::map<std::string, json> oldObjects;
    std::map<std::string, json> newObjects;

    if (HasObjectList(oldJson)) {
        int unnamedIndex = 0;
        for (const auto& obj : oldJson["objects"]) {
            std::string name = GetObjectName(obj, "__old_unnamed_" + std::to_string(unnamedIndex++));
            oldObjects[name] = obj;
        }
    }

    if (HasObjectList(newJson)) {
        int unnamedIndex = 0;
        for (const auto& obj : newJson["objects"]) {
            std::string name = GetObjectName(obj, "__new_unnamed_" + std::to_string(unnamedIndex++));
            newObjects[name] = obj;
        }
    }

    for (const auto& [name, newObj] : newObjects) {
        auto oldIt = oldObjects.find(name);
        ObjectChange change;
        change.name = name;
        change.category = GetObjectCategory(newObj);

        if (oldIt == oldObjects.end()) {
            change.kind = ChangeKind::Added;
            change.details.push_back("type: " + JsonValueToText(newObj.contains("type") ? newObj["type"] : json("")));
            change.details.push_back("modelName: " + JsonValueToText(newObj.contains("modelName") ? newObj["modelName"] : json("")));
            if (newObj.contains("position")) change.details.push_back("position: " + JsonValueToText(newObj["position"]));
            diff.addedCount++;
        }
        else if (oldIt->second != newObj) {
            change.kind = ChangeKind::Modified;
            CollectJsonDiffs(oldIt->second, newObj, "", change.details, 12);
            if (change.details.empty()) {
                change.details.push_back("JSONの内容が変更されています。");
            }
            diff.modifiedCount++;
        }
        else {
            change.kind = ChangeKind::Unchanged;
            change.details.push_back("変更なし");
            diff.unchangedCount++;
        }

        diff.objects.push_back(std::move(change));
    }

    for (const auto& [name, oldObj] : oldObjects) {
        if (newObjects.find(name) != newObjects.end()) continue;

        ObjectChange change;
        change.kind = ChangeKind::Removed;
        change.name = name;
        change.category = GetObjectCategory(oldObj);
        change.details.push_back("保存後のJSONから削除されます。");
        if (oldObj.contains("type")) change.details.push_back("type: " + JsonValueToText(oldObj["type"]));
        diff.removedCount++;
        diff.objects.push_back(std::move(change));
    }

    std::sort(diff.objects.begin(), diff.objects.end(), [](const ObjectChange& a, const ObjectChange& b) {
        if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return a.name < b.name;
    });
}

void SceneSavePreview::BuildFileLevelDiff(FileDiff& diff, const json& oldJson, const json& newJson) const {
    if (!diff.oldFileExists) {
        diff.fileDetails.push_back("メタファイルを新規作成します。");
        return;
    }

    if (!diff.oldJsonValid) {
        diff.fileDetails.push_back("読み込めなかった既存ファイルを置き換えます。");
        return;
    }

    if (oldJson == newJson) {
        diff.fileDetails.push_back("ファイル内容に変更はありません。");
        return;
    }

    CollectJsonDiffs(oldJson, newJson, "", diff.fileDetails, 16);
    if (diff.fileDetails.empty()) {
        diff.fileDetails.push_back("ファイル内容が変更されています。");
    }
}

void SceneSavePreview::UpdateSummary() {
    summary_.fileCount = static_cast<int>(fileDiffs_.size());

    for (const auto& diff : fileDiffs_) {
        summary_.addedObjects += diff.addedCount;
        summary_.removedObjects += diff.removedCount;
        summary_.modifiedObjects += diff.modifiedCount;
        summary_.unchangedObjects += diff.unchangedCount;

        if (!diff.oldFileExists) {
            summary_.addedFiles++;
        }
        else if (!diff.oldJsonValid) {
            summary_.invalidFiles++;
        }
        else if (diff.addedCount > 0 || diff.removedCount > 0 || diff.modifiedCount > 0) {
            summary_.modifiedFiles++;
        }
        else if (!diff.hasObjectList && diff.fileDetails.size() == 1 && diff.fileDetails[0] != "ファイル内容に変更はありません。") {
            summary_.modifiedFiles++;
        }
    }
}

std::string SceneSavePreview::GetObjectName(const json& obj, const std::string& fallback) {
    if (obj.is_object() && obj.contains("name") && obj["name"].is_string()) {
        return obj["name"].get<std::string>();
    }
    return fallback;
}

std::string SceneSavePreview::GetObjectCategory(const json& obj) {
    if (obj.is_object() && obj.contains("saveCategory") && obj["saveCategory"].is_string()) {
        return obj["saveCategory"].get<std::string>();
    }
    return "Object";
}

std::string SceneSavePreview::JsonValueToText(const json& value) {
    if (value.is_null()) return "null";
    if (value.is_string()) return "\"" + value.get<std::string>() + "\"";
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number_float()) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << value.get<double>();
        return oss.str();
    }
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
    if (value.is_array()) {
        if (value.size() <= 6) {
            std::string text = "[";
            for (size_t i = 0; i < value.size(); ++i) {
                if (i > 0) text += ", ";
                text += JsonValueToText(value[i]);
            }
            text += "]";
            return text;
        }
        return "array(" + std::to_string(value.size()) + ")";
    }
    if (value.is_object()) return "object(" + std::to_string(value.size()) + ")";
    return value.dump();
}

void SceneSavePreview::CollectJsonDiffs(const json& before, const json& after, const std::string& path, std::vector<std::string>& outDetails, int maxDetails) {
    if (static_cast<int>(outDetails.size()) >= maxDetails) return;
    if (before == after) return;

    if (before.is_object() && after.is_object()) {
        std::vector<std::string> keys;
        for (auto it = before.begin(); it != before.end(); ++it) keys.push_back(it.key());
        for (auto it = after.begin(); it != after.end(); ++it) {
            if (std::find(keys.begin(), keys.end(), it.key()) == keys.end()) {
                keys.push_back(it.key());
            }
        }
        std::sort(keys.begin(), keys.end());

        for (const std::string& key : keys) {
            if (static_cast<int>(outDetails.size()) >= maxDetails) break;
            bool hasBefore = before.contains(key);
            bool hasAfter = after.contains(key);
            std::string childPath = JoinPath(path, key);

            if (!hasBefore) {
                outDetails.push_back(childPath + ": 追加 " + JsonValueToText(after[key]));
            }
            else if (!hasAfter) {
                outDetails.push_back(childPath + ": 削除 " + JsonValueToText(before[key]));
            }
            else {
                CollectJsonDiffs(before[key], after[key], childPath, outDetails, maxDetails);
            }
        }
        return;
    }

    if (before.is_array() && after.is_array()) {
        if (before.size() == after.size() && before.size() <= 3) {
            for (size_t i = 0; i < before.size(); ++i) {
                if (static_cast<int>(outDetails.size()) >= maxDetails) break;
                CollectJsonDiffs(before[i], after[i], MakeArrayPath(path, i), outDetails, maxDetails);
            }
            return;
        }
    }

    std::string label = path.empty() ? "(root)" : path;
    outDetails.push_back(label + ": " + JsonValueToText(before) + " -> " + JsonValueToText(after));
}

SceneSavePreview::Action SceneSavePreview::Draw() {
#ifdef USE_IMGUI
    if (!isOpen_) return Action::None;

    const char* popupName = "Scene Save Preview###SceneSavePreview";
    if (requestOpenPopup_) {
        ImGui::OpenPopup(popupName);
        requestOpenPopup_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(980.0f, 680.0f), ImGuiCond_Appearing);
    bool keepOpen = true;
    Action action = Action::None;

    if (ImGui::BeginPopupModal(popupName, &keepOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f), ICON_FA_SAVE " %s", title_.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("保存前にJSON差分を確認します");
        ImGui::Separator();

        DrawSummaryCards();
        ImGui::Spacing();
        DrawFileSummaryTable();

        ImGui::Separator();
        ImGui::Checkbox("変更なしのオブジェクトも表示", &showUnchanged_);
        ImGui::SameLine();
        if (!summary_.HasChanges()) {
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.6f, 1.0f), ICON_FA_CHECK_CIRCLE " 差分はありません");
        }
        else {
            ImGui::TextDisabled("詳細を開くと、変更されたプロパティを確認できます");
        }

        if (ImGui::BeginChild("SceneSavePreviewDiffList", ImVec2(0, -52.0f), true)) {
            for (auto& diff : fileDiffs_) {
                DrawFileDiff(diff);
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_TIMES " キャンセル", ImVec2(160.0f, 34.0f))) {
            action = Action::Cancel;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        float buttonSpacer = std::max(0.0f, ImGui::GetContentRegionAvail().x - 330.0f);
        ImGui::Dummy(ImVec2(buttonSpacer, 0.0f));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.60f, 0.32f, 1.0f));
        if (ImGui::Button(ICON_FA_CHECK " この内容で保存", ImVec2(170.0f, 34.0f))) {
            action = Action::Confirm;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);

        ImGui::EndPopup();
    }

    if (!keepOpen && action == Action::None) {
        action = Action::Cancel;
    }

    return action;
#else
    return Action::None;
#endif
}

#ifdef USE_IMGUI
void SceneSavePreview::DrawSummaryCards() const {
    if (ImGui::BeginTable("SceneSavePreviewSummaryCards", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Files");
        ImGui::Text("%d", summary_.fileCount);
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "+%d new / %d changed", summary_.addedFiles, summary_.modifiedFiles);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f), ICON_FA_PLUS_CIRCLE " Added");
        ImGui::Text("%d objects", summary_.addedObjects);

        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), ICON_FA_MINUS_CIRCLE " Removed");
        ImGui::Text("%d objects", summary_.removedObjects);

        ImGui::TableSetColumnIndex(3);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), ICON_FA_EDIT " Modified");
        ImGui::Text("%d objects", summary_.modifiedObjects);

        ImGui::EndTable();
    }
}

void SceneSavePreview::DrawFileSummaryTable() const {
    if (!ImGui::BeginTable("SceneSavePreviewFiles", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("+", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("-", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("~", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("=", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableHeadersRow();

    for (const auto& diff : fileDiffs_) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(diff.target.path.c_str());

        ImGui::TableSetColumnIndex(1);
        if (!diff.oldFileExists) {
            DrawChangeBadge(ChangeKind::FileAdded);
        }
        else if (!diff.oldJsonValid) {
            DrawChangeBadge(ChangeKind::FileInvalid);
        }
        else if (diff.addedCount > 0 || diff.removedCount > 0 || diff.modifiedCount > 0) {
            DrawChangeBadge(ChangeKind::FileModified);
        }
        else {
            DrawChangeBadge(ChangeKind::FileUnchanged);
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d", diff.addedCount);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%d", diff.removedCount);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%d", diff.modifiedCount);
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%d", diff.unchangedCount);
    }

    ImGui::EndTable();
}

void SceneSavePreview::DrawFileDiff(FileDiff& diff) {
    int visibleChanges = 0;
    for (const auto& change : diff.objects) {
        if (showUnchanged_ || change.kind != ChangeKind::Unchanged) {
            visibleChanges++;
        }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (diff.addedCount > 0 || diff.removedCount > 0 || diff.modifiedCount > 0 || !diff.oldFileExists || !diff.oldJsonValid) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    std::string header = diff.target.label + " - " + diff.target.path;
    header += "  (+";
    header += std::to_string(diff.addedCount);
    header += " / -";
    header += std::to_string(diff.removedCount);
    header += " / ~";
    header += std::to_string(diff.modifiedCount);
    header += ")";

    if (!ImGui::CollapsingHeader(header.c_str(), flags)) {
        return;
    }

    for (const std::string& detail : diff.fileDetails) {
        ImGui::TextWrapped("%s", detail.c_str());
    }

    if (!diff.hasObjectList) {
        return;
    }

    if (visibleChanges == 0) {
        ImGui::TextDisabled("表示対象の変更はありません。");
        return;
    }

    if (!ImGui::BeginTable(("SceneSavePreviewObjects_" + diff.target.path).c_str(), 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 82.0f);
    ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    int rowId = 0;
    for (const auto& change : diff.objects) {
        if (!showUnchanged_ && change.kind == ChangeKind::Unchanged) continue;

        ImGui::PushID(rowId++);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawChangeBadge(change.kind);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(change.category.c_str());

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(change.name.c_str());

        ImGui::TableSetColumnIndex(3);
        if (change.details.empty()) {
            ImGui::TextDisabled("-");
        }
        else {
            ImGui::TextWrapped("%s", change.details[0].c_str());
            if (change.details.size() > 1) {
                if (ImGui::TreeNode("詳細")) {
                    for (size_t i = 1; i < change.details.size(); ++i) {
                        ImGui::BulletText("%s", change.details[i].c_str());
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::EndTable();
}

void SceneSavePreview::DrawChangeBadge(ChangeKind kind) const {
    ImVec4 color = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
    switch (kind) {
    case ChangeKind::Added:
    case ChangeKind::FileAdded:
        color = ImVec4(0.35f, 1.0f, 0.55f, 1.0f);
        break;
    case ChangeKind::Removed:
        color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        break;
    case ChangeKind::Modified:
    case ChangeKind::FileModified:
        color = ImVec4(1.0f, 0.78f, 0.22f, 1.0f);
        break;
    case ChangeKind::FileInvalid:
        color = ImVec4(1.0f, 0.25f, 0.75f, 1.0f);
        break;
    case ChangeKind::Unchanged:
    case ChangeKind::FileUnchanged:
    default:
        color = ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
        break;
    }

    ImGui::TextColored(color, "%s", GetKindLabel(kind));
}

const char* SceneSavePreview::GetKindLabel(ChangeKind kind) const {
    switch (kind) {
    case ChangeKind::Added:
        return "Added";
    case ChangeKind::Removed:
        return "Removed";
    case ChangeKind::Modified:
        return "Modified";
    case ChangeKind::FileAdded:
        return "New File";
    case ChangeKind::FileModified:
        return "Changed";
    case ChangeKind::FileInvalid:
        return "Invalid";
    case ChangeKind::Unchanged:
    case ChangeKind::FileUnchanged:
    default:
        return "No Change";
    }
}
#endif
