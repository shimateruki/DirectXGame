#pragma once

// ゲーム固有の登録入口：RegisterGameEditorEvents。登録先の共通Editorに、個別の型名や分岐を持ち込まない。

#include "EditorEventRegistry.h"
#include "CollisionConfig.h"
#include "game/actor/Event.h"

#include <cassert>

/// イベントの保存番号・表示名・選択時の設定をゲーム側で一度だけ登録します。
inline void RegisterGameEditorEvents() {
    // 旧JSONの0～7との対応を固定します。表示順を変えても保存番号は変わりません。
    static_assert(static_cast<int>(EventType::None) == 0);
    static_assert(static_cast<int>(EventType::Damage) == 1);
    static_assert(static_cast<int>(EventType::Warp) == 2);
    static_assert(static_cast<int>(EventType::MovieBridge) == 3);
    static_assert(static_cast<int>(EventType::Checkpoint) == 4);
    static_assert(static_cast<int>(EventType::Goal) == 5);
    static_assert(static_cast<int>(EventType::StageSelect) == 6);
    static_assert(static_cast<int>(EventType::StarCoin) == 7);

    static const bool registered = EditorEventRegistry::Register({
        { static_cast<int>(EventType::None), "なし" },
        { static_cast<int>(EventType::Damage), "ダメージ" },
        { static_cast<int>(EventType::Warp), "ワープ" },
        { static_cast<int>(EventType::MovieBridge), "映像演出 (橋落ち)" },
        { static_cast<int>(EventType::Checkpoint), "中間地点 (Checkpoint)" },
        { static_cast<int>(EventType::Goal), "ゴール", [](engine::Object3D& object) {
            // 従来と同様、Goalへ変更した時だけ接触属性を設定します。
            // 配置済みのColliderの大きさ・形状は保持し、判定なしの場合だけ球を補います。
            object.SetCollisionAttribute(CollisionCategories::Get("trigger"));
            object.SetCollisionMask(CollisionCategories::Get("player"));
            if (object.GetColliderType() == ColliderType::None) {
                engine::Object3D::ColliderConfig config;
                config.type = ColliderType::Sphere;
                config.size = { 1.2f, 1.2f, 1.2f };
                object.SetColliderConfig(config);
                object.SetCollisionRadius(1.2f);
            }
        } },
        { static_cast<int>(EventType::StageSelect), "ステージセレクト" },
        { static_cast<int>(EventType::StarCoin), "スターコイン (StarCoin)" },
    });
    assert(registered && "Event registration failed");
    (void)registered;
}
