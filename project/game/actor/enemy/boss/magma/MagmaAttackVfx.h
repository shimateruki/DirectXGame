#pragma once

#include "game/actor/enemy/combat/EnemyAttackProfile.h"

#include <array>

namespace MagmaAttackVfx {

// 再生と先読みで、同じ攻撃設定と空欄時の既定プリセットを使う。
struct ActivePreset {
    // attacks.json 内で技を特定するID。
    const char* attackId;
    // 旧設定や空欄でも従来の見た目を使うためのGPU粒子プリセット名。
    const char* defaultName;

    // 設定が使える場合は、その攻撃定義が保持するプリセット名を返す。
    const char* Resolve(const EnemyAttackDefinition* attack) const {
        if (!attack || attack->activeVfx.empty() || attack->activeVfx.find_first_of(R"(/\:)") != std::string::npos) {
            return defaultName;
        }
        return attack->activeVfx.c_str();
    }
};

inline constexpr ActivePreset kMortar{"magma_mortar", "magma_slime_mortar_trail"};
inline constexpr ActivePreset kFireColumn{"fire_column", "magma_slime_fire_column_stream"};
inline constexpr ActivePreset kFlood{"magma_flood", "magma_slime_flood_splash"};
inline constexpr std::array kProfilePresets{kMortar, kFireColumn, kFlood};

} // namespace MagmaAttackVfx
