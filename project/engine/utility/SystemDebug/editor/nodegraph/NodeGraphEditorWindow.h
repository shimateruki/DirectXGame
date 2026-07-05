#pragma once

#include "IEditable.h"
#include "NodeGraphCore.h"
#include "NodeGraphExecutor.h"

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
    std::string GetName() override { return "演出ノード (Effect Sequence Graph)"; }

private:
    // 初期化と大きな編集ウィンドウの描画。
    void EnsureInitialized();
    void DrawCompactInspectorPanel();
    void DrawLargeWindow();
    void DrawToolbar();
    void DrawTemplateButtons();

    // ノードキャンバス本体。
    void DrawCanvas();
    void DrawSidePanel();
    void DrawNode(cg2::editor::NodeData& node);
    void DrawPinLabel(const cg2::editor::NodePin& pin);

    // 右側の情報パネル。
    void DrawSelectedNodeInspector();
    void DrawValidationPanel();
    void DrawExecutionPreviewPanel();
    void DrawPropertyEditor(cg2::editor::NodeProperty& property);

    // Node Editor 上のリンク操作。
    void HandleCreateLink();
    void HandleDeleteLink();

    // ノード生成とドライラン。
    void AddNodeFromTemplate(const std::string& templateType);
    void StartDryRun();
    void UpdateDryRun(float deltaTime);

    // 保存、読み込み、選択状態。
    void SaveGraph();
    void LoadGraph();
    void SelectNode(int nodeId);
    cg2::editor::NodeData* GetSelectedNode();

    DebugEditor* editor_ = nullptr;
    ax::NodeEditor::EditorContext* context_ = nullptr;

    // ノード定義、保存、検証、実行順プレビューを管理するデータ本体。
    cg2::editor::NodeGraphCore graph_;

    // Editor 上で実ゲームへ影響させず、実行順と待ち時間だけ確認するための Executor。
    cg2::editor::NodeGraphExecutor dryRunExecutor_;

    std::string graphPath_ = "Resources/json/nodegraph/gate_entry_effect_sequence.json";
    std::string statusMessage_;
    int selectedNodeId_ = 0;

    // ImGui / node-editor の表示状態。
    bool initialized_ = false;
    bool firstFrame_ = true;
    bool largeWindowOpen_ = true;
    bool dryRunActive_ = false;
};
