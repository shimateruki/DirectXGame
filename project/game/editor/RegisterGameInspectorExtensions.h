#pragma once

// ゲーム固有の登録入口：RegisterGameInspectorExtensions。登録先の共通Editorに、個別の型名や分岐を持ち込まない。
#ifdef USE_IMGUI

#include "engine/editor/properties/InspectorExtensionRegistry.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/actor/player/Player.h"
#include "game/actor/item/DroppedCopyStar.h"
#include "imgui.h"

// 専用UIと保存方式はゲームが決める。通常PropertyやScene保存へ二重登録しない。
inline void RegisterGameInspectorExtensions() {
    using Registry = engine::InspectorExtensionRegistry;
    using Slot = Registry::Slot;
    Registry::Register<EnemyFalseKingSlime>("FalseKingFinale", Slot::Gameplay,
        [](EnemyFalseKingSlime& king) { king.DrawFinaleEditor(); });
    Registry::Register<Player>("PlayerWhip", Slot::Gameplay,
        [](Player& player) { player.DrawWhipEditor(); });

    // 星は選択中の実体だけを調整する。寿命・点滅の保存やReplayの仕様は変更しない。
    Registry::Register<DroppedCopyStar>("DroppedCopyStar", Slot::Item, [](DroppedCopyStar& star) {
        ImGui::TextUnformatted("コピー解除で落とした星（一度だけ取得）");
        float lifetime = star.GetLifetime();
        float blink = star.GetBlinkDuration();
        if (ImGui::SliderFloat("消滅までの秒数", &lifetime, 1.0f, 60.0f, "%.1f 秒"))
            star.SetLifetime(lifetime);
        if (ImGui::SliderFloat("点滅を始める残り秒数", &blink, 0.0f, lifetime, "%.1f 秒"))
            star.SetBlinkDuration(blink);
        ImGui::Text("経過: %.1f 秒", star.GetElapsed());
        ImGui::TextUnformatted("この調整は選択中の星だけに適用されます。");
    });
}
#endif
