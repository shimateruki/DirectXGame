#include "EditorTransactionManager.h"

#include <memory>
#include <utility>

EditorTransactionManager* EditorTransactionManager::GetInstance() {
    static EditorTransactionManager instance;
    return &instance;
}

void EditorTransactionManager::Register(EditorTransaction transaction) {
    if (isApplying_ || !transaction.undo || !transaction.redo) {
        return;
    }
    if (transaction.label.empty()) {
        transaction.label = "Editor Edit";
    }

    if (groupDepth_ > 0) {
        groupTransactions_.push_back(std::move(transaction));
        return;
    }

    undoStack_.push_back(std::move(transaction));
    redoStack_.clear();
    while (undoStack_.size() > kMaxHistoryEntries) {
        undoStack_.pop_front();
    }
}

void EditorTransactionManager::BeginGroup(const std::string& label) {
    if (isApplying_) {
        return;
    }
    if (groupDepth_ == 0) {
        groupTransactions_.clear();
        groupLabel_ = label.empty() ? "Editor Group Edit" : label;
    }
    ++groupDepth_;
}

void EditorTransactionManager::EndGroup() {
    if (isApplying_ || groupDepth_ <= 0) {
        return;
    }
    --groupDepth_;
    if (groupDepth_ > 0) {
        return;
    }

    if (groupTransactions_.empty()) {
        groupLabel_.clear();
        return;
    }

    auto grouped = std::make_shared<std::vector<EditorTransaction>>(std::move(groupTransactions_));
    EditorTransaction transaction;
    transaction.label = groupLabel_;
    transaction.undo = [grouped]() {
        for (auto it = grouped->rbegin(); it != grouped->rend(); ++it) {
            if (it->undo) {
                it->undo();
            }
        }
    };
    transaction.redo = [grouped]() {
        for (EditorTransaction& entry : *grouped) {
            if (entry.redo) {
                entry.redo();
            }
        }
    };
    groupLabel_.clear();
    Register(std::move(transaction));
}

bool EditorTransactionManager::Undo() {
    if (undoStack_.empty() || isApplying_) {
        return false;
    }

    EditorTransaction transaction = std::move(undoStack_.back());
    undoStack_.pop_back();
    isApplying_ = true;
    transaction.undo();
    isApplying_ = false;
    redoStack_.push_back(std::move(transaction));
    return true;
}

bool EditorTransactionManager::Redo() {
    if (redoStack_.empty() || isApplying_) {
        return false;
    }

    EditorTransaction transaction = std::move(redoStack_.back());
    redoStack_.pop_back();
    isApplying_ = true;
    transaction.redo();
    isApplying_ = false;
    undoStack_.push_back(std::move(transaction));
    return true;
}

void EditorTransactionManager::Clear() {
    if (isApplying_) {
        return;
    }
    undoStack_.clear();
    redoStack_.clear();
    groupTransactions_.clear();
    groupLabel_.clear();
    groupDepth_ = 0;
}

const std::string& EditorTransactionManager::GetUndoLabel() const {
    static const std::string empty;
    return undoStack_.empty() ? empty : undoStack_.back().label;
}

const std::string& EditorTransactionManager::GetRedoLabel() const {
    static const std::string empty;
    return redoStack_.empty() ? empty : redoStack_.back().label;
}
