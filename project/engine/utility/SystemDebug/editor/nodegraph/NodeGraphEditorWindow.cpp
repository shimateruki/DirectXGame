#define NOMINMAX
#include "NodeGraphEditorWindow.h"

#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "imgui_node_editor.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace ed = ax::NodeEditor;
using cg2::editor::NodeData;
using cg2::editor::NodeGraphIssue;
using cg2::editor::NodeIssueSeverity;
using cg2::editor::NodePin;
using cg2::editor::NodePinKind;
using cg2::editor::NodeProperty;
using cg2::editor::NodePropertyType;
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
    case NodeValueType::Effect: return ImVec4(0.45f, 1.0f, 0.92f, 1.0f);
    case NodeValueType::Scene: return ImVec4(1.0f, 0.62f, 0.35f, 1.0f);
    case NodeValueType::Any: return ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
    default: return ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
    }
}

ImU32 ToU32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

const char* GetTemplateDescription(const std::string& type) {
    if (type == "Event.Start") return "グラフの開始地点。";
    if (type == "Event.OnHit") return "対象オブジェクトが接触した時に開始。";
    if (type == "Flow.Branch") return "条件でTrue/Falseに分岐。";
    if (type == "Flow.Wait") return "指定秒数だけ処理を待つ。";
    if (type == "Action.SetVisible") return "対象オブジェクトの表示を切り替える。";
    if (type == "Action.PlayEffect") return "対象位置にエフェクトを出す。";
    if (type == "Action.ChangeScene") return "指定シーンへ遷移する。";
    if (type == "Debug.Log") return "デバッグログを出す。";
    return "仕様メモを書く。";
}

ImVec4 GetIssueColor(NodeIssueSeverity severity) {
    switch (severity) {
    case NodeIssueSeverity::Error: return ImVec4(1.0f, 0.32f, 0.32f, 1.0f);
    case NodeIssueSeverity::Warning: return ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
    case NodeIssueSeverity::Info: return ImVec4(0.45f, 1.0f, 0.62f, 1.0f);
    default: return ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
    }
}

void CopyStringToBuffer(const std::string& value, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) return;
    buffer[0] = '\0';
    strncpy_s(buffer, bufferSize, value.c_str(), bufferSize - 1);
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
    ImGui::TextWrapped("ノードの表示は外部ライブラリ、意味・保存・検証・将来の実行はCG2側のNodeGraphCoreで管理します。");
    ImGui::Separator();

    DrawToolbar();
    ImGui::Spacing();

    ImGui::Columns(2, "NodeGraphMainColumns", true);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.66f);
    DrawCanvas();
    ImGui::NextColumn();
    DrawSidePanel();
    ImGui::Columns(1);

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
    CopyStringToBuffer(graphPath_, pathBuffer, sizeof(pathBuffer));
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
        selectedNodeId_ = 0;
        firstFrame_ = true;
        statusMessage_ = "サンプルグラフを再生成しました。";
    }

    ImGui::Separator();
    ImGui::TextUnformatted("ノード追加:");
    ImGui::SameLine();
    if (ImGui::Button("開始")) AddNodeFromTemplate("Event.Start");
    ImGui::SameLine();
    if (ImGui::Button("接触イベント")) AddNodeFromTemplate("Event.OnHit");
    ImGui::SameLine();
    if (ImGui::Button("分岐")) AddNodeFromTemplate("Flow.Branch");
    ImGui::SameLine();
    if (ImGui::Button("待機")) AddNodeFromTemplate("Flow.Wait");
    ImGui::SameLine();
    if (ImGui::Button("表示切替")) AddNodeFromTemplate("Action.SetVisible");
    ImGui::SameLine();
    if (ImGui::Button("エフェクト")) AddNodeFromTemplate("Action.PlayEffect");
    ImGui::SameLine();
    if (ImGui::Button("シーン遷移")) AddNodeFromTemplate("Action.ChangeScene");
    ImGui::SameLine();
    if (ImGui::Button("ログ")) AddNodeFromTemplate("Debug.Log");
    ImGui::SameLine();
    if (ImGui::Button("コメント")) AddNodeFromTemplate("Comment");
