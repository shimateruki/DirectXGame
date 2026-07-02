#pragma once

#include "IEditable.h"
#include "NodeGraphCore.h"

#include <string>

namespace ax::NodeEditor {
struct EditorContext;
}

class DebugEditor;

class NodeGraphEditorWindow : public IEditable {
public:
    NodeGraphEditorWindow() = default;
    ~NodeGraphEditorWindow();

    void Initialize(DebugEditor* editor);
    void Finalize();
    void DrawImGui() override;
    std::string GetName() override { return "Node Graph"; }

private:
    void EnsureInitialized();
    void DrawToolbar();
    void DrawCanvas();
    void DrawSidePanel();
    void DrawNode(cg2::editor::NodeData& node);
    void DrawPinLabel(const cg2::editor::NodePin& pin);
    void DrawSelectedNodeInspector();
    void DrawValidationPanel();
    void DrawExecutionPreviewPanel();
    void DrawPropertyEditor(cg2::editor::NodeProperty& property);
    void HandleCreateLink();
    void HandleDeleteLink();
    void AddNodeFromTemplate(const std::string& templateType);
    void SaveGraph();
    void LoadGraph();
    void SelectNode(int nodeId);
    cg2::editor::NodeData* GetSelectedNode();

    DebugEditor* editor_ = nullptr;
    ax::NodeEditor::EditorContext* context_ = nullptr;
    cg2::editor::NodeGraphCore graph_;
    std::string graphPath_ = "Resources/json/nodegraph/editor_graph.json";
    std::string statusMessage_;
    int selectedNodeId_ = 0;
    bool initialized_ = false;
    bool firstFrame_ = true;
};