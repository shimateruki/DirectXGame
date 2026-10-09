#pragma once
#include <algorithm>

// 旧コンボ入力APIの状態を所有します。時間の進行や入力取得は行いません。
// 現行のコピー能力のコンボ制御とは独立し、既存リプレイとの互換性を保ちます。
class PlayerAttackInputBuffer {
  public:
    // 値コピーで受け渡し、リプレイ側から内部状態を直接変更させません。
    struct Snapshot {
        float comboWindowTimer = 0.0f;
        bool buffered = false;
        bool usedForStateStart = false;
        float inputBufferTimer = 0.0f;
    };

    void StartComboWindow(float duration) { state_.comboWindowTimer = (std::max)(0.0f, duration); }
    bool IsComboWindowActive() const { return state_.comboWindowTimer > 0.0f; }

    // duration は秒として保持するが、この互換クラス自身は残り時間を進めない。
    void Record(float duration) {
        state_.buffered = true;
        state_.inputBufferTimer = (std::max)(0.0f, duration);
        state_.usedForStateStart = false;
    }
    void MarkUsedForStateStart() {
        if (state_.buffered)
            state_.usedForStateStart = true;
    }
    // 使用済みフラグだけで消費可否を決める。timer 値はここでは判定しない。
    bool Consume() {
        if (state_.buffered && !state_.usedForStateStart) {
            state_.buffered = false;
            state_.inputBufferTimer = 0.0f;
            state_.usedForStateStart = false;
            return true;
        }
        return false;
    }

    void ClearPendingInput() {
        state_.comboWindowTimer = 0.0f;
        state_.buffered = false;
        state_.inputBufferTimer = 0.0f;
        // 既存の解除処理と同じく使用済みフラグは保持します。
        // 次のRecordで解除され、途中状態のリプレイ復元にも影響しません。
    }

    Snapshot Capture() const { return state_; }
    // 保存値は再解釈・補正せず復元します（古いリプレイの値も保持）。
    void Restore(const Snapshot& snapshot) { state_ = snapshot; }

  private:
    Snapshot state_;
};
