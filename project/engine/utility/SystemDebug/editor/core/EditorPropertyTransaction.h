#pragma once

#include "EditorTransactionManager.h"

#include <cstdint>
#include <string>
#include <utility>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace EditorPropertyTransaction {

// ImGui の直前に描いた項目の有効化から編集終了までを一つの Undo/Redo にまとめます。
// apply は保存値だけでなく編集対象への再適用まで行い、Undo/Redo 時にも呼ばれます。
template <class TValue, class TApply>
void TrackLastItem(
    const std::string& label,
    const TValue& valueBeforeDraw,
    const TValue& valueAfterDraw,
    TApply apply) {
#ifdef USE_IMGUI
    // ドラッグ中の細かな変化を、項目の有効化から編集終了まで一件の履歴にする。
    const std::uint64_t itemId = static_cast<std::uint64_t>(ImGui::GetItemID());
    EditorTransactionManager* transactions = EditorTransactionManager::GetInstance();
    if (ImGui::IsItemActivated()) {
        transactions->BeginInteractive(
            itemId,
            label,
            [valueBeforeDraw, apply]() mutable { apply(valueBeforeDraw); });
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        transactions->CommitInteractive(
            itemId,
            [valueAfterDraw, apply]() mutable { apply(valueAfterDraw); });
    }
#else
    (void)label;
    (void)valueBeforeDraw;
    (void)valueAfterDraw;
    (void)apply;
#endif
}

// Checkbox、追加、削除など1フレームで完了する変更を履歴へ登録します。
template <class TValue, class TApply>
void RegisterDiscrete(
    const std::string& label,
    const TValue& valueBefore,
    const TValue& valueAfter,
    TApply apply) {
    EditorTransaction transaction;
    transaction.label = label;
    transaction.undo = [valueBefore, apply]() mutable { apply(valueBefore); };
    transaction.redo = [valueAfter, apply]() mutable { apply(valueAfter); };
    EditorTransactionManager::GetInstance()->Register(std::move(transaction));
}

} // namespace EditorPropertyTransaction
