#include "NodeGraphExecutor.h"

#include "NodeGraphTemplateRegistry.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace cg2::editor {
namespace {

const NodeProperty* FindProperty(const NodeData& node, const std::string& name) {
    const auto it = std::find_if(node.properties.begin(), node.properties.end(), [&](const NodeProperty& property) {
        return property.name == name;
    });
    return it != node.properties.end() ? &(*it) : nullptr;
}

const NodePin* FindFirstFlowOutput(const NodeData& node) {
    const auto it = std::find_if(node.outputs.begin(), node.outputs.end(), [](const NodePin& pin) {
        return pin.kind == NodePinKind::Flow;
    });
    return it != node.outputs.end() ? &(*it) : nullptr;
}

} // namespace

void NodeGraphExecutor::Reset() {
    graph_ = nullptr;
    executionList_.clear();
    currentIndex_ = 0;
    running_ = false;
    finished_ = false;
    waitRemaining_ = 0.0f;
    statusMessage_.clear();
}

void NodeGraphExecutor::ClearActionHandlers() {
    actionHandlers_.clear();
}

void NodeGraphExecutor::RegisterActionHandler(const std::string& nodeType, NodeActionHandler handler) {
    actionHandlers_[nodeType] = std::move(handler);
}

void NodeGraphExecutor::SetLogHandler(std::function<void(const std::string&)> handler) {
    logHandler_ = std::move(handler);
}

bool NodeGraphExecutor::Start(const NodeGraphCore& graph, const std::string& eventType, std::string* errorMessage) {
    Reset();
    graph_ = &graph;

    if (!BuildLinearExecutionList(graph, eventType, errorMessage)) {
        running_ = false;
        finished_ = true;
        return false;
    }

    running_ = true;
    finished_ = false;
    statusMessage_ = "ドライラン開始: " + eventType;
    return true;
}

void NodeGraphExecutor::Update(float deltaTime) {
    if (!running_ || finished_) {
        return;
    }

    if (waitRemaining_ > 0.0f) {
        waitRemaining_ -= deltaTime;
        if (waitRemaining_ > 0.0f) {
            return;
        }
        waitRemaining_ = 0.0f;
    }

    while (currentIndex_ < executionList_.size()) {
        const NodeData* node = executionList_[currentIndex_++];
        if (!node) {
            continue;
        }

        if (!ExecuteNode(*node)) {
            running_ = false;
            finished_ = true;
            return;
        }

        if (waitRemaining_ > 0.0f) {
            return;
        }
    }

    running_ = false;
    finished_ = true;
    statusMessage_ = "ドライラン完了";
}

bool NodeGraphExecutor::BuildLinearExecutionList(const NodeGraphCore& graph, const std::string& eventType, std::string* errorMessage) {
    const NodeGraphTemplateRegistry& registry = NodeGraphTemplateRegistry::Instance();
    const NodeData* current = nullptr;

    for (const NodeData& node : graph.GetNodes()) {
        if (registry.IsEventNode(node.type) && (eventType.empty() || node.type == eventType)) {
            current = &node;
            break;
        }
    }

    if (!current) {
        if (errorMessage) {
            *errorMessage = "開始イベントが見つかりません: " + eventType;
        }
        return false;
    }

    std::vector<int> visited;
    while (current) {
        if (std::find(visited.begin(), visited.end(), current->id) != visited.end()) {
            if (errorMessage) {
                *errorMessage = "制御フローにループがあります。直列ドライランでは実行できません: " + current->title;
            }
            return false;
        }
        visited.push_back(current->id);
        executionList_.push_back(current);

        const NodePin* outputPin = FindFirstFlowOutput(*current);
        if (!outputPin) {
            break;
        }

        const NodeLink* nextLink = nullptr;
        int linkCount = 0;
        for (const NodeLink& link : graph.GetLinks()) {
            if (link.startPinId == outputPin->id) {
                nextLink = &link;
                ++linkCount;
            }
        }

        if (linkCount > 1) {
            if (errorMessage) {
                *errorMessage = "直列ドライランでは複数の実行リンクにまだ対応していません: " + current->title;
            }
            return false;
        }

        current = nextLink ? graph.FindNodeByPin(nextLink->endPinId) : nullptr;
    }

    return !executionList_.empty();
}

bool NodeGraphExecutor::ExecuteNode(const NodeData& node) {
    const NodeGraphTemplateRegistry& registry = NodeGraphTemplateRegistry::Instance();

    if (registry.IsEventNode(node.type)) {
        statusMessage_ = "イベント: " + node.title;
        return true;
    }

    if (registry.IsCommentNode(node.type) || registry.IsDataNode(node.type)) {
        return true;
    }

    if (node.type == "Flow.Wait") {
        waitRemaining_ = ReadFloatProperty(node, "seconds", 0.2f);
        statusMessage_ = "待機中: " + std::to_string(waitRemaining_) + "秒";
        return true;
    }

    if (node.type == "Debug.Log") {
        const std::string message = ReadStringProperty(node, "message", node.title);
        if (logHandler_) {
            logHandler_(message);
        }
        statusMessage_ = "ログ表示: " + message;
        return true;
    }

    const auto handlerIt = actionHandlers_.find(node.type);
    if (handlerIt == actionHandlers_.end()) {
        statusMessage_ = "未実装ノードをスキップ: " + node.title;
        return true;
    }

    NodeActionContext context;
    context.graph = graph_;
    context.node = &node;
    context.log = logHandler_;

    if (!handlerIt->second(context)) {
        statusMessage_ = "ノード実行に失敗: " + node.title;
        return false;
    }

    if (context.requestedWaitSeconds > 0.0f) {
        waitRemaining_ = context.requestedWaitSeconds;
        statusMessage_ = "演出待機: " + node.title;
    } else {
        statusMessage_ = "実行: " + node.title;
    }
    return true;
}

float NodeGraphExecutor::ReadFloatProperty(const NodeData& node, const std::string& name, float fallback) const {
    const NodeProperty* property = FindProperty(node, name);
    return property && property->type == NodePropertyType::Float ? property->floatValue : fallback;
}

std::string NodeGraphExecutor::ReadStringProperty(const NodeData& node, const std::string& name, const std::string& fallback) const {
    const NodeProperty* property = FindProperty(node, name);
    return property && property->type == NodePropertyType::String ? property->stringValue : fallback;
}

} // namespace cg2::editor

