#include "EventManager.h"
#include <utility>

EventManager* EventManager::GetInstance() {
    static EventManager instance;
    return &instance;
}

template <typename Event>
EventManager::SubscriptionId EventManager::SubscribeTo(
    ListenerList<Event>& listeners, std::function<void(const Event&)> callback) {
    if (!callback) return kInvalidSubscriptionId;
    const SubscriptionId id = nextSubscriptionId_++;
    listeners.push_back({id, std::move(callback), true});
    return id;
}

template <typename Event>
void EventManager::DispatchTo(ListenerList<Event>& listeners, const Event& event) {
    // 配信中に追加された購読は次のイベントから通知します。
    const SubscriptionId lastExistingId = nextSubscriptionId_ - 1;
    ++dispatchDepth_;
    try {
        for (auto& listener : listeners) {
            if (listener.active && listener.id <= lastExistingId) listener.callback(event);
        }
    } catch (...) {
        --dispatchDepth_;
        if (dispatchDepth_ == 0) PruneInactiveListeners();
        throw;
    }
    --dispatchDepth_;
    if (dispatchDepth_ == 0) PruneInactiveListeners();
}

template <typename Event>
void EventManager::MarkInactive(ListenerList<Event>& listeners, SubscriptionId id) {
    for (auto& listener : listeners) {
        if (listener.id == id) {
            listener.active = false;
            return;
        }
    }
}

template <typename Event>
void EventManager::PruneInactive(ListenerList<Event>& listeners) {
    listeners.remove_if([](const Listener<Event>& listener) { return !listener.active; });
}

void EventManager::PruneInactiveListeners() {
    PruneInactive(playerHitListeners_);
    PruneInactive(bulletHitListeners_);
    PruneInactive(damageListeners_);
    PruneInactive(playerDeathListeners_);
    PruneInactive(playerJumpListeners_);
}

EventManager::SubscriptionId EventManager::Subscribe(std::function<void(const PlayerHitEvent&)> callback) {
    return SubscribeTo(playerHitListeners_, std::move(callback));
}

void EventManager::Dispatch(const PlayerHitEvent& event) {
    DispatchTo(playerHitListeners_, event);
}

EventManager::SubscriptionId EventManager::Subscribe(std::function<void(const BulletHitEvent&)> callback) {
    return SubscribeTo(bulletHitListeners_, std::move(callback));
}

void EventManager::Dispatch(const BulletHitEvent& event) {
    DispatchTo(bulletHitListeners_, event);
}

EventManager::SubscriptionId EventManager::Subscribe(std::function<void(const DamageEvent&)> callback) {
    return SubscribeTo(damageListeners_, std::move(callback));
}

void EventManager::Dispatch(const DamageEvent& event) {
    DispatchTo(damageListeners_, event);
}

EventManager::SubscriptionId EventManager::Subscribe(std::function<void(const PlayerDeathEvent&)> callback) {
    return SubscribeTo(playerDeathListeners_, std::move(callback));
}

void EventManager::Dispatch(const PlayerDeathEvent& event) {
    DispatchTo(playerDeathListeners_, event);
}

EventManager::SubscriptionId EventManager::Subscribe(std::function<void(const PlayerJumpEvent&)> callback) {
    return SubscribeTo(playerJumpListeners_, std::move(callback));
}

void EventManager::Dispatch(const PlayerJumpEvent& event) {
    DispatchTo(playerJumpListeners_, event);
}

void EventManager::Unsubscribe(SubscriptionId id) {
    if (id == kInvalidSubscriptionId) return;
    MarkInactive(playerHitListeners_, id);
    MarkInactive(bulletHitListeners_, id);
    MarkInactive(damageListeners_, id);
    MarkInactive(playerDeathListeners_, id);
    MarkInactive(playerJumpListeners_, id);
    if (dispatchDepth_ == 0) PruneInactiveListeners();
}

void EventManager::ClearAllListeners() {
    for (auto& listener : playerHitListeners_) listener.active = false;
    for (auto& listener : bulletHitListeners_) listener.active = false;
    for (auto& listener : damageListeners_) listener.active = false;
    for (auto& listener : playerDeathListeners_) listener.active = false;
    for (auto& listener : playerJumpListeners_) listener.active = false;
    if (dispatchDepth_ == 0) PruneInactiveListeners();
}
