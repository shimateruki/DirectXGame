#define NOMINMAX
#include "NodeGraphEditorWindow.h"

#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "imgui_node_editor.h"

#include <algorithm>
#include <cmath>

namespace ed = ax::NodeEditor;
using cg2::editor::NodeData;
using cg2::editor::NodePin;
using cg2::editor::NodePinKind;
using cg2::editor::NodeValueType;

namespace {
ImVec4 GetPinColor(NodeValueType type) {
    switch (type) {
    case NodeValueType::Flow: return ImVec4(1.0f, 0.78f, 0.22f, 1.0f);
    case NodeValueType::Bool: return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
    case NodeValueType::Int: return ImVec4(0.35f, 0.72f, 1.0f, 1.0f);
    case NodeValueType::Float: return ImVec4(0.35f, 0.95f, 0.62f, 1.0f);
    case NodeValueType::String: return ImVec4(0.95f, 0.52f, 1.0f, 1.0f);
    case NodeValueType::Object: return ImVec4(0.65f, 0.58f, 1.0f, 1.0f);
    case NodeValueType::Any: return ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
    default: return ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
    }
}

ImU32 ToU32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

const char* GetValueTypeLabel(NodeValueType type) {
    switch (type) {
    case NodeValueType::Flow: return "Flow";
    case NodeValueType::Bool: return "Bool";
    case NodeValueType::Int: return "Int";
    case NodeValueType::Float: return "Float";
    case NodeValueType::String: return "String";
    case NodeValueType::Object: return "Object";
    case NodeValueType::Any: return "Any";
    default: return "Any";
    }
}
} // namespace

NodeGraphEditorWindow::~NodeGraphEditorWindow() {
    Finalize();
}

void NodeGraphEditorWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    EnsureInitialized();
}

void NodeGraphEditorWindow::Finalize() {
#ifdef USE_IMGUI
    if (context_) {
        ed::DestroyEditor(context_);
        context_ = nullptr;
    }
#endif
    initialized_ = false;
}

void NodeGraphEditorWindow::EnsureInitialized() {
#ifdef USE_IMGUI
    if (initialized_) {
        return;
    }

    ed::Config config;
    config.SettingsFile = nullptr;
    context_ = ed::CreateEditor(&config);
    graph_.ResetToSample();
    statusMessage_ = "サンプルグラフを作成しました。";
    initialized_ = true;
    firstFrame_ = true;
#else
    initialized_ = true;
#endif
}

void NodeGraphEditorWindow::DrawImGui() {
#ifdef USE_IMGUI
    EnsureInitialized();

    ImGui::TextColored(ImVec4(0.45f, 0.82f, 1.0f, 1.0f), ICON_FA_PROJECT_DIAGRAM " Node Graph Core");
    ImGui::TextWrapped("外部ライブラリは表示と操作だけに使い、ノードデータ・保存形式・実行ロジックはCG2側で管理します。");
    ImGui::Separator();

    DrawToolbar();
    ImGui::Spacing();
    DrawCanvas();

    if (!statusMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }
#else
    ImGui::TextDisabled("Node Graph は USE_IMGUI 有効時のみ使用できます。");
#endif
}

void NodeGraphEditorWindow::DrawToolbar() {
#ifdef USE_IMGUI
    ImGui::PushItemWidth(360.0f);
    char pathBuffer[256] = {};
    strncpy_s(pathBuffer, graphPath_.c_str(), sizeof(pathBuffer) - 1);
    if (ImGui::InputText("保存先", pathBuffer, sizeof(pathBuffer))) {
        graphPath_ = pathBuffer;
    }
    ImGui::PopItemWidth();

    if (ImGui::Button(ICON_FA_SAVE " 保存")) {
        SaveGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " 読込")) {
        LoadGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC_ALT " サンプル再生成")) {
        graph_.ResetToSample();
        firstFrame_ = true;
        statusMessage_ = "サンプルグラフを再生成しました。";
    }

    ImGui::Separator();
    ImGui::TextUnformatted("ノード追加:");
    ImGui::SameLine();
    if (ImGui::Button("イベント開始")) AddNodeFromTemplate(0);
    ImGui::SameLine();
    if (ImGui::Button("待機")) AddNodeFromTemplate(1);
    ImGui::SameLine();
    if (ImGui::Button("ログ表示")) AddNodeFromTemplate(2);
    ImGui::SameLine();
    if (ImGui::Button("コメント")) AddNodeFromTemplate(3);
#endif
}

void NodeGraphEditorWindow::DrawCanvas() {
#ifdef USE_IMGUI
    if (!context_) return;

    ed::SetCurrentEditor(context_);
    ed::Begin("CG2NodeGraphCanvas", ImVec2(0.0f, 560.0f));

    for (NodeData& node : graph_.GetNodes()) {
        DrawNode(node);
        if (firstFrame_) {
            ed::SetNodePosition(ed::NodeId(node.id), ImVec2(node.editorX, node.editorY));
        }
        else {
            ImVec2 position = ed::GetNodePosition(ed::NodeId(node.id));
            node.editorX = position.x;
            node.editorY = position.y;
        }
    }

    for (const auto& link : graph_.GetLinks()) {
        const NodePin* startPin = graph_.FindPin(link.startPinId);
        const ImVec4 color = startPin ? GetPinColor(startPin->valueType) : ImVec4(0.8f, 0.9f, 1.0f, 1.0f);
        ed::Link(ed::LinkId(link.id), ed::PinId(link.startPinId), ed::PinId(link.endPinId), color, 2.5f);
    }

    HandleCreateLink();
    HandleDeleteLink();

    ed::End();
    ed::SetCurrentEditor(nullptr);
    firstFrame_ = false;
#endif
}

