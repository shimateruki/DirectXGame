#pragma once

#include "ObjectComponent.h"

/// <summary>イベント送受信に使う永続的な接続IDを保持する。</summary>
class GameplayLinkComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "GameplayLink";

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // 発火元と接続先のID。-1は未設定。
    int GetEventId() const { return eventId_; }
    void SetEventId(int id) { eventId_ = id; }

    int GetTargetId() const { return targetId_; }
    void SetTargetId(int id) { targetId_ = id; }

private:
    int eventId_ = -1;
    int targetId_ = -1;
};
