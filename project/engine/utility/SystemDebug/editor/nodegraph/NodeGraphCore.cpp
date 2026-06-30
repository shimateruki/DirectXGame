#include "NodeGraphCore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace cg2::editor {

namespace {
constexpr int kInvalidId = 0;

std::string ReadTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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

    NodeData& start = AddNode("Event.Start", "開始", 40.0f, 80.0f);
    AddOutputPin(start, "実行", NodeValueType::Flow);
    start.note = "ここから処理が始まります。将来的にはシーン開始や接触イベントに置き換えます。";

    NodeData& wait = AddNode("Flow.Wait", "待機", 320.0f, 80.0f);
    AddInputPin(wait, "入", NodeValueType::Flow);
    AddOutputPin(wait, "出", NodeValueType::Flow);
    AddInputPin(wait, "秒", NodeValueType::Float);
    wait.note = "フローを少し止めるノードの例です。";

    NodeData& log = AddNode("Debug.Log", "ログ表示", 640.0f, 80.0f);
    AddInputPin(log, "入", NodeValueType::Flow);
    AddInputPin(log, "文字", NodeValueType::String);
    log.note = "実行基盤を作ったあと、DebugConsole などに接続できます。";

    if (!start.outputs.empty() && !wait.inputs.empty()) {
        AddLink(start.outputs.front().id, wait.inputs.front().id);
    }
    if (!wait.outputs.empty() && !log.inputs.empty()) {
        AddLink(wait.outputs.front().id, log.inputs.front().id);
    }
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

NodePin& NodeGraphCore::AddInputPin(NodeData& node, const std::string& name, NodeValueType valueType) {
    NodePin pin;
    pin.id = AllocatePinId();
    pin.name = name;
    pin.kind = NodePinKind::Input;
    pin.valueType = valueType;
    node.inputs.push_back(std::move(pin));
    return node.inputs.back();
}

NodePin& NodeGraphCore::AddOutputPin(NodeData& node, const std::string& name, NodeValueType valueType) {
    NodePin pin;
    pin.id = AllocatePinId();
    pin.name = name;
    pin.kind = NodePinKind::Output;
    pin.valueType = valueType;
    node.outputs.push_back(std::move(pin));
    return node.outputs.back();
}

bool NodeGraphCore::CanCreateLink(int startPinId, int endPinId, std::string* reason) const {
    const NodePin* startPin = FindPin(startPinId);
    const NodePin* endPin = FindPin(endPinId);
    if (!startPin || !endPin) {
        if (reason) *reason = "存在しないピンです。";
        return false;
    }

    if (startPin->kind == NodePinKind::Input && endPin->kind == NodePinKind::Output) {
        std::swap(startPinId, endPinId);
        std::swap(startPin, endPin);
    }

    if (startPin->kind != NodePinKind::Output || endPin->kind != NodePinKind::Input) {
        if (reason) *reason = "リンクは出力ピンから入力ピンへ接続してください。";
        return false;
    }

    const NodeData* startNode = FindNodeByPin(startPinId);
    const NodeData* endNode = FindNodeByPin(endPinId);
    if (!startNode || !endNode || startNode->id == endNode->id) {
        if (reason) *reason = "同じノード内のピン同士は接続できません。";
        return false;
    }

    if (!IsCompatibleValueType(startPin->valueType, endPin->valueType)) {
        if (reason) *reason = "ピンの型が合っていません。";
        return false;
    }

    if (IsPinLinkedTo(startPinId, endPinId)) {
        if (reason) *reason = "同じリンクがすでに存在します。";
        return false;
    }

    if (reason) reason->clear();
    return true;
}

NodeLink* NodeGraphCore::AddLink(int startPinId, int endPinId) {
    const NodePin* startPin = FindPin(startPinId);
    const NodePin* endPin = FindPin(endPinId);
    if (startPin && endPin && startPin->kind == NodePinKind::Input && endPin->kind == NodePinKind::Output) {
        std::swap(startPinId, endPinId);
    }

    if (!CanCreateLink(startPinId, endPinId, nullptr)) {
        return nullptr;
    }

    NodeLink link;
    link.id = AllocateLinkId();
    link.startPinId = startPinId;
    link.endPinId = endPinId;
    links_.push_back(link);
    return &links_.back();
}

bool NodeGraphCore::RemoveLink(int linkId) {
    const auto oldSize = links_.size();
    links_.erase(std::remove_if(links_.begin(), links_.end(), [linkId](const NodeLink& link) {
        return link.id == linkId;
    }), links_.end());
    return oldSize != links_.size();
}

void NodeGraphCore::RemoveLinksForPin(int pinId) {
    links_.erase(std::remove_if(links_.begin(), links_.end(), [pinId](const NodeLink& link) {
        return link.startPinId == pinId || link.endPinId == pinId;
    }), links_.end());
}

NodeData* NodeGraphCore::FindNode(int nodeId) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(), [nodeId](const NodeData& node) { return node.id == nodeId; });
    return it == nodes_.end() ? nullptr : &(*it);
}

