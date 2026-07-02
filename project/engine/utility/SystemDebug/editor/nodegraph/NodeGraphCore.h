#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "json.hpp"

namespace cg2::editor {

enum class NodePinKind {
    Input,
    Output
};

enum class NodeValueType {
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Effect,
    Scene,
    Any
};

enum class NodePropertyType {
    Bool,
    Int,
    Float,
    String,
    ObjectName,
    EffectName,
    SceneName
};

enum class NodeIssueSeverity {
    Info,
    Warning,
    Error
};

struct NodePin {
    int id = 0;
    std::string name;
    NodePinKind kind = NodePinKind::Input;
    NodeValueType valueType = NodeValueType::Any;
};

struct NodeProperty {
    std::string key;
    std::string label;
    NodePropertyType type = NodePropertyType::String;
    std::string value;
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
    int pinId = 0;
    std::string message;
};

struct NodeExecutionStep {
    int order = 0;
    int nodeId = 0;
    std::string title;
    std::string type;
};

class NodeGraphCore {
public:
    void Clear();
    void ResetToSample();

    NodeData& AddNode(const std::string& type, const std::string& title, float editorX, float editorY);
    NodePin& AddInputPin(NodeData& node, const std::string& name, NodeValueType valueType);
    NodePin& AddOutputPin(NodeData& node, const std::string& name, NodeValueType valueType);
    NodeProperty& AddProperty(NodeData& node, const std::string& key, const std::string& label, NodePropertyType type, const std::string& defaultValue);

    bool CanCreateLink(int startPinId, int endPinId, std::string* reason = nullptr) const;
    NodeLink* AddLink(int startPinId, int endPinId);
    bool RemoveLink(int linkId);
    void RemoveLinksForPin(int pinId);
    bool RemoveNode(int nodeId);

    NodeData* FindNode(int nodeId);
    const NodeData* FindNode(int nodeId) const;
    NodePin* FindPin(int pinId);
    const NodePin* FindPin(int pinId) const;
    NodeProperty* FindProperty(NodeData& node, const std::string& key);
    const NodeProperty* FindProperty(const NodeData& node, const std::string& key) const;
    NodeData* FindNodeByPin(int pinId);
    const NodeData* FindNodeByPin(int pinId) const;
    NodeLink* FindLink(int linkId);
    const NodeLink* FindLink(int linkId) const;

    std::vector<NodeGraphIssue> Validate() const;
    std::vector<NodeExecutionStep> BuildExecutionPreview() const;

    const std::vector<NodeData>& GetNodes() const { return nodes_; }
    std::vector<NodeData>& GetNodes() { return nodes_; }
    const std::vector<NodeLink>& GetLinks() const { return links_; }
    std::vector<NodeLink>& GetLinks() { return links_; }

    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& json, std::string* errorMessage = nullptr);
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);

    static const char* ToString(NodePinKind kind);
    static const char* ToString(NodeValueType type);
    static const char* ToString(NodePropertyType type);
    static const char* ToString(NodeIssueSeverity severity);

private:
    int AllocateNodeId();
    int AllocatePinId();
    int AllocateLinkId();
    void RebuildNextIds();
    bool IsPinLinkedTo(int startPinId, int endPinId) const;
    int CountIncomingLinks(int pinId) const;
    int CountOutgoingLinks(int pinId) const;
    std::vector<int> GetFlowInputPinIds(const NodeData& node) const;
    std::vector<int> GetFlowOutputPinIds(const NodeData& node) const;
    static std::optional<NodePinKind> ParsePinKind(const std::string& value);
    static std::optional<NodeValueType> ParseValueType(const std::string& value);
    static std::optional<NodePropertyType> ParsePropertyType(const std::string& value);
    static bool IsCompatibleValueType(NodeValueType outputType, NodeValueType inputType);

    std::vector<NodeData> nodes_;
    std::vector<NodeLink> links_;
    int nextNodeId_ = 1;
    int nextPinId_ = 1001;
    int nextLinkId_ = 2001;
};

} // namespace cg2::editor