#endif
}

void NodeGraphEditorWindow::DrawCanvas() {
#ifdef USE_IMGUI
    if (!context_) return;

    ed::SetCurrentEditor(context_);
    ed::Begin("CG2NodeGraphCanvas", ImVec2(0.0f, 620.0f));

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

void NodeGraphEditorWindow::DrawSidePanel() {
#ifdef USE_IMGUI
    DrawSelectedNodeInspector();
    ImGui::Separator();
    DrawValidationPanel();
    ImGui::Separator();
    DrawExecutionPreviewPanel();
#endif
}

void NodeGraphEditorWindow::DrawNode(NodeData& node) {
#ifdef USE_IMGUI
    ed::BeginNode(ed::NodeId(node.id));

    ImGui::PushID(node.id);
    const bool selected = selectedNodeId_ == node.id;
    ImGui::TextColored(selected ? ImVec4(0.45f, 1.0f, 0.75f, 1.0f) : ImVec4(1.0f, 0.86f, 0.42f, 1.0f), "%s", node.title.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("編集")) {
        SelectNode(node.id);
    }
    ImGui::TextDisabled("%s", node.type.c_str());
    ImGui::TextColored(ImVec4(0.62f, 0.78f, 1.0f, 1.0f), "%s", GetTemplateDescription(node.type));
    if (!node.note.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
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
            ImGui::Dummy(ImVec2(92.0f, ImGui::GetTextLineHeightWithSpacing()));
        }

        ImGui::SameLine(170.0f);
        if (row < node.outputs.size()) {
            DrawPinLabel(node.outputs[row]);
        }
    }

    if (!node.properties.empty()) {
        ImGui::Separator();
        for (const NodeProperty& property : node.properties) {
            ImGui::TextDisabled("%s: %s", property.label.c_str(), property.value.c_str());
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
        ImGui::Text("型: %s", cg2::editor::NodeGraphCore::ToString(pin.valueType));
        ImGui::Text("ID: %d", pin.id);
        ImGui::EndTooltip();
    }
    ed::EndPin();
#else
    (void)pin;
#endif
}

void NodeGraphEditorWindow::DrawSelectedNodeInspector() {
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.45f, 0.82f, 1.0f, 1.0f), ICON_FA_SLIDERS_H " ノード設定");
    NodeData* node = GetSelectedNode();
    if (!node) {
        ImGui::TextDisabled("ノード内の『編集』を押すと、ここで意味やパラメータを調整できます。");
        return;
    }

    char titleBuffer[128] = {};
    CopyStringToBuffer(node->title, titleBuffer, sizeof(titleBuffer));
    if (ImGui::InputText("表示名", titleBuffer, sizeof(titleBuffer))) {
        node->title = titleBuffer;
    }

    char noteBuffer[512] = {};
    CopyStringToBuffer(node->note, noteBuffer, sizeof(noteBuffer));
    if (ImGui::InputTextMultiline("説明", noteBuffer, sizeof(noteBuffer), ImVec2(-1.0f, 80.0f))) {
        node->note = noteBuffer;
    }

    ImGui::TextDisabled("種類: %s", node->type.c_str());
    ImGui::TextWrapped("意味: %s", GetTemplateDescription(node->type));

    if (!node->properties.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("プロパティ");
        for (NodeProperty& property : node->properties) {
            DrawPropertyEditor(property);
        }
    }

    ImGui::Separator();
    if (ImGui::Button(ICON_FA_TRASH " ノード削除")) {
        const int removedId = node->id;
        if (graph_.RemoveNode(removedId)) {
            selectedNodeId_ = 0;
            statusMessage_ = "ノードを削除しました。";
        }
    }
#endif
}