const NodeData* NodeGraphCore::FindNode(int nodeId) const {
    auto it = std::find_if(nodes_.begin(), nodes_.end(), [nodeId](const NodeData& node) { return node.id == nodeId; });
    return it == nodes_.end() ? nullptr : &(*it);
}

NodePin* NodeGraphCore::FindPin(int pinId) {
    for (NodeData& node : nodes_) {
        for (NodePin& pin : node.inputs) {
            if (pin.id == pinId) return &pin;
        }
        for (NodePin& pin : node.outputs) {
            if (pin.id == pinId) return &pin;
        }
    }
    return nullptr;
}

const NodePin* NodeGraphCore::FindPin(int pinId) const {
    for (const NodeData& node : nodes_) {
        for (const NodePin& pin : node.inputs) {
            if (pin.id == pinId) return &pin;
        }
        for (const NodePin& pin : node.outputs) {
            if (pin.id == pinId) return &pin;
        }
    }
    return nullptr;
}

NodeData* NodeGraphCore::FindNodeByPin(int pinId) {
    for (NodeData& node : nodes_) {
        const auto hasInput = std::any_of(node.inputs.begin(), node.inputs.end(), [pinId](const NodePin& pin) { return pin.id == pinId; });
        const auto hasOutput = std::any_of(node.outputs.begin(), node.outputs.end(), [pinId](const NodePin& pin) { return pin.id == pinId; });
        if (hasInput || hasOutput) return &node;
    }
    return nullptr;
}

const NodeData* NodeGraphCore::FindNodeByPin(int pinId) const {
    for (const NodeData& node : nodes_) {
        const auto hasInput = std::any_of(node.inputs.begin(), node.inputs.end(), [pinId](const NodePin& pin) { return pin.id == pinId; });
        const auto hasOutput = std::any_of(node.outputs.begin(), node.outputs.end(), [pinId](const NodePin& pin) { return pin.id == pinId; });
        if (hasInput || hasOutput) return &node;
    }
    return nullptr;
}

NodeLink* NodeGraphCore::FindLink(int linkId) {
    auto it = std::find_if(links_.begin(), links_.end(), [linkId](const NodeLink& link) { return link.id == linkId; });
    return it == links_.end() ? nullptr : &(*it);
}

const NodeLink* NodeGraphCore::FindLink(int linkId) const {
    auto it = std::find_if(links_.begin(), links_.end(), [linkId](const NodeLink& link) { return link.id == linkId; });
    return it == links_.end() ? nullptr : &(*it);
}

