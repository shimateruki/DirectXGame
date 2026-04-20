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

void EventManager::Subscribe(std::function<void(const BulletHitEvent&)> callback) {
    // Bullet 用のリストに追加する
    bulletHitListeners_.push_back(callback);
}

void EventManager::Dispatch(const BulletHitEvent& event) {
    // Bullet 用のリストのリスナーを実行する
    for (auto& listener : bulletHitListeners_) {
        listener(event);
    }
}

void EventManager::Subscribe(std::function<void(const DamageEvent&)> callback) {
    damageListeners_.push_back(callback);
}

void EventManager::Dispatch(const DamageEvent& event) {
    for (auto& listener : damageListeners_) {
        listener(event);
    }
}

void EventManager::ClearAllListeners() {
    playerHitListeners_.clear();
    bulletHitListeners_.clear();
    damageListeners_.clear();
}