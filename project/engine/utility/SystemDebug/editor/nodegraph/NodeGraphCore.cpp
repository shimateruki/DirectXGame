#include "NodeGraphCore.h"

#include "NodeGraphTemplateRegistry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace cg2::editor {
namespace {

using Json = nlohmann::json;

const NodePin* FindPinInNode(const NodeData& node, const std::string& name, bool isInput) {
    const std::vector<NodePin>& pins = isInput ? node.inputs : node.outputs;
    const auto it = std::find_if(pins.begin(), pins.end(), [&](const NodePin& pin) {
        return pin.name == name;
    });
    return it != pins.end() ? &(*it) : nullptr;
}

const NodeProperty* FindPropertyInNode(const NodeData& node, const std::string& name) {
    const auto it = std::find_if(node.properties.begin(), node.properties.end(), [&](const NodeProperty& property) {
        return property.name == name;
    });
    return it != node.properties.end() ? &(*it) : nullptr;
}

int FindFirstFlowInputId(const NodeData& node) {
    const auto it = std::find_if(node.inputs.begin(), node.inputs.end(), [](const NodePin& pin) {
        return pin.kind == NodePinKind::Flow;
    });
    return it != node.inputs.end() ? it->id : 0;
}

int FindFirstFlowOutputId(const NodeData& node) {
    const auto it = std::find_if(node.outputs.begin(), node.outputs.end(), [](const NodePin& pin) {
        return pin.kind == NodePinKind::Flow;
    });
    return it != node.outputs.end() ? it->id : 0;
}

void LinkFlow(NodeGraphCore& graph, const NodeData* from, const NodeData* to) {
    if (!from || !to) {
        return;
    }
    const int outputPinId = FindFirstFlowOutputId(*from);
    const int inputPinId = FindFirstFlowInputId(*to);
    if (outputPinId != 0 && inputPinId != 0) {
        graph.AddLink(outputPinId, inputPinId);
    }
}

Json SerializePin(const NodePin& pin) {
    Json json;
    json["id"] = pin.id;
    json["name"] = pin.name;
    json["kind"] = ToString(pin.kind);
    json["valueType"] = ToString(pin.valueType);
    json["isInput"] = pin.isInput;
    return json;
}

Json SerializeProperty(const NodeProperty& property) {
    Json json;
    json["name"] = property.name;
    json["displayName"] = property.displayName;
    json["type"] = ToString(property.type);
    json["boolValue"] = property.boolValue;
    json["intValue"] = property.intValue;
    json["floatValue"] = property.floatValue;
    json["stringValue"] = property.stringValue;
    return json;
}

NodePin DeserializePin(const Json& json) {
    NodePin pin;
    pin.id = json.value("id", 0);
    pin.name = json.value("name", "");
    pin.kind = ParseNodePinKind(json.value("kind", "Flow"));
    pin.valueType = ParseNodeValueType(json.value("valueType", "Flow"));
    pin.isInput = json.value("isInput", false);
    return pin;
}

NodeProperty DeserializeProperty(const Json& json) {
    NodeProperty property;
    property.name = json.value("name", "");
    property.displayName = json.value("displayName", property.name);
    property.type = ParseNodePropertyType(json.value("type", "String"));
    property.boolValue = json.value("boolValue", false);
    property.intValue = json.value("intValue", 0);
    property.floatValue = json.value("floatValue", 0.0f);
    property.stringValue = json.value("stringValue", "");
    return property;
}

bool IsPropertyEmpty(const NodeProperty& property) {
    switch (property.type) {
    case NodePropertyType::String:
        return property.stringValue.empty();
    default:
        return false;
    }
}

void AddIssue(std::vector<NodeGraphIssue>& issues, NodeIssueSeverity severity, int nodeId, int linkId, const std::string& message) {
    NodeGraphIssue issue;
    issue.severity = severity;
    issue.nodeId = nodeId;
    issue.linkId = linkId;
    issue.message = message;
    issues.push_back(std::move(issue));
}

} // namespace

