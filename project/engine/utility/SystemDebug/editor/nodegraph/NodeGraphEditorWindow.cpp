#include "NodeGraphEditorWindow.h"

#include "DebugEditor.h"
#include "NodeGraphTemplateRegistry.h"

#ifdef USE_IMGUI
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "imgui.h"
#include "imgui_node_editor.h"
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef USE_IMGUI
namespace ed = ax::NodeEditor;
#endif

using cg2::editor::NodeData;
using cg2::editor::NodeDryRunBehavior;
using cg2::editor::NodeGraphIssue;
using cg2::editor::NodeGraphTemplateRegistry;
using cg2::editor::NodeIssueSeverity;
using cg2::editor::NodePin;
using cg2::editor::NodePinKind;
using cg2::editor::NodeProperty;
using cg2::editor::NodePropertyType;
using cg2::editor::NodeTemplateCategory;
using cg2::editor::NodeTemplateDefinition;
using cg2::editor::NodeValueType;

namespace {

#ifdef USE_IMGUI

int ToInt(ed::NodeId id) {
    return static_cast<int>(id.Get());
}

int ToInt(ed::PinId id) {
    return static_cast<int>(id.Get());
}

int ToInt(ed::LinkId id) {
    return static_cast<int>(id.Get());
}

void CopyStringToBuffer(const std::string& text, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return;
    }
    std::snprintf(buffer, bufferSize, "%s", text.c_str());
}

bool EditString(const char* label, std::string& value, size_t bufferSize = 256) {
    std::vector<char> buffer(bufferSize, '\0');
    CopyStringToBuffer(value, buffer.data(), buffer.size());
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool EditMultilineString(const char* label, std::string& value, const ImVec2& size, size_t bufferSize = 1024) {
    std::vector<char> buffer(bufferSize, '\0');
    CopyStringToBuffer(value, buffer.data(), buffer.size());
    if (ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), size)) {
        value = buffer.data();
        return true;
    }
    return false;
}

ImVec4 GetPinColor(const NodePin& pin) {
    if (pin.kind == NodePinKind::Flow) {
        return ImVec4(0.42f, 0.78f, 1.00f, 1.0f);
    }

    switch (pin.valueType) {
    case NodeValueType::Bool:
        return ImVec4(0.95f, 0.62f, 0.25f, 1.0f);
    case NodeValueType::Float:
    case NodeValueType::Int:
        return ImVec4(0.62f, 0.95f, 0.36f, 1.0f);
    case NodeValueType::String:
        return ImVec4(0.92f, 0.70f, 1.00f, 1.0f);
    case NodeValueType::Object:
        return ImVec4(1.00f, 0.86f, 0.35f, 1.0f);
    case NodeValueType::Effect:
        return ImVec4(1.00f, 0.46f, 0.30f, 1.0f);
    case NodeValueType::Scene:
        return ImVec4(0.55f, 0.78f, 1.00f, 1.0f);
    default:
        return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
    }
}

ImVec4 GetIssueColor(NodeIssueSeverity severity) {
    switch (severity) {
    case NodeIssueSeverity::Error:
        return ImVec4(1.00f, 0.32f, 0.28f, 1.0f);
    case NodeIssueSeverity::Warning:
        return ImVec4(1.00f, 0.78f, 0.28f, 1.0f);
    case NodeIssueSeverity::Info:
    default:
        return ImVec4(0.55f, 0.82f, 1.00f, 1.0f);
    }
}

const char* GetIssueLabel(NodeIssueSeverity severity) {
    switch (severity) {
    case NodeIssueSeverity::Error:
        return "Error";
    case NodeIssueSeverity::Warning:
        return "Warning";
    case NodeIssueSeverity::Info:
    default:
        return "Info";
    }
}

const char* GetTemplateDescription(const std::string& type) {
    const NodeTemplateDefinition* definition = NodeGraphTemplateRegistry::Instance().Find(type);
    return definition ? definition->description.c_str() : "未登録のノードです。";
}

#endif // USE_IMGUI

float ReadDurationProperty(const NodeData& node, float fallback) {
    for (const NodeProperty& property : node.properties) {
        if ((property.name == "duration" || property.name == "seconds") && property.type == NodePropertyType::Float) {
            return property.floatValue < 0.01f ? 0.01f : property.floatValue;
        }
    }
    return fallback;
}

} // namespace

