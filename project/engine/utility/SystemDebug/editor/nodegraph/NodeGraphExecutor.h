#pragma once

#include "NodeGraphCore.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cg2::editor {

// ドライラン中にアクションノードへ渡す情報です。
// 実ゲームへ接続する前でも、処理順や待ち時間を確認できるようにします。
struct NodeActionContext {
    const NodeGraphCore* graph = nullptr;
    const NodeData* node = nullptr;
    float requestedWaitSeconds = 0.0f;
    std::function<void(const std::string&)> log;

    void RequestWait(float seconds) { requestedWaitSeconds = seconds; }
    void Log(const std::string& message) const {
        if (log) {
            log(message);
        }
    }
};

using NodeActionHandler = std::function<bool(NodeActionContext&)>;

// Effect Sequence Graph の簡易実行器です。
// まだ実ゲーム演出を直接動かす段階ではなく、Editor 上で流れを確認するための直列 Executor です。
class NodeGraphExecutor {
public:
    void Reset();
    void ClearActionHandlers();
    void RegisterActionHandler(const std::string& nodeType, NodeActionHandler handler);
    void SetLogHandler(std::function<void(const std::string&)> handler);

    bool Start(const NodeGraphCore& graph, const std::string& eventType, std::string* errorMessage = nullptr);
    void Update(float deltaTime);

    bool IsRunning() const { return running_; }
    bool IsFinished() const { return finished_; }
    float GetWaitRemaining() const { return waitRemaining_; }
    const std::string& GetStatusMessage() const { return statusMessage_; }
    const std::string& GetStateText() const { return statusMessage_; }

private:
    bool BuildLinearExecutionList(const NodeGraphCore& graph, const std::string& eventType, std::string* errorMessage);
    bool ExecuteNode(const NodeData& node);
    float ReadFloatProperty(const NodeData& node, const std::string& name, float fallback) const;
    std::string ReadStringProperty(const NodeData& node, const std::string& name, const std::string& fallback) const;

    const NodeGraphCore* graph_ = nullptr;
    std::vector<const NodeData*> executionList_;
    std::unordered_map<std::string, NodeActionHandler> actionHandlers_;
    std::function<void(const std::string&)> logHandler_;

    size_t currentIndex_ = 0;
    bool running_ = false;
    bool finished_ = false;
    float waitRemaining_ = 0.0f;
    std::string statusMessage_;
};

} // namespace cg2::editor