void NodeGraphCore::Clear() {
    nodes_.clear();
    links_.clear();
    nextNodeId_ = 1;
    nextPinId_ = 1001;
    nextLinkId_ = 2001;
}

void NodeGraphCore::ResetToSample() {
    Clear();

    const NodeGraphTemplateRegistry& registry = NodeGraphTemplateRegistry::Instance();
    const NodeData* start = registry.CreateNode(*this, "Event.OnEnterGate", 40.0f, 80.0f);
    const NodeData* lock = registry.CreateNode(*this, "Action.LockPlayerControl", 360.0f, 80.0f);
    const NodeData* camera = registry.CreateNode(*this, "Action.CameraGateEntry", 680.0f, 80.0f);
    const NodeData* align = registry.CreateNode(*this, "Action.AlignToGateEntry", 1000.0f, 80.0f);
    const NodeData* effect = registry.CreateNode(*this, "Action.PlayEffect", 1320.0f, 80.0f);
    const NodeData* move = registry.CreateNode(*this, "Action.MoveToGateInside", 1640.0f, 80.0f);
    const NodeData* dissolve = registry.CreateNode(*this, "Action.DissolvePlayer", 1960.0f, 80.0f);
    const NodeData* wait = registry.CreateNode(*this, "Flow.Wait", 2280.0f, 80.0f);
    const NodeData* fade = registry.CreateNode(*this, "Action.Fade", 2600.0f, 80.0f);
    const NodeData* changeScene = registry.CreateNode(*this, "Action.ChangeScene", 2920.0f, 80.0f);

    LinkFlow(*this, start, lock);
    LinkFlow(*this, lock, camera);
    LinkFlow(*this, camera, align);
    LinkFlow(*this, align, effect);
    LinkFlow(*this, effect, move);
    LinkFlow(*this, move, dissolve);
    LinkFlow(*this, dissolve, wait);
    LinkFlow(*this, wait, fade);
    LinkFlow(*this, fade, changeScene);
}

NodeData& NodeGraphCore::AddNode(const std::string& type, const std::string& title, float editorX, float editorY) {
    NodeData node;
    node.id = AllocateNodeId();
    node.type = type;
    node.title = title.empty() ? type : title;
    node.editorX = editorX;
    node.editorY = editorY;
    nodes_.push_back(std::move(node));
    return nodes_.back();
}

NodePin& NodeGraphCore::AddInputPin(NodeData& node, const std::string& name, NodePinKind kind, NodeValueType valueType) {
    NodePin pin;
    pin.id = AllocatePinId();
    pin.name = name;
    pin.kind = kind;
    pin.valueType = valueType;
    pin.isInput = true;
    node.inputs.push_back(std::move(pin));
    return node.inputs.back();
}

NodePin& NodeGraphCore::AddOutputPin(NodeData& node, const std::string& name, NodePinKind kind, NodeValueType valueType) {
    NodePin pin;
    pin.id = AllocatePinId();
    pin.name = name;
    pin.kind = kind;
    pin.valueType = valueType;
    pin.isInput = false;
    node.outputs.push_back(std::move(pin));
    return node.outputs.back();
}

NodeProperty& NodeGraphCore::AddProperty(NodeData& node, const std::string& name, const std::string& displayName, NodePropertyType type) {
    NodeProperty property;
    property.name = name;
    property.displayName = displayName.empty() ? name : displayName;
    property.type = type;
    node.properties.push_back(std::move(property));
    return node.properties.back();
}

NodeLink* NodeGraphCore::AddLink(int startPinId, int endPinId) {
    const NodePin* startPin = FindPin(startPinId);
    const NodePin* endPin = FindPin(endPinId);
    if (!startPin || !endPin || startPin->isInput || !endPin->isInput) {
        return nullptr;
    }
    if (startPin->kind != endPin->kind || !IsCompatibleValueType(startPin->valueType, endPin->valueType)) {
        return nullptr;
    }
    if (CountIncomingLinks(endPinId) > 0) {
        return nullptr;
    }

    NodeLink link;
    link.id = AllocateLinkId();
    link.startPinId = startPinId;
    link.endPinId = endPinId;
    links_.push_back(std::move(link));
    return &links_.back();
}