NodeGraphEditorWindow::~NodeGraphEditorWindow() {
    Finalize();
}

void NodeGraphEditorWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
#ifdef USE_IMGUI
    if (!context_) {
        context_ = ed::CreateEditor();
    }
#endif
}

void NodeGraphEditorWindow::Finalize() {
#ifdef USE_IMGUI
    if (context_) {
        ed::SetCurrentEditor(nullptr);
        ed::DestroyEditor(context_);
        context_ = nullptr;
    }
#endif
    initialized_ = false;
}

void NodeGraphEditorWindow::EnsureInitialized() {
    if (initialized_) {
        return;
    }

    std::string errorMessage;
    if (!graph_.LoadFromFile(graphPath_, &errorMessage)) {
        graph_.ResetToSample();
        statusMessage_ = "サンプルのゲート突入グラフを作成しました。";
    } else {
        statusMessage_ = "ノードグラフを読み込みました。";
    }

    initialized_ = true;
    firstFrame_ = true;
}

void NodeGraphEditorWindow::DrawImGui() {
    EnsureInitialized();
#ifdef USE_IMGUI
    UpdateDryRun(ImGui::GetIO().DeltaTime);
    DrawCompactInspectorPanel();
    if (largeWindowOpen_) {
        DrawLargeWindow();
    }
#endif
}

void NodeGraphEditorWindow::DrawCompactInspectorPanel() {
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("演出ノード (Effect Sequence Graph)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("ゲート突入、フェード、エフェクトなどを順番で組み立てるためのノード編集ツールです。");
        ImGui::Spacing();
        ImGui::Text("ノード数: %d", static_cast<int>(graph_.GetNodes().size()));
        ImGui::Text("リンク数: %d", static_cast<int>(graph_.GetLinks().size()));
        if (!statusMessage_.empty()) {
            ImGui::TextWrapped("状態: %s", statusMessage_.c_str());
        }
        if (dryRunActive_) {
            ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.00f, 1.0f), "ドライラン中: %s", dryRunExecutor_.GetStateText().c_str());
        }
        if (ImGui::Button("大きなノード編集画面を開く")) {
            largeWindowOpen_ = true;
        }
    }
#endif
}

void NodeGraphEditorWindow::DrawLargeWindow() {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(1380.0f, 780.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("演出ノード - Effect Sequence Graph", &largeWindowOpen_)) {
        ImGui::End();
        return;
    }

    DrawToolbar();
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float sidePanelWidth = 360.0f;
    const float requestedCanvasWidth = available.x - sidePanelWidth - 12.0f;
    const float canvasWidth = requestedCanvasWidth < 520.0f ? 520.0f : requestedCanvasWidth;

    ImGui::BeginChild("TemplateAndCanvas", ImVec2(canvasWidth, 0.0f), true);
    DrawTemplateButtons();
    ImGui::Separator();
    DrawCanvas();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("NodeGraphSidePanel", ImVec2(0.0f, 0.0f), true);
    DrawSidePanel();
    ImGui::EndChild();

    ImGui::End();
#endif
}

void NodeGraphEditorWindow::DrawToolbar() {
#ifdef USE_IMGUI
    EditString("保存先", graphPath_, 512);
    ImGui::SameLine();
    if (ImGui::Button("保存")) {
        SaveGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("読込")) {
        LoadGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("ゲート突入サンプル")) {
        graph_.ResetToSample();
        selectedNodeId_ = 0;
        firstFrame_ = true;
        statusMessage_ = "ゲート突入サンプルを再生成しました。";
    }
    ImGui::SameLine();
    if (ImGui::Button("ドライラン")) {
        StartDryRun();
    }
    if (!statusMessage_.empty()) {
        ImGui::TextWrapped("状態: %s", statusMessage_.c_str());
    }
#endif
}

