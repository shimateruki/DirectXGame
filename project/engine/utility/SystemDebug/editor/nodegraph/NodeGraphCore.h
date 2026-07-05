#pragma once

#include "json.hpp"

#include <string>
#include <vector>

namespace cg2::editor {

// ノードのピンが制御フローを流すのか、値を受け渡すのかを表します。
enum class NodePinKind {
    Flow,
    Value,
};

// 値ピンが扱うデータ型です。Flow は制御ピン用の特別な型として扱います。
enum class NodeValueType {
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Effect,
    Scene,
    Unknown,
};

// Inspector に表示するノードプロパティの編集型です。
enum class NodePropertyType {
    Bool,
    Int,
    Float,
    String,
};

// 検証結果の重要度です。
enum class NodeIssueSeverity {
    Info,
    Warning,
    Error,
};

struct NodePin {
    int id = 0;
    std::string name;
    NodePinKind kind = NodePinKind::Flow;
    NodeValueType valueType = NodeValueType::Flow;
    bool isInput = false;
};

struct NodeProperty {
    std::string name;
    std::string displayName;
    NodePropertyType type = NodePropertyType::String;
    bool boolValue = false;
    int intValue = 0;
    float floatValue = 0.0f;
    std::string stringValue;
};

struct NodeData {
    int id = 0;
    std::string type;
    std::string title;
    std::string note;
    float editorX = 0.0f;
    float editorY = 0.0f;
    std::vector<NodePin> inputs;
    std::vector<NodePin> outputs;
    std::vector<NodeProperty> properties;
};

struct NodeLink {
    int id = 0;
    int startPinId = 0;
    int endPinId = 0;
};

struct NodeGraphIssue {
    NodeIssueSeverity severity = NodeIssueSeverity::Info;
    int nodeId = 0;
    int linkId = 0;
    std::string message;
};

struct NodeExecutionStep {
    int index = 0;
    int nodeId = 0;
    std::string type;
    std::string title;
    std::string note;
};

// Effect Sequence Graph の実体です。
// UI 表示だけでなく、保存、検証、ドライラン用の実行順作成まで担当します。
class NodeGraphCore {
public:
    void Clear();
    void ResetToSample();

    NodeData& AddNode(const std::string& type, const std::string& title, float editorX, float editorY);
    NodePin& AddInputPin(NodeData& node, const std::string& name, NodePinKind kind, NodeValueType valueType = NodeValueType::Flow);
    NodePin& AddOutputPin(NodeData& node, const std::string& name, NodePinKind kind, NodeValueType valueType = NodeValueType::Flow);
    NodeProperty& AddProperty(NodeData& node, const std::string& name, const std::string& displayName, NodePropertyType type);
    NodeLink* AddLink(int startPinId, int endPinId);

    bool RemoveNode(int nodeId);
    bool RemoveLink(int linkId);

    NodeData* FindNode(int nodeId);
    const NodeData* FindNode(int nodeId) const;
    NodePin* FindPin(int pinId);
    const NodePin* FindPin(int pinId) const;
    NodeData* FindNodeByPin(int pinId);
    const NodeData* FindNodeByPin(int pinId) const;
    NodeLink* FindLink(int linkId);
    const NodeLink* FindLink(int linkId) const;
    const NodeLink* FindIncomingLink(int inputPinId) const;
    const NodeData* FindOutputNodeConnectedTo(int inputPinId) const;

    int CountIncomingLinks(int inputPinId) const;
    int CountOutgoingLinks(int outputPinId) const;

    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);

    std::vector<NodeGraphIssue> Validate() const;
    std::vector<NodeExecutionStep> BuildExecutionPreview() const;

    std::vector<NodeData>& GetNodes() { return nodes_; }
    const std::vector<NodeData>& GetNodes() const { return nodes_; }
    std::vector<NodeLink>& GetLinks() { return links_; }
    const std::vector<NodeLink>& GetLinks() const { return links_; }

private:
    int AllocateNodeId();
    int AllocatePinId();
    int AllocateLinkId();

    std::vector<NodeData> nodes_;
    std::vector<NodeLink> links_;
    int nextNodeId_ = 1;
    int nextPinId_ = 1001;
    int nextLinkId_ = 2001;
};

std::string ToString(NodePinKind value);
std::string ToString(NodeValueType value);
std::string ToString(NodePropertyType value);
std::string ToString(NodeIssueSeverity value);
NodePinKind ParseNodePinKind(const std::string& value);
NodeValueType ParseNodeValueType(const std::string& value);
NodePropertyType ParseNodePropertyType(const std::string& value);
bool IsCompatibleValueType(NodeValueType outputType, NodeValueType inputType);

} // namespace cg2::editor