bool NodeGraphCore::RemoveNode(int nodeId) {
    const auto nodeIt = std::find_if(nodes_.begin(), nodes_.end(), [&](const NodeData& node) {
        return node.id == nodeId;
    });
    if (nodeIt == nodes_.end()) {
        return false;
    }

    std::set<int> pinIds;
    for (const NodePin& pin : nodeIt->inputs) {
        pinIds.insert(pin.id);
    }
    for (const NodePin& pin : nodeIt->outputs) {
        pinIds.insert(pin.id);
    }

    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const NodeLink& link) {
        return pinIds.count(link.startPinId) > 0 || pinIds.count(link.endPinId) > 0;
    }), links_.end());

    nodes_.erase(nodeIt);
    return true;
}

bool NodeGraphCore::RemoveLink(int linkId) {
    const auto it = std::remove_if(links_.begin(), links_.end(), [&](const NodeLink& link) {
        return link.id == linkId;
    });
    if (it == links_.end()) {
        return false;
    }
    links_.erase(it, links_.end());
    return true;
}

NodeData* NodeGraphCore::FindNode(int nodeId) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const NodeData& node) {
        return node.id == nodeId;
    });
    return it != nodes_.end() ? &(*it) : nullptr;
}

const NodeData* NodeGraphCore::FindNode(int nodeId) const {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const NodeData& node) {
        return node.id == nodeId;
    });
    return it != nodes_.end() ? &(*it) : nullptr;
}

NodePin* NodeGraphCore::FindPin(int pinId) {
    for (NodeData& node : nodes_) {
        for (NodePin& pin : node.inputs) {
            if (pin.id == pinId) {
                return &pin;
            }
        }
        for (NodePin& pin : node.outputs) {
            if (pin.id == pinId) {
                return &pin;
            }
        }
    }
    return nullptr;
}

const NodePin* NodeGraphCore::FindPin(int pinId) const {
    for (const NodeData& node : nodes_) {
        for (const NodePin& pin : node.inputs) {
            if (pin.id == pinId) {
                return &pin;
            }
        }
        for (const NodePin& pin : node.outputs) {
            if (pin.id == pinId) {
                return &pin;
            }
        }
    }
    return nullptr;
}

NodeData* NodeGraphCore::FindNodeByPin(int pinId) {
    for (NodeData& node : nodes_) {
        const auto inputIt = std::find_if(node.inputs.begin(), node.inputs.end(), [&](const NodePin& pin) { return pin.id == pinId; });
        const auto outputIt = std::find_if(node.outputs.begin(), node.outputs.end(), [&](const NodePin& pin) { return pin.id == pinId; });
        if (inputIt != node.inputs.end() || outputIt != node.outputs.end()) {
            return &node;
        }
    }
    return nullptr;
}

const NodeData* NodeGraphCore::FindNodeByPin(int pinId) const {
    for (const NodeData& node : nodes_) {
        const auto inputIt = std::find_if(node.inputs.begin(), node.inputs.end(), [&](const NodePin& pin) { return pin.id == pinId; });
        const auto outputIt = std::find_if(node.outputs.begin(), node.outputs.end(), [&](const NodePin& pin) { return pin.id == pinId; });
        if (inputIt != node.inputs.end() || outputIt != node.outputs.end()) {
            return &node;
        }
    }
    return nullptr;
}

NodeLink* NodeGraphCore::FindLink(int linkId) {
    const auto it = std::find_if(links_.begin(), links_.end(), [&](const NodeLink& link) {
        return link.id == linkId;
    });
    return it != links_.end() ? &(*it) : nullptr;
}

const NodeLink* NodeGraphCore::FindLink(int linkId) const {
    const auto it = std::find_if(links_.begin(), links_.end(), [&](const NodeLink& link) {
        return link.id == linkId;
    });
    return it != links_.end() ? &(*it) : nullptr;
}