void NodeGraphEditorWindow::DrawTemplateButtons() {
#ifdef USE_IMGUI
    const NodeGraphTemplateRegistry& registry = NodeGraphTemplateRegistry::Instance();
    ImGui::TextUnformatted("ノード追加");
    ImGui::TextDisabled("用途別に整理したテンプレートから追加します。Data ノードは値ピン接続の基礎です。");

    for (NodeTemplateCategory category : registry.GetDisplayCategories()) {
        if (!ImGui::TreeNodeEx(NodeGraphTemplateRegistry::ToDisplayName(category), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }

        const std::vector<const NodeTemplateDefinition*> templates = registry.GetTemplatesByCategory(category);
        for (const NodeTemplateDefinition* definition : templates) {
            if (!definition) {
                continue;
            }
            ImGui::PushID(definition->type.c_str());
            if (ImGui::Button(definition->title.c_str(), ImVec2(170.0f, 0.0f))) {
                AddNodeFromTemplate(definition->type);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s", definition->type.c_str(), definition->description.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", definition->type.c_str());
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
#endif
}

void NodeGraphEditorWindow::DrawCanvas() {
#ifdef USE_IMGUI
    if (!context_) {
        return;
    }

    ed::SetCurrentEditor(context_);

    ImGui::TextUnformatted("グラフキャンバス");
    ImGui::TextDisabled("制御フローは水色、値ピンは型ごとに色を分けています。右クリック追加はまだ未実装です。");

    ed::Begin("EffectSequenceCanvas", ImVec2(0.0f, 430.0f));

    if (firstFrame_) {
        for (const NodeData& node : graph_.GetNodes()) {
            ed::SetNodePosition(ed::NodeId(node.id), ImVec2(node.editorX, node.editorY));
        }
        ed::NavigateToContent(0.0f);
        firstFrame_ = false;
    }

    for (NodeData& node : graph_.GetNodes()) {
        DrawNode(node);
    }

    for (const cg2::editor::NodeLink& link : graph_.GetLinks()) {
        ed::Link(ed::LinkId(link.id), ed::PinId(link.startPinId), ed::PinId(link.endPinId), ImColor(150, 210, 255), 2.0f);
    }

    HandleCreateLink();
    HandleDeleteLink();

    ed::End();

    for (NodeData& node : graph_.GetNodes()) {
        const ImVec2 position = ed::GetNodePosition(ed::NodeId(node.id));
        node.editorX = position.x;
        node.editorY = position.y;
    }

    ed::SetCurrentEditor(nullptr);
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

    ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.55f, 1.0f), "%s", node.title.c_str());
    ImGui::SameLine();
    ImGui::PushID(node.id);
    if (ImGui::SmallButton("編集")) {
        SelectNode(node.id);
    }
    ImGui::PopID();

    ImGui::TextDisabled("%s", node.type.c_str());
    const char* description = GetTemplateDescription(node.type);
    if (description && description[0] != '\0') {
        ImGui::TextWrapped("%s", description);
    }

    if (!node.note.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 260.0f);
        ImGui::TextColored(ImVec4(0.75f, 0.88f, 1.0f, 1.0f), "%s", node.note.c_str());
        ImGui::PopTextWrapPos();
    }

    if (!node.inputs.empty()) {
        ImGui::Separator();
        for (const NodePin& pin : node.inputs) {
            ed::BeginPin(ed::PinId(pin.id), ed::PinKind::Input);
            DrawPinLabel(pin);
            ed::EndPin();
        }
    }

    if (!node.outputs.empty()) {
        ImGui::Separator();
        for (const NodePin& pin : node.outputs) {
            ed::BeginPin(ed::PinId(pin.id), ed::PinKind::Output);
            DrawPinLabel(pin);
            ed::EndPin();
        }
    }

    ed::EndNode();
#endif
}

void NodeGraphEditorWindow::DrawPinLabel(const NodePin& pin) {
#ifdef USE_IMGUI
    const ImVec4 color = GetPinColor(pin);
    ImGui::TextColored(color, "●");
    ImGui::SameLine();
    ImGui::Text("%s", pin.name.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s / %s", cg2::editor::ToString(pin.kind).c_str(), cg2::editor::ToString(pin.valueType).c_str());
    }
#endif
}

