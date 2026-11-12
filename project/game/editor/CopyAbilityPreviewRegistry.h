#pragma once

#include "game/actor/player/copy/PlayerCopyAbilityController.h"
#include <cmath>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Player;

// プレビューの入力合成に使う一フレームの情報。時間値はすべて秒単位。
struct CopyAbilityPreviewFrame {
    float elapsed = 0.0f;
    float deltaTime = 0.0f;
    float holdTime = 1.5f;
    float startTime = 0.5f; // 操作を発火させるプレビュー開始からの秒数。
    bool triggered = false;

    bool IsHeld() const { return elapsed >= startTime && elapsed < startTime + holdTime; }
    // 前フレームでは未到達で、今回到達した時刻だけを検出する。保持判定とは分ける。
    bool Crossed(float time) const { return elapsed >= time && elapsed - deltaTime < time; }
};

// buildInput は毎フレームの疑似入力、onTrigger は開始時の追加操作を定義する。
struct CopyAbilityPreviewAction {
    std::string id;
    std::string displayName;
    bool startsUncopied = false; // コピー前のPlayerから開始して、取得過程も確認するか。
    float startTime = 0.5f;
    std::function<PlayerCopyAbilityInput(const CopyAbilityPreviewFrame&)> buildInput;
    std::function<void(Player&, const std::string&)> onTrigger;
};

// プレビューの技・確認時刻・距離をまとめたジャンプ先。能力の保存設定とは別に扱う。
struct CopyAbilityPreviewBookmark {
    std::string displayName;
    std::string actionId;
    float time = 0.0f;
    float targetDistance = 0.0f; // 0なら現在の距離を維持します。
};

// コピー形態とProfileの敵IDは別々に指定できます。保存用の列挙値は変更しません。
struct CopyAbilityPreviewDescriptor {
    std::string type;
    std::string displayName;
    std::string profileType;
    std::string description;
    std::string profileActionId;
    std::string bookmarkHelp;
    std::vector<CopyAbilityPreviewAction> actions;
    std::vector<CopyAbilityPreviewBookmark> bookmarks;

    const CopyAbilityPreviewAction* FindAction(const std::string& id) const {
        for (const auto& action : actions) if (action.id == id) return &action;
        return nullptr;
    }
};

// 関連：ゲーム側の登録関数とActorDebugPanelをつなぎ、能力入力とブックマークを保持する。
// 主な入口：Register / Find
class CopyAbilityPreviewRegistry {
public:
    // 起動時に登録します。型や操作の重複を検証してから一括で追加します。
    static bool Register(std::vector<CopyAbilityPreviewDescriptor> entries) {
        std::unordered_set<std::string> types;
        for (const auto& entry : Entries()) types.insert(entry.type);
        for (const auto& entry : entries) {
            if (entry.type.empty() || entry.displayName.empty() || entry.profileType.empty() ||
                entry.actions.empty() || !types.insert(entry.type).second) return false;
            std::unordered_set<std::string> ids;
            for (const auto& action : entry.actions)
                if (action.id.empty() || action.displayName.empty() || !std::isfinite(action.startTime) ||
                    action.startTime < 0.0f || !ids.insert(action.id).second) return false;
            if (!entry.profileActionId.empty() && !entry.FindAction(entry.profileActionId)) return false;
            for (const auto& bookmark : entry.bookmarks)
                if (!entry.FindAction(bookmark.actionId) || !std::isfinite(bookmark.time) || bookmark.time < 0.0f)
                    return false;
        }
        for (auto& entry : entries) Entries().push_back(std::move(entry));
        return true;
    }

    static const std::vector<CopyAbilityPreviewDescriptor>& GetEntries() { return Entries(); }
    // 未登録の種類は nullptr。返す定義は内部配列の借用参照で、追加入力登録後は取得し直す。
    static const CopyAbilityPreviewDescriptor* Find(const std::string& type) {
        for (const auto& entry : Entries()) if (entry.type == type) return &entry;
        return nullptr;
    }

private:
    static std::vector<CopyAbilityPreviewDescriptor>& Entries() {
        static std::vector<CopyAbilityPreviewDescriptor> entries;
        return entries;
    }
};