const NodeLink* NodeGraphCore::FindIncomingLink(int inputPinId) const {
    const auto it = std::find_if(links_.begin(), links_.end(), [&](const NodeLink& link) {
        return link.endPinId == inputPinId;
    });
    return it != links_.end() ? &(*it) : nullptr;
}

const NodeData* NodeGraphCore::FindOutputNodeConnectedTo(int inputPinId) const {
    const NodeLink* link = FindIncomingLink(inputPinId);
    return link ? FindNodeByPin(link->startPinId) : nullptr;
}

int NodeGraphCore::CountIncomingLinks(int inputPinId) const {
    return static_cast<int>(std::count_if(links_.begin(), links_.end(), [&](const NodeLink& link) {
        return link.endPinId == inputPinId;
    }));
}

int NodeGraphCore::CountOutgoingLinks(int outputPinId) const {
    return static_cast<int>(std::count_if(links_.begin(), links_.end(), [&](const NodeLink& link) {
        return link.startPinId == outputPinId;
    }));
}

bool NodeGraphCore::SaveToFile(const std::string& path, std::string* errorMessage) const {
    try {
        const std::filesystem::path filePath(path);
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }

        Json root;
        root["version"] = 2;
        root["nodes"] = Json::array();
        root["links"] = Json::array();

        for (const NodeData& node : nodes_) {
            Json nodeJson;
            nodeJson["id"] = node.id;
            nodeJson["type"] = node.type;
            nodeJson["title"] = node.title;
            nodeJson["note"] = node.note;
            nodeJson["editorX"] = node.editorX;
            nodeJson["editorY"] = node.editorY;
            nodeJson["inputs"] = Json::array();
            nodeJson["outputs"] = Json::array();
            nodeJson["properties"] = Json::array();

            for (const NodePin& pin : node.inputs) {
                nodeJson["inputs"].push_back(SerializePin(pin));
            }
            for (const NodePin& pin : node.outputs) {
                nodeJson["outputs"].push_back(SerializePin(pin));
            }
            for (const NodeProperty& property : node.properties) {
                nodeJson["properties"].push_back(SerializeProperty(property));
            }
            root["nodes"].push_back(std::move(nodeJson));
        }

        for (const NodeLink& link : links_) {
            Json linkJson;
            linkJson["id"] = link.id;
            linkJson["startPinId"] = link.startPinId;
            linkJson["endPinId"] = link.endPinId;
            root["links"].push_back(std::move(linkJson));
        }

        std::ofstream output(path, std::ios::binary);
        if (!output) {
            if (errorMessage) {
                *errorMessage = "ノードグラフを書き込めませんでした: " + path;
            }
            return false;
        }
        output << root.dump(4);
        return true;
    } catch (const std::exception& e) {
        if (errorMessage) {
            *errorMessage = e.what();
        }
        return false;
    }
}

bool NodeGraphCore::LoadFromFile(const std::string& path, std::string* errorMessage) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            if (errorMessage) {
                *errorMessage = "ノードグラフを読み込めませんでした: " + path;
            }
            return false;
        }

        Json root;
        input >> root;
        Clear();

        int maxNodeId = 0;
        int maxPinId = 1000;
        int maxLinkId = 2000;

        for (const Json& nodeJson : root.value("nodes", Json::array())) {
            NodeData node;
            node.id = nodeJson.value("id", 0);
            node.type = nodeJson.value("type", "");
            node.title = nodeJson.value("title", node.type);
            node.note = nodeJson.value("note", "");
            node.editorX = nodeJson.value("editorX", 0.0f);
            node.editorY = nodeJson.value("editorY", 0.0f);

            for (const Json& pinJson : nodeJson.value("inputs", Json::array())) {
                NodePin pin = DeserializePin(pinJson);
                pin.isInput = true;
                maxPinId = std::max(maxPinId, pin.id);
                node.inputs.push_back(std::move(pin));
            }
            for (const Json& pinJson : nodeJson.value("outputs", Json::array())) {
                NodePin pin = DeserializePin(pinJson);
                pin.isInput = false;
                maxPinId = std::max(maxPinId, pin.id);
                node.outputs.push_back(std::move(pin));
            }
            for (const Json& propertyJson : nodeJson.value("properties", Json::array())) {
                node.properties.push_back(DeserializeProperty(propertyJson));
            }

            maxNodeId = std::max(maxNodeId, node.id);
            nodes_.push_back(std::move(node));
        }

        for (const Json& linkJson : root.value("links", Json::array())) {
            NodeLink link;
            link.id = linkJson.value("id", 0);
            link.startPinId = linkJson.value("startPinId", 0);
            link.endPinId = linkJson.value("endPinId", 0);
            maxLinkId = std::max(maxLinkId, link.id);
            links_.push_back(std::move(link));
        }

        nextNodeId_ = maxNodeId + 1;
        nextPinId_ = maxPinId + 1;
        nextLinkId_ = maxLinkId + 1;
        return true;
    } catch (const std::exception& e) {
        Clear();
        if (errorMessage) {
            *errorMessage = e.what();
        }
        return false;
    }
}

