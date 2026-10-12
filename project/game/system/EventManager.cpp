#include "EventManager.h"

EventManager* EventManager::GetInstance() {
    // シーン内のイベント購読を同じバスへ集約する。
    static EventManager instance;
    return &instance;
}

// --- PlayerHitEvent 関連 ---

void EventManager::Subscribe(std::function<void(const PlayerHitEvent&)> callback) {
    // リストにコールバック関数を追加する
    playerHitListeners_.push_back(callback);
}

void EventManager::Dispatch(const PlayerHitEvent& event) {
    // 登録順にその場で通知する。通知中に ClearAllListeners() を呼ばない。
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
    // ダメージ処理の購読を発生順に保持する。
    damageListeners_.push_back(callback);
}

void EventManager::Dispatch(const DamageEvent& event) {
    // 命中側が渡したイベントをその場で登録順に配る。
    for (auto& listener : damageListeners_) {
        listener(event);
    }
}

void EventManager::Subscribe(std::function<void(const PlayerDeathEvent&)> callback) {
    // プレイヤー死亡時の処理をシーン単位で登録する。
    playerDeathListeners_.push_back(callback);
}

void EventManager::Dispatch(const PlayerDeathEvent& event) {
    // 死亡確定後に登録済みの通知先を順番に呼ぶ。
    for (auto& listener : playerDeathListeners_) {
        listener(event);
    }
}

void EventManager::Subscribe(std::function<void(const PlayerJumpEvent&)> callback) {
    // ジャンプ発生時の効果やルール処理を登録する。
    playerJumpListeners_.push_back(callback);
}

void EventManager::Dispatch(const PlayerJumpEvent& event) {
    // ジャンプ通知は待ち行列に積まず、同じフレームで処理する。
    for (auto& listener : playerJumpListeners_) {
        listener(event);
    }
}

// シーン切り替えで古いオブジェクトを指すコールバックを残さないよう、購読をまとめて解除します。
void EventManager::ClearAllListeners() {
    // シーン破棄後のオブジェクトをコールバックが参照しないよう、登録をまとめて外す。
    playerHitListeners_.clear();
    bulletHitListeners_.clear();
    damageListeners_.clear();
    playerDeathListeners_.clear();
    playerJumpListeners_.clear();
}