nlohmann::json NodeGraphCore::ToJson() const {
    nlohmann::json root;
    root["version"] = 1;
    root["nodes"] = nlohmann::json::array();
    root["links"] = nlohmann::json::array();

    for (const NodeData& node : nodes_) {
        nlohmann::json nodeJson;
        nodeJson["id"] = node.id;
        nodeJson["type"] = node.type;
        nodeJson["title"] = node.title;
        nodeJson["note"] = node.note;
        nodeJson["editorPosition"] = { { "x", node.editorX }, { "y", node.editorY } };
        nodeJson["inputs"] = nlohmann::json::array();
        nodeJson["outputs"] = nlohmann::json::array();

        auto writePin = [](const NodePin& pin) {
            nlohmann::json pinJson;
            pinJson["id"] = pin.id;
            pinJson["name"] = pin.name;
            pinJson["kind"] = ToString(pin.kind);
            pinJson["valueType"] = ToString(pin.valueType);
            return pinJson;
        };

        for (const NodePin& pin : node.inputs) {
            nodeJson["inputs"].push_back(writePin(pin));
        }
        for (const NodePin& pin : node.outputs) {
            nodeJson["outputs"].push_back(writePin(pin));
        }
        root["nodes"].push_back(nodeJson);
    }

    for (const NodeLink& link : links_) {
        root["links"].push_back({
            { "id", link.id },
            { "startPinId", link.startPinId },
            { "endPinId", link.endPinId }
        });
    }
    return root;
}

bool NodeGraphCore::FromJson(const nlohmann::json& json, std::string* errorMessage) {
    if (!json.is_object()) {
        if (errorMessage) *errorMessage = "ノードグラフJSONのルートがオブジェクトではありません。";
        return false;
    }
    if (!json.contains("nodes") || !json["nodes"].is_array()) {
        if (errorMessage) *errorMessage = "nodes 配列がありません。";
        return false;
    }

    std::vector<NodeData> loadedNodes;
    std::vector<NodeLink> loadedLinks;

    for (const auto& nodeJson : json["nodes"]) {
        if (!nodeJson.is_object()) continue;

        NodeData node;
        node.id = nodeJson.value("id", kInvalidId);
        node.type = nodeJson.value("type", std::string("Unknown"));
        node.title = nodeJson.value("title", node.type);
        node.note = nodeJson.value("note", std::string());
        if (node.id == kInvalidId) {
            if (errorMessage) *errorMessage = "ノードIDが不正です。";
            return false;
        }

        if (const auto posIt = nodeJson.find("editorPosition"); posIt != nodeJson.end() && posIt->is_object()) {
            node.editorX = posIt->value("x", 0.0f);
            node.editorY = posIt->value("y", 0.0f);
        }

        auto readPins = [&](const char* key, NodePinKind expectedKind, std::vector<NodePin>& outPins) -> bool {
            if (!nodeJson.contains(key) || !nodeJson[key].is_array()) return true;
            for (const auto& pinJson : nodeJson[key]) {
                if (!pinJson.is_object()) continue;
                NodePin pin;
                pin.id = pinJson.value("id", kInvalidId);
                pin.name = pinJson.value("name", std::string("Pin"));
                pin.kind = expectedKind;
                if (const auto kind = ParsePinKind(pinJson.value("kind", std::string())); kind.has_value()) {
                    pin.kind = *kind;
                }
                if (const auto type = ParseValueType(pinJson.value("valueType", std::string("Any"))); type.has_value()) {
                    pin.valueType = *type;
                }
                if (pin.id == kInvalidId) {
                    if (errorMessage) *errorMessage = "ピンIDが不正です。";
                    return false;
                }
                outPins.push_back(std::move(pin));
            }
            return true;
        };

        if (!readPins("inputs", NodePinKind::Input, node.inputs)) return false;
        if (!readPins("outputs", NodePinKind::Output, node.outputs)) return false;
        loadedNodes.push_back(std::move(node));
    }

    if (const auto linksIt = json.find("links"); linksIt != json.end() && linksIt->is_array()) {
        for (const auto& linkJson : *linksIt) {
            if (!linkJson.is_object()) continue;
            NodeLink link;
            link.id = linkJson.value("id", kInvalidId);
            link.startPinId = linkJson.value("startPinId", kInvalidId);
            link.endPinId = linkJson.value("endPinId", kInvalidId);
            if (link.id == kInvalidId || link.startPinId == kInvalidId || link.endPinId == kInvalidId) {
                continue;
            }
            loadedLinks.push_back(link);
        }
    }

    nodes_ = std::move(loadedNodes);
    links_.clear();
    RebuildNextIds();

    for (const NodeLink& link : loadedLinks) {
        if (CanCreateLink(link.startPinId, link.endPinId, nullptr)) {
            links_.push_back(link);
        }
    }
    RebuildNextIds();
    if (errorMessage) errorMessage->clear();
    return true;
}

