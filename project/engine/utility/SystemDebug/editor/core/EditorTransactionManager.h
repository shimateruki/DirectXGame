#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Editor全体で共有する1回分のUndo/Redo操作です。
struct EditorTransaction {
    std::string label;
    std::function<void()> undo;
    std::function<void()> redo;
};

/// Inspector、Gizmo、Ghost Recorderなどの履歴を一つの時系列へ統合します。
class EditorTransactionManager {
public:
    static EditorTransactionManager* GetInstance();

    void Register(EditorTransaction transaction);
    void BeginInteractive(std::uint64_t itemId, const std::string& label, std::function<void()> undo);
    void CommitInteractive(std::uint64_t itemId, std::function<void()> redo);
    void CancelInteractive(std::uint64_t itemId);
    void BeginGroup(const std::string& label);
    void EndGroup();
    bool Undo();
    bool Redo();
    void Clear();

    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }
    const std::string& GetUndoLabel() const;
    const std::string& GetRedoLabel() const;

private:
    struct InteractiveTransaction {
        std::uint64_t itemId = 0;
        EditorTransaction transaction;
    };

    static constexpr std::size_t kMaxHistoryEntries = 128;
    std::deque<EditorTransaction> undoStack_;
    std::deque<EditorTransaction> redoStack_;
    std::vector<EditorTransaction> groupTransactions_;
    std::string groupLabel_;
    int groupDepth_ = 0;
    bool isApplying_ = false;
    std::optional<InteractiveTransaction> interactiveTransaction_;
};