std::vector<NodeGraphIssue> NodeGraphCore::Validate() const {
    std::vector<NodeGraphIssue> issues;
    const NodeGraphTemplateRegistry& registry = NodeGraphTemplateRegistry::Instance();

    if (nodes_.empty()) {
        AddIssue(issues, NodeIssueSeverity::Warning, 0, 0, "ノードがありません。まず Event ノードから作成してください。");
        return issues;
    }

    int eventNodeCount = 0;
    std::set<int> flowReachableNodes;
    std::queue<int> queue;

    for (const NodeData& node : nodes_) {
        if (registry.IsEventNode(node.type)) {
            ++eventNodeCount;
            flowReachableNodes.insert(node.id);
            queue.push(node.id);
        }
    }

    while (!queue.empty()) {
        const NodeData* node = FindNode(queue.front());
        queue.pop();
        if (!node) {
            continue;
        }
        for (const NodePin& output : node->outputs) {
            if (output.kind != NodePinKind::Flow) {
                continue;
            }
            for (const NodeLink& link : links_) {
                if (link.startPinId != output.id) {
                    continue;
                }
                const NodeData* nextNode = FindNodeByPin(link.endPinId);
                if (nextNode && flowReachableNodes.insert(nextNode->id).second) {
                    queue.push(nextNode->id);
                }
            }
        }
    }

    if (eventNodeCount == 0) {
        AddIssue(issues, NodeIssueSeverity::Warning, 0, 0, "Event ノードがありません。実行開始地点がないため、演出順を確定できません。");
    } else if (eventNodeCount > 1) {
        AddIssue(issues, NodeIssueSeverity::Info, 0, 0, "Event ノードが複数あります。用途別に入口を分ける場合は問題ありません。");
    }

    for (const NodeData& node : nodes_) {
        const NodeTemplateDefinition* definition = registry.Find(node.type);
        if (!definition) {
            AddIssue(issues, NodeIssueSeverity::Warning, node.id, 0, "未登録のノード種類です: " + node.type);
            continue;
        }

        if (node.title.empty()) {
            AddIssue(issues, NodeIssueSeverity::Warning, node.id, 0, "ノード名が空です。Inspector で表示名を設定してください。");
        }

        for (const NodePinDefinition& pinDefinition : definition->inputs) {
            const NodePin* pin = FindPinInNode(node, pinDefinition.name, true);
            if (!pin) {
                AddIssue(issues, NodeIssueSeverity::Error, node.id, 0, "テンプレートに必要な入力ピンがありません: " + pinDefinition.name);
                continue;
            }
            if (pin->kind != pinDefinition.kind || !IsCompatibleValueType(pinDefinition.valueType, pin->valueType)) {
                AddIssue(issues, NodeIssueSeverity::Error, node.id, 0, "入力ピンの型がテンプレートと一致しません: " + pinDefinition.name);
            }
            if (pinDefinition.required && CountIncomingLinks(pin->id) == 0) {
                const NodeIssueSeverity severity = pin->kind == NodePinKind::Flow ? NodeIssueSeverity::Warning : NodeIssueSeverity::Info;
                AddIssue(issues, severity, node.id, 0, "必須入力ピンが未接続です: " + pinDefinition.name);
            }
        }

        for (const NodePinDefinition& pinDefinition : definition->outputs) {
            const NodePin* pin = FindPinInNode(node, pinDefinition.name, false);
            if (!pin) {
                AddIssue(issues, NodeIssueSeverity::Error, node.id, 0, "テンプレートに必要な出力ピンがありません: " + pinDefinition.name);
                continue;
            }
            if (pin->kind != pinDefinition.kind || !IsCompatibleValueType(pin->valueType, pinDefinition.valueType)) {
                AddIssue(issues, NodeIssueSeverity::Error, node.id, 0, "出力ピンの型がテンプレートと一致しません: " + pinDefinition.name);
            }
        }

        for (const NodePropertyDefinition& propertyDefinition : definition->properties) {
            const NodeProperty* property = FindPropertyInNode(node, propertyDefinition.name);
            if (!property) {
                AddIssue(issues, NodeIssueSeverity::Warning, node.id, 0, "テンプレートに必要なプロパティがありません: " + propertyDefinition.displayName);
                continue;
            }
            if (property->type != propertyDefinition.type) {
                AddIssue(issues, NodeIssueSeverity::Error, node.id, 0, "プロパティの型がテンプレートと一致しません: " + propertyDefinition.displayName);
            }
            if (propertyDefinition.required && IsPropertyEmpty(*property)) {
                AddIssue(issues, NodeIssueSeverity::Warning, node.id, 0, "必須プロパティが未設定です: " + propertyDefinition.displayName);
            }
        }

        const bool needsFlowReachability =
            !registry.IsEventNode(node.type) &&
            !registry.IsCommentNode(node.type) &&
            !registry.IsDataNode(node.type);
        if (needsFlowReachability && flowReachableNodes.count(node.id) == 0) {
            AddIssue(issues, NodeIssueSeverity::Warning, node.id, 0, "Event ノードから制御フローで到達できません。");
        }

        if (registry.IsDataNode(node.type)) {
            bool hasOutgoingValueLink = false;
            for (const NodePin& output : node.outputs) {
                if (output.kind == NodePinKind::Value && CountOutgoingLinks(output.id) > 0) {
                    hasOutgoingValueLink = true;
                    break;
                }
            }
            if (!hasOutgoingValueLink) {
                AddIssue(issues, NodeIssueSeverity::Info, node.id, 0, "Data ノードがまだどこにも接続されていません。");
            }
        }
    }

    std::unordered_map<int, int> incomingCounts;
    for (const NodeLink& link : links_) {
        const NodePin* startPin = FindPin(link.startPinId);
        const NodePin* endPin = FindPin(link.endPinId);
        if (!startPin || !endPin) {
            AddIssue(issues, NodeIssueSeverity::Error, 0, link.id, "存在しないピンに接続しているリンクがあります。");
            continue;
        }
        if (startPin->isInput || !endPin->isInput) {
            AddIssue(issues, NodeIssueSeverity::Error, 0, link.id, "リンクの向きが不正です。出力ピンから入力ピンへ接続してください。");
        }
        if (startPin->kind != endPin->kind || !IsCompatibleValueType(startPin->valueType, endPin->valueType)) {
            AddIssue(issues, NodeIssueSeverity::Error, 0, link.id, "リンク先のピン型が一致していません。");
        }
        incomingCounts[link.endPinId]++;
    }

    for (const auto& [pinId, count] : incomingCounts) {
        if (count > 1) {
            AddIssue(issues, NodeIssueSeverity::Error, 0, 0, "同じ入力ピンへ複数のリンクが接続されています。入力ピンID: " + std::to_string(pinId));
        }
    }

    return issues;
}

