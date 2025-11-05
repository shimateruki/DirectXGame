#include "EventManager.h"


EventManager* EventManager::GetInstance() {
    static EventManager instance;
    return &instance;
}

// --- PlayerHitEvent 関連 ---

void EventManager::Subscribe(std::function<void(const PlayerHitEvent&)> callback) {
    // リストにコールバック関数を追加する
    playerHitListeners_.push_back(callback);
}

void EventManager::Dispatch(const PlayerHitEvent& event) {
    // 登録されている全てのリスナー（コールバック関数）を実行する
    for (auto& listener : playerHitListeners_) {
        listener(event);
    }
}