void NodeGraphEditorWindow::DrawValidationPanel() {
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.45f, 0.82f, 1.0f, 1.0f), ICON_FA_CHECK_CIRCLE " 検証");
    const auto issues = graph_.Validate();
    for (const NodeGraphIssue& issue : issues) {
        ImGui::TextColored(GetIssueColor(issue.severity), "[%s] %s", cg2::editor::NodeGraphCore::ToString(issue.severity), issue.message.c_str());
        if (issue.nodeId != 0) {
            ImGui::SameLine();
            ImGui::PushID(issue.nodeId + issue.pinId);
            if (ImGui::SmallButton("選択")) {
                SelectNode(issue.nodeId);
            }
            ImGui::PopID();
        }
    }
#endif
}

void NodeGraphEditorWindow::DrawExecutionPreviewPanel() {
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.45f, 0.82f, 1.0f, 1.0f), ICON_FA_PLAY " 実行順プレビュー");
    const auto steps = graph_.BuildExecutionPreview();
    if (steps.empty()) {
        ImGui::TextDisabled("開始ノードから辿れる実行フローがありません。実行ピン同士を接続してください。");
        return;
    }

    for (const auto& step : steps) {
        ImGui::Text("%02d. %s", step.order, step.title.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", step.type.c_str());
    }
#endif
}