void NodeGraphEditorWindow::DrawSelectedNodeInspector() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("選択ノード");
    NodeData* node = GetSelectedNode();
    if (!node) {
        ImGui::TextDisabled("ノードを選択すると、ここで名前やプロパティを編集できます。");
        return;
    }

    ImGui::Text("ID: %d", node->id);
    ImGui::TextDisabled("%s", node->type.c_str());
    EditString("表示名", node->title, 256);
    EditMultilineString("説明メモ", node->note, ImVec2(-1.0f, 72.0f), 1024);

    const NodeTemplateDefinition* definition = NodeGraphTemplateRegistry::Instance().Find(node->type);
    if (definition) {
        ImGui::Text("分類: %s", NodeGraphTemplateRegistry::ToString(definition->executionKind));
        ImGui::TextWrapped("用途: %s", definition->description.c_str());
    }

    if (!node->properties.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("プロパティ");
        for (NodeProperty& property : node->properties) {
            ImGui::PushID(property.name.c_str());
            DrawPropertyEditor(property);
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("ノード削除")) {
        const int deleteId = node->id;
        graph_.RemoveNode(deleteId);
        selectedNodeId_ = 0;
        statusMessage_ = "ノードを削除しました。";
    }
#endif
}

void NodeGraphEditorWindow::DrawValidationPanel() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("検証");
    const std::vector<NodeGraphIssue> issues = graph_.Validate();
    if (issues.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.50f, 1.0f), "問題は見つかりませんでした。");
        return;
    }

    for (const NodeGraphIssue& issue : issues) {
        ImGui::TextColored(GetIssueColor(issue.severity), "[%s] %s", GetIssueLabel(issue.severity), issue.message.c_str());
        if (issue.nodeId != 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton(("選択##issue" + std::to_string(issue.nodeId)).c_str())) {
                SelectNode(issue.nodeId);
            }
        }
    }
#endif
}

void NodeGraphEditorWindow::DrawExecutionPreviewPanel() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("実行順プレビュー");
    const std::vector<cg2::editor::NodeExecutionStep> steps = graph_.BuildExecutionPreview();
    if (steps.empty()) {
        ImGui::TextDisabled("Event から辿れる実行順がありません。");
        return;
    }

    for (const cg2::editor::NodeExecutionStep& step : steps) {
        ImGui::Text("%02d. %s", step.index, step.title.c_str());
        ImGui::TextDisabled("    %s", step.type.c_str());
    }
#endif
}

void NodeGraphEditorWindow::DrawPropertyEditor(NodeProperty& property) {
#ifdef USE_IMGUI
    const std::string label = property.displayName.empty() ? property.name : property.displayName;
    switch (property.type) {
    case NodePropertyType::Bool:
        ImGui::Checkbox(label.c_str(), &property.boolValue);
        break;
    case NodePropertyType::Int:
        ImGui::InputInt(label.c_str(), &property.intValue);
        break;
    case NodePropertyType::Float:
        ImGui::InputFloat(label.c_str(), &property.floatValue, 0.05f, 0.2f, "%.3f");
        break;
    case NodePropertyType::String:
    default:
        EditString(label.c_str(), property.stringValue, 512);
        break;
    }
#endif
}

void NodeGraphEditorWindow::HandleCreateLink() {
#ifdef USE_IMGUI
    ed::BeginCreate(ImColor(120, 210, 255), 2.0f);

    ed::PinId startPinId;
    ed::PinId endPinId;
    if (ed::QueryNewLink(&startPinId, &endPinId)) {
        int startId = ToInt(startPinId);
        int endId = ToInt(endPinId);
        NodePin* startPin = graph_.FindPin(startId);
        NodePin* endPin = graph_.FindPin(endId);

        if (startPin && endPin && startPin->isInput && !endPin->isInput) {
            std::swap(startId, endId);
            std::swap(startPin, endPin);
        }

        bool canConnect = startPin && endPin && !startPin->isInput && endPin->isInput;
        canConnect = canConnect && startPin->kind == endPin->kind;
        canConnect = canConnect && cg2::editor::IsCompatibleValueType(startPin->valueType, endPin->valueType);
        canConnect = canConnect && graph_.CountIncomingLinks(endId) == 0;

        if (canConnect) {
            if (ed::AcceptNewItem(ImColor(100, 255, 140), 2.5f)) {
                if (graph_.AddLink(startId, endId)) {
                    statusMessage_ = "リンクを作成しました。";
                } else {
                    statusMessage_ = "リンク作成に失敗しました。";
                }
            }
        } else {
            ed::RejectNewItem(ImColor(255, 80, 80), 2.0f);
        }
    }

    ed::EndCreate();
#endif
}