std::vector<NodeExecutionStep> NodeGraphCore::BuildExecutionPreview() const {
    std::vector<NodeExecutionStep> steps;
    const NodeGraphTemplateRegistry& registry = NodeGraphTemplateRegistry::Instance();
    std::set<int> visited;
    std::queue<int> queue;

    for (const NodeData& node : nodes_) {
        if (registry.IsEventNode(node.type)) {
            queue.push(node.id);
            visited.insert(node.id);
        }
    }

    int index = 1;
    while (!queue.empty()) {
        const NodeData* node = FindNode(queue.front());
        queue.pop();
        if (!node) {
            continue;
        }

        NodeExecutionStep step;
        step.index = index++;
        step.nodeId = node->id;
        step.type = node->type;
        step.title = node->title;
        step.note = node->note;
        steps.push_back(std::move(step));

        for (const NodePin& output : node->outputs) {
            if (output.kind != NodePinKind::Flow) {
                continue;
            }
            for (const NodeLink& link : links_) {
                if (link.startPinId != output.id) {
                    continue;
                }
                const NodeData* nextNode = FindNodeByPin(link.endPinId);
                if (nextNode && visited.insert(nextNode->id).second) {
                    queue.push(nextNode->id);
                }
            }
        }
    }

    return steps;
}

