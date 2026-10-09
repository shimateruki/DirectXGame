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
    // 適用時まで呼出先が有効であることが必要。生ポインタを捕捉する場合は対象の寿命を呼出側で保証する。
    std::function<void()> undo;
    std::function<void()> redo;
};

/// Inspector、Gizmo、Ghost Recorderなどの履歴を一つの時系列へ統合します。
class EditorTransactionManager {
public:
    static EditorTransactionManager* GetInstance();

    // undo/redo が両方ある操作だけ登録する。新規登録時は Redo 履歴を破棄する。
    void Register(EditorTransaction transaction);

    // ドラッグ等の連続操作を一件にする。同じ itemId で Commit すると履歴へ登録する。
    void BeginInteractive(std::uint64_t itemId, const std::string& label, std::function<void()> undo);
    void CommitInteractive(std::uint64_t itemId, std::function<void()> redo);
    // 保留中の履歴を捨てるだけで、編集値を元に戻す undo は実行しない。
    void CancelInteractive(std::uint64_t itemId);

    // 入れ子の Group は最外側の EndGroup で一件の履歴へまとめる。
    void BeginGroup(const std::string& label);
    // グループの Undo は登録と逆順、Redo は登録順。依存する複数操作を元の順序で復元する。
    void EndGroup();

    // 実行できる履歴がない場合は false。履歴適用中の再登録は抑制する。
    bool Undo();
    bool Redo();
    void Clear();

    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }
    const std::string& GetUndoLabel() const;
    const std::string& GetRedoLabel() const;

private:
    // ドラッグ中の履歴候補を UI の itemId に結び付け、確定まで一つの操作として保持する。
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
