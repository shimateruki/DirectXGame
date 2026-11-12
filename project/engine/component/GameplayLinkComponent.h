#pragma once

#include "ObjectComponent.h"

// 担当：接触イベントの発火元IDと接続先IDを保持する。
// 関連：ObjectComponent（共通型）、Object3D（保持・設定の窓口）
// 主な入口：GetEventId / SetEventId / GetTargetId / SetTargetId
class GameplayLinkComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "GameplayLink"; // 保存と実行時の型判定に使う固定ID。

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // 発火元と接続先のID。-1は未設定。
    int GetEventId() const { return eventId_; }

    void SetEventId(int id) { eventId_ = id; }

    int GetTargetId() const { return targetId_; }

    void SetTargetId(int id) { targetId_ = id; }

private:
    int eventId_ = -1; // 発火元ID。-1は未設定。
    int targetId_ = -1; // 接続先ID。-1は未設定。
};
