#define NOMINMAX
#include "SceneSavePreview.h"

#ifdef USE_IMGUI
#include "IconsFontAwesome5.h"
#include "imgui.h"
#endif

#include <algorithm>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

// 保存前の差分を確認し、保存・取消の判断を受け付けます。
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
