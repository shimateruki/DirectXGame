#pragma once

#include "ObjectComponent.h"

// GameplayLinkComponentは、イベント送受信に使う永続的な接続IDを所有します。
class GameplayLinkComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "GameplayLink";

    std::string_view GetTypeId() const override { return kTypeId; }

    int GetEventId() const { return eventId_; }
    void SetEventId(int id) { eventId_ = id; }

    int GetTargetId() const { return targetId_; }
    void SetTargetId(int id) { targetId_ = id; }

private:
    int eventId_ = -1;
    int targetId_ = -1;
};
