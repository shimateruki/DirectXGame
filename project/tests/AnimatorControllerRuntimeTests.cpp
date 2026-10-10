#include "engine/animation/AnimatorController.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Check(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
}

int main() {
    try {
        AnimatorControllerAsset asset;
        Check(asset.Load("Resources/json/animator/player_slime.json"), "player_slime.json could not be loaded");

        AnimatorControllerRuntime runtime;
        runtime.SetController(&asset);
        Check(runtime.IsPlayingState("Idle"), "entry state should be Idle");

        constexpr std::array<const char*, 10> kModeStates = {
            "Idle", "Run", "Jump", "Dash", "AimHook", "ShootHook", "PullEnemy", "Carry", "GateReturn", "Disabled"
        };
        for (int mode = 1; mode < static_cast<int>(kModeStates.size()); ++mode) {
            runtime.SetInt("LocomotionMode", mode);
            runtime.Update(1.0f / 60.0f, {});
            Check(runtime.IsPlayingState(kModeStates[mode]), "LocomotionMode did not select the expected state");
            Check(runtime.GetInt("LocomotionMode") == mode, "runtime parameter was lost");
        }

        runtime.SetInt("LocomotionMode", 0);
        runtime.Update(1.0f / 60.0f, {});
        Check(runtime.IsPlayingState("Idle"), "Idle return transition failed");
        Check(runtime.IsTransitioning(), "transition duration was not applied");
        runtime.Update(0.05f, {});
        const float weight = runtime.GetTransitionWeight();
        Check(weight > 0.0f && weight < 1.0f, "blend weight should be between old and new poses");
        runtime.Update(0.5f, {});
        Check(!runtime.IsTransitioning(), "transition did not complete");

        // 能力Stateの参照先は、実行時に読み込めるBody Clipでなければならない。
        for (const AnimatorStateDefinition& state : asset.states) {
            if (state.name.rfind("player_", 0) != 0) {
                continue;
            }
            Check(!state.bodyClipName.empty(), "ability state has no Body Clip");
            Check(std::filesystem::exists(
                std::filesystem::path("Resources/json/animation_clip") / (state.bodyClipName + ".json")),
                "ability state refers to a missing Body Clip");
        }

        std::cout << "Animator Controller runtime and player asset: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