bool NodeGraphCore::SaveToFile(const std::string& path, std::string* errorMessage) const {
    try {
        std::filesystem::path filePath(path);
        if (!filePath.parent_path().empty()) {
            std::filesystem::create_directories(filePath.parent_path());
        }
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            if (errorMessage) *errorMessage = "保存ファイルを開けませんでした。";
            return false;
        }
        file << ToJson().dump(4);
        if (errorMessage) errorMessage->clear();
        return file.good();
    }
    catch (const std::exception& e) {
        if (errorMessage) *errorMessage = e.what();
        return false;
    }
}

bool NodeGraphCore::LoadFromFile(const std::string& path, std::string* errorMessage) {
    try {
        const std::string text = ReadTextFile(path);
        if (text.empty()) {
            if (errorMessage) *errorMessage = "読み込みファイルが空、または開けませんでした。";
            return false;
        }
        return FromJson(nlohmann::json::parse(text), errorMessage);
    }
    catch (const std::exception& e) {
        if (errorMessage) *errorMessage = e.what();
        return false;
    }
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

void NodeGraphCore::RebuildNextIds() {
    int maxNode = 0;
    int maxPin = 1000;
    int maxLink = 2000;
    for (const NodeData& node : nodes_) {
        maxNode = std::max(maxNode, node.id);
        for (const NodePin& pin : node.inputs) maxPin = std::max(maxPin, pin.id);
        for (const NodePin& pin : node.outputs) maxPin = std::max(maxPin, pin.id);
    }
    for (const NodeLink& link : links_) {
        maxLink = std::max(maxLink, link.id);
    }
    nextNodeId_ = maxNode + 1;
    nextPinId_ = maxPin + 1;
    nextLinkId_ = maxLink + 1;
}

bool NodeGraphCore::IsPinLinkedTo(int startPinId, int endPinId) const {
    return std::any_of(links_.begin(), links_.end(), [startPinId, endPinId](const NodeLink& link) {
        return link.startPinId == startPinId && link.endPinId == endPinId;
    });
}

const char* NodeGraphCore::ToString(NodePinKind kind) {
    return kind == NodePinKind::Input ? "Input" : "Output";
}

const char* NodeGraphCore::ToString(NodeValueType type) {
    switch (type) {
    case NodeValueType::Flow: return "Flow";
    case NodeValueType::Bool: return "Bool";
    case NodeValueType::Int: return "Int";
    case NodeValueType::Float: return "Float";
    case NodeValueType::String: return "String";
    case NodeValueType::Object: return "Object";
    case NodeValueType::Any: return "Any";
    default: return "Any";
    }
}

std::optional<NodePinKind> NodeGraphCore::ParsePinKind(const std::string& value) {
    if (value == "Input") return NodePinKind::Input;
    if (value == "Output") return NodePinKind::Output;
    return std::nullopt;
}

std::optional<NodeValueType> NodeGraphCore::ParseValueType(const std::string& value) {
    if (value == "Flow") return NodeValueType::Flow;
    if (value == "Bool") return NodeValueType::Bool;
    if (value == "Int") return NodeValueType::Int;
    if (value == "Float") return NodeValueType::Float;
    if (value == "String") return NodeValueType::String;
    if (value == "Object") return NodeValueType::Object;
    if (value == "Any") return NodeValueType::Any;
    return std::nullopt;
}

bool NodeGraphCore::IsCompatibleValueType(NodeValueType outputType, NodeValueType inputType) {
    if (outputType == NodeValueType::Any || inputType == NodeValueType::Any) return true;
    return outputType == inputType;
}

} // namespace cg2::editor