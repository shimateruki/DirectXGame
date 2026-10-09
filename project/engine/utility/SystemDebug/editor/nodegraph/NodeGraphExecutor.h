#pragma once

#include "NodeGraphCore.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cg2::editor {

// ドライラン中のアクションノードへ渡す参照と、次の更新までの待機要求を保持する。
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

// Effect Sequence Graph の直列ドライラン実行器。分岐とループは実行対象外とする。
class NodeGraphExecutor {
public:
    // 実行状態と参照中のグラフを破棄する。登録済みハンドラは維持する。
    void Reset();
    void ClearActionHandlers();
    // ノード種別ごとの処理を登録する。処理が false を返すとドライランを終了する。
    void RegisterActionHandler(const std::string& nodeType, NodeActionHandler handler);
    void SetLogHandler(std::function<void(const std::string&)> handler);

    // eventType が空なら最初のイベントを起点にする。graph はドライラン終了まで存続させる。
    // 対応しない分岐やループがあれば false を返し、errorMessage に理由を書く。
    bool Start(const NodeGraphCore& graph, const std::string& eventType, std::string* errorMessage = nullptr);
    // deltaTime は秒。待機が終わると次のノードから実行を再開する。
    void Update(float deltaTime);

    bool IsRunning() const { return running_; }
    bool IsFinished() const { return finished_; }
    float GetWaitRemaining() const { return waitRemaining_; }
    const std::string& GetStatusMessage() const { return statusMessage_; }
    const std::string& GetStateText() const { return statusMessage_; }

private:
    bool BuildLinearExecutionList(const NodeGraphCore& graph, const std::string& eventType, std::string* errorMessage);
    // 1ノード分のアクションを実行し、必要なら待機やログを反映する。
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
