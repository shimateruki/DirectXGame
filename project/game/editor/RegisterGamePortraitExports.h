#pragma once

// ゲーム固有の登録入口：RegisterGamePortraitExports。登録先の共通Editorに、個別の型名や分岐を持ち込まない。
#include "engine/editor/assets/PortraitExportRegistry.h"

/// <summary>HUDで使用する敵Presetと撮影姿勢・保存名をゲーム側から登録する。</summary>
inline void RegisterGamePortraitExports() {
    constexpr const char* kDirectory = "Resources/sprite/ui/hud/morph_icons";
    // 現在の6種類、順番、撮影倍率、通常・被弾PNGの名前を維持する。
    engine::PortraitExportRegistry::Register({
        {"Builtin/Enemy/Slime", kDirectory, "slime", 0.0f, -0.12f, 1.08f, 0.0f},
        {"Builtin/Enemy/Bomber", kDirectory, "bomber", 0.0f, -0.12f, 1.08f, 0.0f},
        {"Builtin/Enemy/GiantSlime", kDirectory, "giant_slime", 0.0f, -0.12f, 1.24f, 0.0f},
        {"Builtin/Enemy/FireSlime", kDirectory, "fire_slime", 0.0f, -0.12f, 1.08f, 0.0f},
        {"Builtin/Enemy/ThunderSlime", kDirectory, "thunder_slime", 0.0f, -0.12f, 1.08f, 0.0f},
        {"Builtin/Enemy/WindSlime", kDirectory, "wind_slime", 0.0f, -0.12f, 1.08f, 0.0f},
    });
}
