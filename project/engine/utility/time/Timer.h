#pragma once

#include <memory>

namespace engine {
class Timer;

// Timerの時刻を借りて読む窓口。更新は所有者に任せ、破棄後は無効になる。
// Elapsed / IsRunningで確認し、IsValidで所有者の生存を判定する。スレッド間共有用ではない。
class TimerView {
public:
    bool IsValid() const;
    float Elapsed() const; // 秒。未接続・所有者破棄後は0。
    bool IsRunning() const; // 未接続・所有者破棄後はfalse。

private:
    friend class Timer;
    struct State {
        float elapsed = 0.0f; // 開始からの秒数。Seekで指定時刻へ変更できる。
        bool running = false; // Updateによる加算を許可するか。
    };
    std::weak_ptr<const State> state_; // 生存期間は延長せず、同じ時計を参照する。
};

// 秒単位の時計を所有する。Start / Updateで計測し、GetViewで読み取り専用参照を渡す。
// 演出の長さ・ループ・イベントはCinematicPlayerが担当する。
class Timer {
public:
    Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    void Start(); // 0秒へ戻して計測開始。
    void Pause(); // 時刻を保って加算を止める。
    void Resume(); // 現在時刻から加算を再開。
    void Stop(); // 停止して0秒へ戻す。既存ビューも追従する。
    void Update(float deltaSeconds); // 正の有限秒数のみ加算。停止中は無視する。
    void Seek(float seconds); // 進行状態を変えず時刻設定。負値は0、非有限値は無視する。
    float Elapsed() const;
    bool IsRunning() const;
    TimerView GetView() const;

private:
    std::shared_ptr<TimerView::State> state_; // 時刻の唯一の保存先。ビューに所有権を渡さない。
};
} // namespace engine