int NodeGraphCore::AllocateNodeId() {
    return nextNodeId_++;
}

int NodeGraphCore::AllocatePinId() {
    return nextPinId_++;
}

int NodeGraphCore::AllocateLinkId() {
    return nextLinkId_++;
}

std::string ToString(NodePinKind value) {
    switch (value) {
    case NodePinKind::Flow:
        return "Flow";
    case NodePinKind::Value:
        return "Value";
    default:
        return "Unknown";
    }
}

std::string ToString(NodeValueType value) {
    switch (value) {
    case NodeValueType::Flow:
        return "Flow";
    case NodeValueType::Bool:
        return "Bool";
    case NodeValueType::Int:
        return "Int";
    case NodeValueType::Float:
        return "Float";
    case NodeValueType::String:
        return "String";
    case NodeValueType::Object:
        return "Object";
    case NodeValueType::Effect:
        return "Effect";
    case NodeValueType::Scene:
        return "Scene";
    default:
        return "Unknown";
    }
}

std::string ToString(NodePropertyType value) {
    switch (value) {
    case NodePropertyType::Bool:
        return "Bool";
    case NodePropertyType::Int:
        return "Int";
    case NodePropertyType::Float:
        return "Float";
    case NodePropertyType::String:
        return "String";
    default:
        return "String";
    }
}

std::string ToString(NodeIssueSeverity value) {
    switch (value) {
    case NodeIssueSeverity::Info:
        return "Info";
    case NodeIssueSeverity::Warning:
        return "Warning";
    case NodeIssueSeverity::Error:
        return "Error";
    default:
        return "Info";
    }
}

NodePinKind ParseNodePinKind(const std::string& value) {
    return value == "Value" ? NodePinKind::Value : NodePinKind::Flow;
}

NodeValueType ParseNodeValueType(const std::string& value) {
    if (value == "Bool") {
        return NodeValueType::Bool;
    }
    if (value == "Int") {
        return NodeValueType::Int;
    }
    if (value == "Float") {
        return NodeValueType::Float;
    }
    if (value == "String") {
        return NodeValueType::String;
    }
    if (value == "Object") {
        return NodeValueType::Object;
    }
    if (value == "Effect") {
        return NodeValueType::Effect;
    }
    if (value == "Scene") {
        return NodeValueType::Scene;
    }
    if (value == "Flow") {
        return NodeValueType::Flow;
    }
    return NodeValueType::Unknown;
}

NodePropertyType ParseNodePropertyType(const std::string& value) {
    if (value == "Bool") {
        return NodePropertyType::Bool;
    }
    if (value == "Int") {
        return NodePropertyType::Int;
    }
    if (value == "Float") {
        return NodePropertyType::Float;
    }
    return NodePropertyType::String;
}

bool IsCompatibleValueType(NodeValueType outputType, NodeValueType inputType) {
    if (outputType == inputType) {
        return true;
    }
    if (outputType == NodeValueType::Unknown || inputType == NodeValueType::Unknown) {
        return true;
    }
    return false;
}

} // namespace cg2::editor

