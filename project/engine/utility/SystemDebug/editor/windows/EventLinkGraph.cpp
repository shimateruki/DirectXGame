#define NOMINMAX
#include "EventLinkGraph.h"

#include "BaseScene.h"
#include "DebugEditor.h"
#include "EditorManager.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <map>

void EventLinkGraph::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
}

void EventLinkGraph::CollectNodes() {
    nodes_.clear();
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (const auto& object : objects) {
        if (!object) continue;

        int eventID = object->GetEventID();
        int targetID = object->GetTargetID();
        bool linked = eventID != -1 || targetID != -1;
        if (showOnlyLinked_ && !linked) continue;

        NodeInfo node;
        node.object = object.get();
        node.name = object->GetName().empty() ? "(No Name)" : object->GetName();
        node.eventID = eventID;
        node.targetID = targetID;
        node.hasMissingTarget = targetID != -1 && !HasEventID(targetID, nullptr);
        node.hasDuplicateID = eventID != -1 && CountEventID(eventID) >= 2;
        nodes_.push_back(node);
    }

    std::sort(nodes_.begin(), nodes_.end(), [](const NodeInfo& a, const NodeInfo& b) {
        int aKey = a.eventID == -1 ? 999999 : a.eventID;
        int bKey = b.eventID == -1 ? 999999 : b.eventID;
        if (aKey != bKey) return aKey < bKey;
        return a.name < b.name;
    });
}

bool EventLinkGraph::HasEventID(int id, Object3d* ignoreObject) const {
    if (id == -1 || !sceneManager_ || !sceneManager_->GetCurrentScene()) return false;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (const auto& object : objects) {
        if (!object || object.get() == ignoreObject) continue;
        if (object->GetEventID() == id) return true;
    }
    return false;
}

int EventLinkGraph::CountEventID(int id) const {
    if (id == -1 || !sceneManager_ || !sceneManager_->GetCurrentScene()) return 0;

    int count = 0;
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (const auto& object : objects) {
        if (object && object->GetEventID() == id) count++;
    }
    return count;
}

void EventLinkGraph::DrawImGui() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        ImGui::TextDisabled("シーンが読み込まれていません。");
        return;
    }

    CollectNodes();

    ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f), ICON_FA_PROJECT_DIAGRAM " Event Link Graph");
    ImGui::SameLine();
    ImGui::Checkbox("リンク済みのみ表示", &showOnlyLinked_);
    ImGui::Separator();

    int missingCount = 0;
    int duplicateCount = 0;
    for (const NodeInfo& node : nodes_) {
        if (node.hasMissingTarget) missingCount++;
        if (node.hasDuplicateID) duplicateCount++;
    }

    ImGui::Text("Objects: %d", static_cast<int>(nodes_.size()));
    ImGui::SameLine();
    ImGui::TextColored(missingCount > 0 ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
        "Missing Target: %d", missingCount);
    ImGui::SameLine();
    ImGui::TextColored(duplicateCount > 0 ? ImVec4(1.0f, 0.78f, 0.2f, 1.0f) : ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
        "Duplicate ID: %d", duplicateCount);

    ImVec2 canvasSize = ImVec2(ImGui::GetContentRegionAvail().x, 300.0f);
    ImGui::BeginChild("EventLinkGraphCanvas", canvasSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();

    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(35, 38, 42, 255));

    std::map<int, ImVec2> receiverPositions;
    std::vector<std::pair<const NodeInfo*, ImVec2>> senderPositions;

    float leftX = origin.x + 24.0f;
    float rightX = origin.x + size.x * 0.56f;
    float nodeW = std::max(150.0f, size.x * 0.34f);
    float nodeH = 44.0f;
    float y = origin.y + 22.0f;

    int receiverIndex = 0;
    for (const NodeInfo& node : nodes_) {
        if (node.eventID == -1) continue;

        ImVec2 pos = ImVec2(rightX, y + receiverIndex * 58.0f);
        receiverPositions[node.eventID] = ImVec2(pos.x, pos.y + nodeH * 0.5f);

        ImU32 color = node.hasDuplicateID ? IM_COL32(235, 190, 60, 255) : IM_COL32(75, 135, 190, 255);
        drawList->AddRectFilled(pos, ImVec2(pos.x + nodeW, pos.y + nodeH), color, 6.0f);
        drawList->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), IM_COL32(255, 255, 255, 255), node.name.c_str());
        std::string idText = "ID: " + std::to_string(node.eventID);
        drawList->AddText(ImVec2(pos.x + 10.0f, pos.y + 24.0f), IM_COL32(230, 240, 255, 255), idText.c_str());
        receiverIndex++;
    }

    int senderIndex = 0;
    for (const NodeInfo& node : nodes_) {
        if (node.targetID == -1) continue;

        ImVec2 pos = ImVec2(leftX, y + senderIndex * 58.0f);
        senderPositions.push_back({ &node, ImVec2(pos.x + nodeW, pos.y + nodeH * 0.5f) });

        ImU32 color = node.hasMissingTarget ? IM_COL32(210, 65, 65, 255) : IM_COL32(65, 150, 105, 255);
        drawList->AddRectFilled(pos, ImVec2(pos.x + nodeW, pos.y + nodeH), color, 6.0f);
        drawList->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), IM_COL32(255, 255, 255, 255), node.name.c_str());
        std::string targetText = "Target: " + std::to_string(node.targetID);
        drawList->AddText(ImVec2(pos.x + 10.0f, pos.y + 24.0f), IM_COL32(230, 255, 235, 255), targetText.c_str());
        senderIndex++;
    }

    for (const auto& sender : senderPositions) {
        const NodeInfo* node = sender.first;
        auto receiverIt = receiverPositions.find(node->targetID);
        if (receiverIt == receiverPositions.end()) continue;

        ImVec2 start = sender.second;
        ImVec2 end = receiverIt->second;
        ImVec2 c1 = ImVec2(start.x + 80.0f, start.y);
        ImVec2 c2 = ImVec2(end.x - 80.0f, end.y);
        drawList->AddBezierCubic(start, c1, c2, end, IM_COL32(90, 220, 255, 210), 2.5f);
        drawList->AddCircleFilled(end, 4.0f, IM_COL32(90, 220, 255, 255));
    }

    ImGui::EndChild();

    if (ImGui::BeginTable("EventLinkGraphTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Object");
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        int row = 0;
        for (const NodeInfo& node : nodes_) {
            ImGui::PushID(row++);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (node.hasMissingTarget) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Missing");
            }
            else if (node.hasDuplicateID) {
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.2f, 1.0f), "Duplicate");
            }
            else {
                ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "OK");
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(node.name.c_str());

            ImGui::TableSetColumnIndex(2);
            if (node.eventID == -1) ImGui::TextDisabled("-");
            else ImGui::Text("%d", node.eventID);

            ImGui::TableSetColumnIndex(3);
            if (node.targetID == -1) ImGui::TextDisabled("-");
            else ImGui::Text("%d", node.targetID);

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(node.object ? node.object->GetSaveCategory().c_str() : "-");

            ImGui::TableSetColumnIndex(5);
            if (ImGui::Button("選択")) {
                if (editor_ && node.object) {
                    editor_->SetSelectedObject(node.object);
                    EditorManager::GetInstance()->SetSelectedObject(editor_);
                    ImGui::SetScrollHereY();
                }
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
#endif
}
