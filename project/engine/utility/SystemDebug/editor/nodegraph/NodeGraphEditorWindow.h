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
    void DrawNode(cg2::editor::NodeData& node);
    void DrawPinLabel(const cg2::editor::NodePin& pin);
    void HandleCreateLink();
    void HandleDeleteLink();
    void AddNodeFromTemplate(int templateIndex);
    void SaveGraph();
    void LoadGraph();

    DebugEditor* editor_ = nullptr;
    ax::NodeEditor::EditorContext* context_ = nullptr;
    cg2::editor::NodeGraphCore graph_;
    std::string graphPath_ = "Resources/json/nodegraph/editor_graph.json";
    std::string statusMessage_;
    bool initialized_ = false;
    bool firstFrame_ = true;
};