void NodeGraphEditorWindow::DrawPropertyEditor(NodeProperty& property) {
#ifdef USE_IMGUI
    ImGui::PushID(property.key.c_str());
    if (property.type == NodePropertyType::Bool) {
        bool value = property.value == "true" || property.value == "1";
        if (ImGui::Checkbox(property.label.c_str(), &value)) {
            property.value = value ? "true" : "false";
        }
    }
    else if (property.type == NodePropertyType::Int) {
        int value = 0;
        try { value = std::stoi(property.value); } catch (...) { value = 0; }
        if (ImGui::InputInt(property.label.c_str(), &value)) {
            property.value = std::to_string(value);
        }
    }
    else if (property.type == NodePropertyType::Float) {
        float value = 0.0f;
        try { value = std::stof(property.value); } catch (...) { value = 0.0f; }
        if (ImGui::InputFloat(property.label.c_str(), &value, 0.1f, 1.0f, "%.3f")) {
            property.value = std::to_string(value);
        }
    }
    else {
        char buffer[256] = {};
        CopyStringToBuffer(property.value, buffer, sizeof(buffer));
        if (ImGui::InputText(property.label.c_str(), buffer, sizeof(buffer))) {
            property.value = buffer;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", cg2::editor::NodeGraphCore::ToString(property.type));
    }
    ImGui::PopID();
#else
    (void)property;
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

void NodeGraphEditorWindow::AddNodeFromTemplate(const std::string& templateType) {
    const float baseX = 80.0f + static_cast<float>(graph_.GetNodes().size() % 4) * 290.0f;
    const float baseY = 180.0f + static_cast<float>(graph_.GetNodes().size() / 4) * 220.0f;
    NodeData* created = nullptr;

    if (templateType == "Event.Start") {
        NodeData& node = graph_.AddNode("Event.Start", "開始", baseX, baseY);
        graph_.AddOutputPin(node, "実行", NodeValueType::Flow);
        node.note = "シーン開始や任意イベントの入口です。";
        created = &node;
    }
    else if (templateType == "Event.OnHit") {
        NodeData& node = graph_.AddNode("Event.OnHit", "接触イベント", baseX, baseY);
        graph_.AddOutputPin(node, "実行", NodeValueType::Flow);
        graph_.AddOutputPin(node, "相手", NodeValueType::Object);
        graph_.AddProperty(node, "selfObject", "対象オブジェクト", NodePropertyType::ObjectName, "Player");
        graph_.AddProperty(node, "otherTag", "相手タグ", NodePropertyType::String, "Enemy");
        node.note = "対象が指定タグの相手に触れた時に発火する想定です。";
        created = &node;
    }
    else if (templateType == "Flow.Branch") {
        NodeData& node = graph_.AddNode("Flow.Branch", "分岐", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddInputPin(node, "条件", NodeValueType::Bool);
        graph_.AddOutputPin(node, "True", NodeValueType::Flow);
        graph_.AddOutputPin(node, "False", NodeValueType::Flow);
        graph_.AddProperty(node, "defaultCondition", "仮条件", NodePropertyType::Bool, "true");
        node.note = "条件で実行先を分けるノードです。";
        created = &node;
    }
    else if (templateType == "Flow.Wait") {
        NodeData& node = graph_.AddNode("Flow.Wait", "待機", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddOutputPin(node, "出", NodeValueType::Flow);
        graph_.AddProperty(node, "seconds", "待機秒数", NodePropertyType::Float, "0.5");
        node.note = "演出の間を作るために処理を待たせます。";
        created = &node;
    }
    else if (templateType == "Action.SetVisible") {
        NodeData& node = graph_.AddNode("Action.SetVisible", "表示切替", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddOutputPin(node, "出", NodeValueType::Flow);
        graph_.AddInputPin(node, "対象", NodeValueType::Object);
        graph_.AddProperty(node, "targetObject", "対象オブジェクト", NodePropertyType::ObjectName, "ObjectName");
        graph_.AddProperty(node, "visible", "表示する", NodePropertyType::Bool, "true");
        node.note = "ステージギミックや演出用オブジェクトの表示状態を切り替えます。";
        created = &node;
    }
    else if (templateType == "Action.PlayEffect") {
        NodeData& node = graph_.AddNode("Action.PlayEffect", "エフェクト再生", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddOutputPin(node, "出", NodeValueType::Flow);
        graph_.AddInputPin(node, "対象", NodeValueType::Object);
        graph_.AddProperty(node, "effectName", "エフェクト名", NodePropertyType::EffectName, "HitSpark");
        graph_.AddProperty(node, "targetObject", "対象オブジェクト", NodePropertyType::ObjectName, "Player");
        node.note = "対象位置にエフェクトを発生させるアクションです。";
        created = &node;
    }
    else if (templateType == "Action.ChangeScene") {
        NodeData& node = graph_.AddNode("Action.ChangeScene", "シーン遷移", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddProperty(node, "sceneName", "遷移先", NodePropertyType::SceneName, "TITLE");
        graph_.AddProperty(node, "fadeSeconds", "フェード秒数", NodePropertyType::Float, "0.5");
        node.note = "フェードつきでシーンを切り替える想定です。";
        created = &node;
    }
    else if (templateType == "Debug.Log") {
        NodeData& node = graph_.AddNode("Debug.Log", "ログ表示", baseX, baseY);
        graph_.AddInputPin(node, "入", NodeValueType::Flow);
        graph_.AddOutputPin(node, "出", NodeValueType::Flow);
        graph_.AddProperty(node, "message", "表示文字", NodePropertyType::String, "Debug log");
        node.note = "ランタイム接続の確認用にログを出します。";
        created = &node;
    }
    else {
        NodeData& node = graph_.AddNode("Comment", "コメント", baseX, baseY);
        graph_.AddProperty(node, "text", "メモ", NodePropertyType::String, "ここに仕様メモを書く");
        node.note = "仕様や意図を書いて、あとから見返しやすくします。";
        created = &node;
    }

    if (created) {
        SelectNode(created->id);
    }
    firstFrame_ = true;
    statusMessage_ = "ノードを追加しました: " + templateType;
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
        selectedNodeId_ = 0;
        firstFrame_ = true;
        statusMessage_ = "読み込みました: " + graphPath_;
    }
    else {
        statusMessage_ = "読み込みに失敗しました: " + error;
    }
}

void NodeGraphEditorWindow::SelectNode(int nodeId) {
    selectedNodeId_ = nodeId;
#ifdef USE_IMGUI
    if (context_) {
        ed::SetCurrentEditor(context_);
        ed::SelectNode(ed::NodeId(nodeId), false);
        ed::NavigateToSelection(false, 0.25f);
        ed::SetCurrentEditor(nullptr);
    }
#endif
}

NodeData* NodeGraphEditorWindow::GetSelectedNode() {
    if (selectedNodeId_ == 0) {
        return nullptr;
    }
    return graph_.FindNode(selectedNodeId_);
}