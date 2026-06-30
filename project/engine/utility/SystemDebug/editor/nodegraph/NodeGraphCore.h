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
    Any
};

struct NodePin {
    int id = 0;
    std::string name;
    NodePinKind kind = NodePinKind::Input;
    NodeValueType valueType = NodeValueType::Any;
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
};

struct NodeLink {
    int id = 0;
    int startPinId = 0;
    int endPinId = 0;
};

class NodeGraphCore {
public:
    void Clear();
    void ResetToSample();

    NodeData& AddNode(const std::string& type, const std::string& title, float editorX, float editorY);
    NodePin& AddInputPin(NodeData& node, const std::string& name, NodeValueType valueType);
    NodePin& AddOutputPin(NodeData& node, const std::string& name, NodeValueType valueType);

    bool CanCreateLink(int startPinId, int endPinId, std::string* reason = nullptr) const;
    NodeLink* AddLink(int startPinId, int endPinId);
    bool RemoveLink(int linkId);
    void RemoveLinksForPin(int pinId);

    NodeData* FindNode(int nodeId);
    const NodeData* FindNode(int nodeId) const;
    NodePin* FindPin(int pinId);
    const NodePin* FindPin(int pinId) const;
    NodeData* FindNodeByPin(int pinId);
    const NodeData* FindNodeByPin(int pinId) const;
    NodeLink* FindLink(int linkId);
    const NodeLink* FindLink(int linkId) const;

    const std::vector<NodeData>& GetNodes() const { return nodes_; }
    std::vector<NodeData>& GetNodes() { return nodes_; }
    const std::vector<NodeLink>& GetLinks() const { return links_; }
    std::vector<NodeLink>& GetLinks() { return links_; }

    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& json, std::string* errorMessage = nullptr);
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);

private:
    int AllocateNodeId();
    int AllocatePinId();
    int AllocateLinkId();
    void RebuildNextIds();
    bool IsPinLinkedTo(int startPinId, int endPinId) const;
    static const char* ToString(NodePinKind kind);
    static const char* ToString(NodeValueType type);
    static std::optional<NodePinKind> ParsePinKind(const std::string& value);
    static std::optional<NodeValueType> ParseValueType(const std::string& value);
    static bool IsCompatibleValueType(NodeValueType outputType, NodeValueType inputType);

    std::vector<NodeData> nodes_;
    std::vector<NodeLink> links_;
    int nextNodeId_ = 1;
    int nextPinId_ = 1001;
    int nextLinkId_ = 2001;
};

} // namespace cg2::editor