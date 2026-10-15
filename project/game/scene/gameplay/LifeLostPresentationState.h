#pragma once

#include "json.hpp"

#include <algorithm>

// 残機減少演出の時間と遷移状態を所有します。Sprite・Cameraはシーンが管理します。
class LifeLostPresentationState {
  public:
    // 残機減少前後の表示値を 0～99 に収め、暗転保持から演出を開始します。
    void Start(int beforeLives, int afterLives) {
        active_ = true;
        finished_ = false;
        blackHold_ = true;
        numberDropped_ = false;
        revive_ = afterLives > 0;
        timer_ = 0.0f;
        beforeLives_ = std::clamp(beforeLives, 0, 99);
        afterLives_ = std::clamp(afterLives, 0, 99);
    }

    // 演出を非表示で完了扱いにし、暗転保持を解除します。
    void Hide() {
        active_ = false;
        finished_ = true;
        blackHold_ = false;
    }

    // 再生状態と残機表示値を初期値へ戻します。
    void Reset() {
        *this = LifeLostPresentationState{};
    }

    // 呼び出し側の経過時間を加算します。負値の補正は行いません。
    void Advance(float deltaTime) { timer_ += deltaTime; }
    void MarkNumberDropped() { numberDropped_ = true; }
    // 再生を終え、後続の画面遷移まで暗転を保持します。
    void Finish() {
        active_ = false;
        finished_ = true;
        blackHold_ = true;
    }

    bool IsActive() const { return active_; }
    bool IsFinished() const { return finished_; }
    bool HoldsBlack() const { return blackHold_; }
    bool DidDropNumber() const { return numberDropped_; }
    bool Revives() const { return revive_; }
    float Timer() const { return timer_; }
    int BeforeLives() const { return beforeLives_; }
    int AfterLives() const { return afterLives_; }

    // 残機減少演出の進行状態を Replay 用 JSON として取得します。
    nlohmann::json CaptureReplay() const {
        return {{"active", active_}, {"finished", finished_}, {"blackHold", blackHold_},
                {"numberDropped", numberDropped_}, {"timer", timer_},
                {"beforeLives", beforeLives_}, {"afterLives", afterLives_}, {"revive", revive_}};
    }

    // JSON に存在する項目だけを復元します。オブジェクト以外は無視します。
    void RestoreReplay(const nlohmann::json& state) {
        if (!state.is_object()) return;
        active_ = state.value("active", active_);
        finished_ = state.value("finished", finished_);
        blackHold_ = state.value("blackHold", blackHold_);
        numberDropped_ = state.value("numberDropped", numberDropped_);
        timer_ = state.value("timer", timer_);
        beforeLives_ = state.value("beforeLives", beforeLives_);
        afterLives_ = state.value("afterLives", afterLives_);
        revive_ = state.value("revive", revive_);
    }

  private:
    bool active_ = false;
    bool finished_ = true;
    bool blackHold_ = false;
    bool numberDropped_ = false;
    float timer_ = 0.0f;
    int beforeLives_ = 0;
    int afterLives_ = 0;
    bool revive_ = false;
};
