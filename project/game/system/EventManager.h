#pragma once
#include "Event.h"
#include <cstdint>
#include <functional>
#include <list>

// ダメージ、弾ヒット、死亡などのイベントを同期的に配信する。
// 購読IDで個別解除でき、購読者の破棄時にも解除できる。
class EventManager {
  public:
    using SubscriptionId = uint64_t;
    static constexpr SubscriptionId kInvalidSubscriptionId = 0;
    /// 同期イベントの購読と配信を共有するインスタンスを返す。
    static EventManager* GetInstance();

    // --- PlayerHitEvent 関連 ---
    SubscriptionId Subscribe(std::function<void(const PlayerHitEvent&)> callback);
    void Dispatch(const PlayerHitEvent& event);

    // --- BulletHitEvent 関連 ---
    SubscriptionId Subscribe(std::function<void(const BulletHitEvent&)> callback);
    void Dispatch(const BulletHitEvent& event);

    // --- DamageEvent 関連 ---
    SubscriptionId Subscribe(std::function<void(const DamageEvent&)> callback);
    void Dispatch(const DamageEvent& event);

    // --- PlayerDeathEvent 関連 ---
    SubscriptionId Subscribe(std::function<void(const PlayerDeathEvent&)> callback);
    void Dispatch(const PlayerDeathEvent& event);

    // --- PlayerJumpEvent 関連 ---
    SubscriptionId Subscribe(std::function<void(const PlayerJumpEvent&)> callback);
    void Dispatch(const PlayerJumpEvent& event);

    /// <summary>指定した購読だけを無効化します。配信中の解除も次の呼び出しに反映します。</summary>
    void Unsubscribe(SubscriptionId id);
    // 全購読を解除する必要がある場合に使用する。
    void ClearAllListeners();

  private:
    EventManager() = default;
    ~EventManager() = default;
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

  private:
    template <typename Event> struct Listener {
        SubscriptionId id = kInvalidSubscriptionId;
        std::function<void(const Event&)> callback;
        bool active = true;
    };
    template <typename Event> using ListenerList = std::list<Listener<Event>>;

    /// リスナーへ個別解除用 ID を付けて登録する。
    template <typename Event>
    SubscriptionId SubscribeTo(ListenerList<Event>& listeners, std::function<void(const Event&)> callback);
    /// 同種イベントの有効なリスナーへ同期配信する。
    template <typename Event> void DispatchTo(ListenerList<Event>& listeners, const Event& event);
    /// 配信中でも安全に個別 ID を無効化する。
    template <typename Event> void MarkInactive(ListenerList<Event>& listeners, SubscriptionId id);
    /// 無効化済みリスナーを一覧から除去する。
    template <typename Event> void PruneInactive(ListenerList<Event>& listeners);
    /// 全イベント種別の無効化済みリスナーを整理する。
    void PruneInactiveListeners();

    ListenerList<PlayerHitEvent> playerHitListeners_;
    ListenerList<BulletHitEvent> bulletHitListeners_;
    ListenerList<DamageEvent> damageListeners_;
    ListenerList<PlayerDeathEvent> playerDeathListeners_;
    ListenerList<PlayerJumpEvent> playerJumpListeners_;
    SubscriptionId nextSubscriptionId_ = 1;
    int dispatchDepth_ = 0;
};
