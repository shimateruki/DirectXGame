#include "Timer.h"

#include <algorithm>
#include <cmath>

namespace engine {
bool TimerView::IsValid() const { return !state_.expired(); }
float TimerView::Elapsed() const {
    const auto state = state_.lock();
    return state ? state->elapsed : 0.0f;
}
bool TimerView::IsRunning() const {
    const auto state = state_.lock();
    return state && state->running;
}

Timer::Timer() : state_(std::make_shared<TimerView::State>()) {}
void Timer::Start() { state_->elapsed = 0.0f; state_->running = true; }
void Timer::Pause() { state_->running = false; }
void Timer::Resume() { state_->running = true; }
void Timer::Stop() { state_->running = false; state_->elapsed = 0.0f; }
void Timer::Update(float deltaSeconds) {
    // 不正な入力や加算の桁あふれで、参照先全体の時計を壊さない。
    if (state_->running && std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) {
        Seek(state_->elapsed + deltaSeconds);
    }
}
void Timer::Seek(float seconds) {
    // 指定時刻は開始からの秒です。再生中か一時停止中かを保ったまま、経過時間だけを更新します。
    if (std::isfinite(seconds)) state_->elapsed = std::max(0.0f, seconds);
}
float Timer::Elapsed() const { return state_->elapsed; }
bool Timer::IsRunning() const { return state_->running; }
TimerView Timer::GetView() const {
    TimerView view;
    // 弱参照だけを渡すため、参照側が時計の寿命を延ばすことはありません。
    view.state_ = state_;
    return view;
}
} // namespace engine
