#pragma once

#include "NodeGraphCore.h"

#include <string>
#include <vector>

namespace cg2::editor {

// テンプレート一覧の表示カテゴリです。
enum class NodeTemplateCategory {
    Event,
    Flow,
    Action,
    Data,
    Utility,
    Debug,
};

// 実行器がノードをどう扱うかを示す分類です。
enum class NodeExecutionKind {
    Event,
    FlowControl,
    Action,
    Data,
    Utility,
    Debug,
};

// ドライラン時に外部実装なしで完了扱いにするかを決めます。
enum class NodeDryRunBehavior {
    None,
    BuiltIn,
    Immediate,
    Timed,
};

struct NodePinDefinition {
    std::string name;
    NodePinKind kind = NodePinKind::Flow;
    NodeValueType valueType = NodeValueType::Flow;
    bool required = false;
};

struct NodePropertyDefinition {
    std::string name;
    std::string displayName;
    NodePropertyType type = NodePropertyType::String;
    bool required = false;
    bool boolValue = false;
    int intValue = 0;
    float floatValue = 0.0f;
    std::string stringValue;
};

struct NodeTemplateDefinition {
    std::string type;
    std::string title;
    std::string description;
    std::string defaultNote;
    NodeTemplateCategory category = NodeTemplateCategory::Action;
    NodeExecutionKind executionKind = NodeExecutionKind::Action;
    NodeDryRunBehavior dryRunBehavior = NodeDryRunBehavior::None;
    std::vector<NodePinDefinition> inputs;
    std::vector<NodePinDefinition> outputs;
    std::vector<NodePropertyDefinition> properties;
};

// ノード定義を一か所に集約する registry です。
// UI、サンプル生成、検証、ドライランが同じ定義を参照することで、ハードコードの分散を防ぎます。
class NodeGraphTemplateRegistry {
public:
    static const NodeGraphTemplateRegistry& Instance();

    const NodeTemplateDefinition* Find(const std::string& type) const;
    const std::vector<NodeTemplateDefinition>& GetTemplates() const { return templates_; }
    std::vector<const NodeTemplateDefinition*> GetTemplatesByCategory(NodeTemplateCategory category) const;
    std::vector<NodeTemplateCategory> GetDisplayCategories() const;

    NodeData* CreateNode(NodeGraphCore& graph, const std::string& type, float editorX, float editorY) const;

    bool IsEventNode(const std::string& type) const;
    bool IsCommentNode(const std::string& type) const;
    bool IsDataNode(const std::string& type) const;
    NodeDryRunBehavior GetDryRunBehavior(const std::string& type) const;

    static const char* ToDisplayName(NodeTemplateCategory category);
    static const char* ToString(NodeExecutionKind kind);

private:
    NodeGraphTemplateRegistry();

    std::vector<NodeTemplateDefinition> templates_;
};

} // namespace cg2::editor