void NodeGraphEditorWindow::HandleDeleteLink() {
#ifdef USE_IMGUI
    ed::BeginDelete();
    ed::LinkId deletedLinkId;
    while (ed::QueryDeletedLink(&deletedLinkId)) {
        if (ed::AcceptDeletedItem()) {
            graph_.RemoveLink(ToInt(deletedLinkId));
            statusMessage_ = "リンクを削除しました。";
        }
    }
    ed::EndDelete();
#endif
}

void NodeGraphEditorWindow::AddNodeFromTemplate(const std::string& templateType) {
    const float baseX = 80.0f + static_cast<float>(graph_.GetNodes().size() % 4) * 280.0f;
    const float baseY = 120.0f + static_cast<float>(graph_.GetNodes().size() / 4) * 180.0f;

    NodeData* node = NodeGraphTemplateRegistry::Instance().CreateNode(graph_, templateType, baseX, baseY);
    if (!node) {
        statusMessage_ = "未登録のノードテンプレートです: " + templateType;
        return;
    }

    SelectNode(node->id);
#ifdef USE_IMGUI
    if (context_) {
        ed::SetCurrentEditor(context_);
        ed::SetNodePosition(ed::NodeId(node->id), ImVec2(node->editorX, node->editorY));
        ed::SetCurrentEditor(nullptr);
    }
#endif
    statusMessage_ = "ノードを追加しました: " + node->title;
}

void NodeGraphEditorWindow::StartDryRun() {
    dryRunExecutor_.ClearActionHandlers();
    dryRunExecutor_.SetLogHandler([this](const std::string& message) {
        statusMessage_ = "ログ: " + message;
    });

    const NodeGraphTemplateRegistry& registry = NodeGraphTemplateRegistry::Instance();
    for (const NodeTemplateDefinition& definition : registry.GetTemplates()) {
        if (definition.dryRunBehavior == NodeDryRunBehavior::Immediate) {
            dryRunExecutor_.RegisterActionHandler(definition.type, [title = definition.title](cg2::editor::NodeActionContext& context) {
                context.Log("実行: " + title);
                return true;
            });
        } else if (definition.dryRunBehavior == NodeDryRunBehavior::Timed) {
            dryRunExecutor_.RegisterActionHandler(definition.type, [title = definition.title](cg2::editor::NodeActionContext& context) {
                const float seconds = context.node ? ReadDurationProperty(*context.node, 0.35f) : 0.35f;
                context.Log("演出待機: " + title);
                context.RequestWait(seconds);
                return true;
            });
        }
    }

    std::string errorMessage;
    if (!dryRunExecutor_.Start(graph_, "Event.OnEnterGate", &errorMessage)) {
        statusMessage_ = "ドライラン開始失敗: " + errorMessage;
        dryRunActive_ = false;
        return;
    }

    dryRunActive_ = true;
    statusMessage_ = "ドライランを開始しました。";
}

void NodeGraphEditorWindow::UpdateDryRun(float deltaTime) {
    if (!dryRunActive_) {
        return;
    }

    dryRunExecutor_.Update(deltaTime);
    if (dryRunExecutor_.IsFinished()) {
        dryRunActive_ = false;
        statusMessage_ = dryRunExecutor_.GetStatusMessage();
    }
}

void NodeGraphEditorWindow::SaveGraph() {
    std::string errorMessage;
    if (graph_.SaveToFile(graphPath_, &errorMessage)) {
        statusMessage_ = "ノードグラフを保存しました。";
    } else {
        statusMessage_ = "保存に失敗しました: " + errorMessage;
    }
}

void NodeGraphEditorWindow::LoadGraph() {
    std::string errorMessage;
    if (graph_.LoadFromFile(graphPath_, &errorMessage)) {
        selectedNodeId_ = 0;
        firstFrame_ = true;
        statusMessage_ = "ノードグラフを読み込みました。";
    } else {
        statusMessage_ = "読み込みに失敗しました: " + errorMessage;
    }
}

void NodeGraphEditorWindow::SelectNode(int nodeId) {
    selectedNodeId_ = nodeId;
#ifdef USE_IMGUI
    if (context_) {
        ed::SetCurrentEditor(context_);
        ed::SelectNode(ed::NodeId(nodeId));
        ed::SetCurrentEditor(nullptr);
    }
#endif
}

NodeData* NodeGraphEditorWindow::GetSelectedNode() {
    return graph_.FindNode(selectedNodeId_);
}