void NodeGraphEditorWindow::DrawNode(NodeData& node) {
#ifdef USE_IMGUI
    ed::BeginNode(ed::NodeId(node.id));

    ImGui::PushID(node.id);
    ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.42f, 1.0f), "%s", node.title.c_str());
    ImGui::TextDisabled("%s", node.type.c_str());
    if (!node.note.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 220.0f);
        ImGui::TextWrapped("%s", node.note.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

    const size_t rowCount = (std::max)(node.inputs.size(), node.outputs.size());
    for (size_t row = 0; row < rowCount; ++row) {
        if (row < node.inputs.size()) {
            DrawPinLabel(node.inputs[row]);
        }
        else {
            ImGui::Dummy(ImVec2(82.0f, ImGui::GetTextLineHeightWithSpacing()));
        }

        ImGui::SameLine(150.0f);
        if (row < node.outputs.size()) {
            DrawPinLabel(node.outputs[row]);
        }
    }

    ImGui::PopID();
    ed::EndNode();
#endif
}

void NodeGraphEditorWindow::DrawPinLabel(const NodePin& pin) {
#ifdef USE_IMGUI
    const ImVec4 color = GetPinColor(pin.valueType);
    const char* arrow = pin.kind == NodePinKind::Input ? "<" : ">";

    ed::BeginPin(ed::PinId(pin.id), pin.kind == NodePinKind::Input ? ed::PinKind::Input : ed::PinKind::Output);
    ImGui::TextColored(color, "%s %s", arrow, pin.name.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("型: %s", GetValueTypeLabel(pin.valueType));
        ImGui::Text("ID: %d", pin.id);
        ImGui::EndTooltip();
    }
    ed::EndPin();
#else
    (void)pin;
#endif
}

void NodeGraphEditorWindow::HandleCreateLink() {
#ifdef USE_IMGUI
    ed::PinId startPinId;
    ed::PinId endPinId;
    if (ed::BeginCreate(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), 2.0f)) {
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            const int startId = static_cast<int>(startPinId.Get());
            const int endId = static_cast<int>(endPinId.Get());
            std::string reason;
            if (graph_.CanCreateLink(startId, endId, &reason)) {
                if (ed::AcceptNewItem(ImVec4(0.45f, 1.0f, 0.65f, 1.0f), 3.0f)) {
                    graph_.AddLink(startId, endId);
                    statusMessage_ = "リンクを作成しました。";
                }
            }
            else {
                ed::RejectNewItem(ImVec4(1.0f, 0.28f, 0.28f, 1.0f), 2.0f);
                if (!reason.empty()) {
                    statusMessage_ = reason;
                }
            }
        }
    }
    ed::EndCreate();
#endif
}

void NodeGraphEditorWindow::HandleDeleteLink() {
#ifdef USE_IMGUI
    ed::LinkId deletedLinkId;
    if (ed::BeginDelete()) {
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            if (ed::AcceptDeletedItem()) {
                graph_.RemoveLink(static_cast<int>(deletedLinkId.Get()));
                statusMessage_ = "リンクを削除しました。";
            }
        }
    }
    ed::EndDelete();
#endif
}

void NodeGraphEditorWindow::AddNodeFromTemplate(int templateIndex) {
    const float baseX = 80.0f + static_cast<float>(graph_.GetNodes().size() % 4) * 260.0f;
    const float baseY = 160.0f + static_cast<float>(graph_.GetNodes().size() / 4) * 180.0f;

    if (templateIndex == 0) {
        NodeData& node = graph_.AddNode("Event.Start", "開始", baseX, baseY);
        graph_.AddOutputPin(node, "実行", NodeValueType::Flow);
        node.note = "処理の入口です。";
    }
    else if (templateIndex == 1) {
        NodeData& node = graph_.AddNode("Flow.Wait", "待機", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddOutputPin(node, "出", NodeValueType::Flow);
        graph_.AddInputPin(node, "秒", NodeValueType::Float);
        node.note = "指定秒数だけ待機する予定のノードです。";
    }
    else if (templateIndex == 2) {
        NodeData& node = graph_.AddNode("Debug.Log", "ログ表示", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddInputPin(node, "文字", NodeValueType::String);
        node.note = "デバッグログを出す予定のノードです。";
    }
    else {
        NodeData& node = graph_.AddNode("Comment", "コメント", baseX, baseY);
        node.note = "仕様メモや処理の意図を書いておくためのノードです。";
    }

    firstFrame_ = true;
    statusMessage_ = "ノードを追加しました。";
}

void NodeGraphEditorWindow::SaveGraph() {
    std::string error;
    if (graph_.SaveToFile(graphPath_, &error)) {
        statusMessage_ = "保存しました: " + graphPath_;
    }
    else {
        statusMessage_ = "保存に失敗しました: " + error;
    }
}

void NodeGraphEditorWindow::LoadGraph() {
    std::string error;
    if (graph_.LoadFromFile(graphPath_, &error)) {
        firstFrame_ = true;
        statusMessage_ = "読み込みました: " + graphPath_;
    }
    else {
        statusMessage_ = "読み込みに失敗しました: " + error;
    }
}
