#pragma once

// ゲーム固有の登録入口：RegisterGameCopyPreviews。登録先の共通Editorに、個別の型名や分岐を持ち込まない。

#include "game/editor/CopyAbilityPreviewRegistry.h"
#include "Player.h"
#include "game/actor/player/copy/CrownMeleeSettings.h"
#include <cassert>

namespace GameCopyPreviewDetail {
// 入力を実際の能力へ渡します。攻撃本体・能力状態の更新処理は作り直しません。
inline std::vector<CopyAbilityPreviewAction> CommonActions() {
    CopyAbilityPreviewAction acquire{"acquire", "コピー開始", true};
    acquire.onTrigger = [](Player& player, const std::string& type) { player.ApplyStoredCopy(type); };
    CopyAbilityPreviewAction primary{"primary", "左クリック"};
    primary.buildInput = [](const CopyAbilityPreviewFrame& frame) {
        PlayerCopyAbilityInput input{};
        input.primaryTriggered = input.primaryHeld = frame.triggered;
        return input;
    };
    CopyAbilityPreviewAction hold{"primary_hold", "左長押し → 離す"};
    hold.buildInput = [](const CopyAbilityPreviewFrame& frame) {
        PlayerCopyAbilityInput input{};
        input.primaryTriggered = frame.triggered;
        input.primaryHeld = frame.IsHeld();
        return input;
    };
    CopyAbilityPreviewAction secondary{"secondary", "右クリック / ガード"};
    secondary.buildInput = [](const CopyAbilityPreviewFrame& frame) {
        PlayerCopyAbilityInput input{};
        input.secondaryTriggered = frame.triggered;
        input.secondaryHeld = frame.IsHeld();
        return input;
    };
    CopyAbilityPreviewAction special{"special", "E 技"};
    special.buildInput = [](const CopyAbilityPreviewFrame& frame) {
        PlayerCopyAbilityInput input{};
        input.specialTriggered = frame.triggered;
        return input;
    };
    CopyAbilityPreviewAction release{"release", "コピー解除"};
    release.onTrigger = [](Player& player, const std::string&) { player.ReleaseCopyPreview(); };
    return {std::move(acquire), std::move(primary), std::move(hold), std::move(secondary),
        std::move(special), std::move(release), {"idle", "待機・常駐エフェクト"}};
}
inline CopyAbilityPreviewDescriptor Make(const char* type, const char* label,
    const char* profile, const char* description) {
    CopyAbilityPreviewDescriptor entry;
    entry.type=type; entry.displayName=label; entry.profileType=profile;
    entry.description=description;
    entry.profileActionId="special";
    entry.actions=CommonActions();
    return entry;
}
} // namespace GameCopyPreviewDetail

inline void RegisterGameCopyPreviews() {
    static const bool registered=[] {
        using namespace GameCopyPreviewDetail;
        auto pink=Make("Slime","ピンク","Slime",
            "左: 追加クリックで3段打撃 / 右: 身を縮めるガード / E: 跳ね上がり突進");
        CopyAbilityPreviewAction combo{"pink_combo", "ピンク: 3段コンボ"};
        combo.buildInput=[](const CopyAbilityPreviewFrame& frame) {
            PlayerCopyAbilityInput input{};
            input.primaryTriggered=frame.triggered || frame.Crossed(0.90f) || frame.Crossed(1.55f);
            return input;
        };
        pink.actions.push_back(std::move(combo));
        pink.bookmarks={
            {"ガード中を確認","secondary",0.95f}, {"Eの跳躍を確認","special",0.88f},
            {"近接1段目: 引きを確認","primary",0.66f,2.4f},
            {"近接1段目: 打点を確認","primary",0.80f,2.4f},
            {"近接2段目: 打点を確認","pink_combo",1.48f,2.4f},
            {"近接3段目: プレスを確認","pink_combo",2.26f,2.4f}
        };
        pink.bookmarkHelp="追加クリックで次の段へ。3段コンボは実入力を3回送って確認します。打点を確認で停止できます。命中専用演出はダミーへのダメージ成立時のみです。";
        std::vector<CopyAbilityPreviewDescriptor> entries;
        entries.push_back(std::move(pink));
        entries.push_back(Make("FireSlime","炎","FireSlime","左長押し: 炎ブレス / 右: 炎ダッシュ / E: 火球"));
        entries.push_back(Make("ThunderSlime","雷","ThunderSlime",
            "左長押し: 周囲に帯電 / 離す: 電撃弾（1秒で最大） / 右: 雷移動 / E: 雷の直線攻撃"));
        entries.push_back(Make("WindSlime","風","WindSlime",
            "左長押し: 竜巻攻撃（最大1.6秒） / 右: 風跳び（地上発動・頂点から緩降下） / E: 小型竜巻"));
        entries.push_back(Make("Bomber","ボム","Bomber","左: 設置ボム / 右: 爆風ジャンプ / E: ボム投擲"));
        auto crown = Make("Crown", "覇王", "FalseKingSlime",
            "左を溜めて離す: ゲート槍1→3→5本 / 最大溜め: 中央槍強化 / 右: ゲート回避 / Eを追加: 近接3段");
        CopyAbilityPreviewAction crownCombo{"crown_combo", "覇王: 近接3段コンボ"};
        crownCombo.buildInput = [](const CopyAbilityPreviewFrame& frame) {
            PlayerCopyAbilityInput input{};
            const auto& settings = CrownMeleeSettings::Get();
            const float first = (std::max)(settings.queueStart + 0.01f, settings.duration[0] * 0.72f);
            const float second = (std::max)(settings.queueStart + 0.01f, settings.duration[1] * 0.72f);
            input.specialTriggered = frame.triggered
                || frame.Crossed(frame.startTime + first)
                || frame.Crossed(frame.startTime + settings.duration[0] + second);
            return input;
        };
        crown.actions.push_back(std::move(crownCombo));
        crown.bookmarkHelp = "Eを押すたびに次段を1回予約。右クリックは各段の後隙から回避できます。近接設定とBody Clipを調整できます。";
        entries.push_back(std::move(crown));
        return CopyAbilityPreviewRegistry::Register(std::move(entries));
    }();
    assert(registered && "copy preview registration failed");
    (void)registered;
}
