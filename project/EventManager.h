#pragma once
#include "Event.h"  // 作成したイベント構造体をインクルード
#include <list>
#include <functional> // std::function のため

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



private:
    EventManager() = default;
    ~EventManager() = default;
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

private:
    // PlayerHitEvent のリスナーリスト
    std::list<std::function<void(const PlayerHitEvent&)>> playerHitListeners_;
};