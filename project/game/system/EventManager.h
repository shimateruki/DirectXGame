#pragma once
#include "Event.h"
#include <functional>
#include <list>

// ダメージ、弾ヒット、死亡などのイベントを同期的に配信する。
// Subscribe は解除ハンドルを返さないため、シーン破棄前に ClearAllListeners() が必要。
class EventManager {
  public:
    static EventManager* GetInstance();

    // --- PlayerHitEvent 関連 ---
    void Subscribe(std::function<void(const PlayerHitEvent&)> callback);
    void Dispatch(const PlayerHitEvent& event);

    // --- BulletHitEvent 関連 ---
    void Subscribe(std::function<void(const BulletHitEvent&)> callback);
    void Dispatch(const BulletHitEvent& event);

    // --- DamageEvent 関連 ---
    void Subscribe(std::function<void(const DamageEvent&)> callback);
    void Dispatch(const DamageEvent& event);

    // --- PlayerDeathEvent 関連 ---
    void Subscribe(std::function<void(const PlayerDeathEvent&)> callback);
    void Dispatch(const PlayerDeathEvent& event);

    // --- PlayerJumpEvent 関連 ---
    void Subscribe(std::function<void(const PlayerJumpEvent&)> callback);
    void Dispatch(const PlayerJumpEvent& event);

    // シーンを参照するコールバックが無効になる前に、全購読を解除する。
    void ClearAllListeners();

  private:
    EventManager() = default;
    ~EventManager() = default;
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

  private:
    std::list<std::function<void(const PlayerHitEvent&)>> playerHitListeners_;
    std::list<std::function<void(const BulletHitEvent&)>> bulletHitListeners_;
    std::list<std::function<void(const DamageEvent&)>> damageListeners_;
    std::list<std::function<void(const PlayerDeathEvent&)>> playerDeathListeners_;
    std::list<std::function<void(const PlayerJumpEvent&)>> playerJumpListeners_;
};
