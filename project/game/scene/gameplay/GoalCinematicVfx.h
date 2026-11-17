#pragma once

#include <array>
#include <string_view>

namespace GoalCinematicVfx {

// トラック補完と再生開始で共有する素材名・対象。
struct Cue {
    std::string_view prefix;
    const char* label;
    const char* gold;
    const char* silver;
    bool followsCrown;
};

inline constexpr Cue kFocus{"crown_focus", "Crown Focus VFX", "shared/crown_focus_cue", "shared/crown_focus_silver_cue", true};
inline constexpr Cue kLanding{"crown_get", "Crown Landing VFX", "shared/crown_get_cue", "shared/crown_get_silver_cue", true};
inline constexpr Cue kResult{"crown_result", "Result Burst VFX", "shared/crown_result_cue", "shared/crown_result_silver_cue", false};
inline constexpr Cue kVictory{"crown_victory_land", "Victory Landing VFX", "shared/crown_victory_land_cue",
                              "shared/crown_victory_land_silver_cue", false};
inline constexpr std::array kCues{&kFocus, &kLanding, &kResult, &kVictory};

inline const Cue* FindCue(std::string_view sequenceName) {
    for (const auto* cue : kCues) {
        if (sequenceName.starts_with(cue->prefix)) {
            return cue;
        }
    }
    return nullptr;
}

} // namespace GoalCinematicVfx
