#pragma once
#include "Event.h"  
#include <list>
#include <functional>

class EventManager {
public:
    static EventManager* GetInstance();

    // --- PlayerHitEvent 関連 ---

    /// <summary>
    /// PlayerHitEvent のリスナー（購読者）を登録する
    /// </summary>
    void Subscribe(std::function<void(const PlayerHitEvent&)> callback);

    /// <summary>
    /// PlayerHitEvent を発行（ディスパッチ）する
    /// </summary>
    void Dispatch(const PlayerHitEvent& event);

    /// <summary>
    /// BulletHitEvent のリスナー（購読者）を登録する
    /// </summary>
    void Subscribe(std::function<void(const BulletHitEvent&)> callback);

    /// <summary>
    /// BulletHitEvent を発行（ディスパッチ）する
    /// </summary>
    void Dispatch(const BulletHitEvent& event);

    /// <summary>
    /// DamageEvent のリスナー（購読者）を登録する
    /// </summary>
    void Subscribe(std::function<void(const DamageEvent&)> callback);

    /// <summary>
    /// DamageEvent を発行（ディスパッチ）する
    /// </summary>
    void Dispatch(const DamageEvent& event);
    
    /// <summary>
    /// PlayerDeathEvent のリスナー（購読者）を登録する
    /// </summary>
    void Subscribe(std::function<void(const PlayerDeathEvent&)> callback);

    /// <summary>
    /// PlayerDeathEvent を発行（ディスパッチ）する
    /// </summary>
    void Dispatch(const PlayerDeathEvent& event);
    
    /// <summary>
    /// PlayerJumpEvent のリスナー（購読者）を登録する
    /// </summary>
    void Subscribe(std::function<void(const PlayerJumpEvent&)> callback);
    
    /// <summary>
    /// PlayerJumpEvent を発行（ディスパッチ）する
    /// </summary>
    void Dispatch(const PlayerJumpEvent& event);

    void ClearAllListeners();
private:
    EventManager() = default;
    ~EventManager() = default;
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

private:
    // PlayerHitEvent のリスナーリスト
    std::list<std::function<void(const PlayerHitEvent&)>> playerHitListeners_;

    std::list<std::function<void(const BulletHitEvent&)>> bulletHitListeners_;
    std::list<std::function<void(const DamageEvent&)>> damageListeners_;
    std::list<std::function<void(const PlayerDeathEvent&)>> playerDeathListeners_;
    std::list<std::function<void(const PlayerJumpEvent&)>> playerJumpListeners_;
};