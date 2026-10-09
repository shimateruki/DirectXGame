#pragma once

#ifdef USE_IMGUI
#include "SceneManager.h"
#include "MeshEffectManager.h"
#include "WinApp.h"
#include <Windows.h>
#include <fstream>
#include <string>
#include "AttackLifecycleRuntimeProbe.h"
#include "AttackEditorRuntimeProbe.h"

// 明示的に環境変数を設定した実行だけで、実際の更新・GPU描画を通して遷移を検証します。
// 通常起動ではシーン選択、エフェクト、保存データに介入しません。
namespace SceneTransitionRuntimeProbe {
inline const std::string& LogPath() {
    static const std::string path = [] {
        char value[2048]{};
        const auto length = GetEnvironmentVariableA("CG2_SCENE_TRANSITION_TEST_LOG", value, sizeof(value));
        return length && length < sizeof(value) ? std::string(value) : std::string{};
    }();
    return path;
}
inline bool Enabled() { return !LogPath().empty(); }
inline void AfterDraw(SceneManager* manager, DebugEditor* editor = nullptr) {
    if (!Enabled() || !manager) return;
    if (AttackEditorRuntimeProbe::Enabled()) { AttackEditorRuntimeProbe::AfterDraw(manager, editor, LogPath()); return; }
    if (AttackLifecycleRuntimeProbe::Enabled()) { AttackLifecycleRuntimeProbe::AfterDraw(manager, LogPath()); return; }
    static std::ofstream log(LogPath(), std::ios::trunc);
    static ULONGLONG phaseStarted = GetTickCount64();
    static int step = 0;
    static int frames = 0;
    static bool finished = false;
    static uint64_t generation = 0;
    if (finished) return;
    auto finish = [&](const char* message) {
        log << message << std::endl;
        finished = true;
        // 検証専用プロセスなので保存確認UIを挟まず、通常のFinalizeへ進めます。
        WinApp::CloseNow();
    };
    // 複数回の実アセット読込を合算せず、各遷移が進まなくなった場合を検出します。
    if (GetTickCount64() - phaseStarted > 180000) { finish("FAIL: transition timeout"); return; }
    if (manager->IsTransitioning()) return;
    const char* scenes[] = {"TITLE", "GAMEPLAY", "PREVIEW", "GAMEPLAY", "GAMEPLAY"};
    if (manager->GetCurrentSceneName() != scenes[step]) { finish("FAIL: unexpected scene"); return; }
    if (frames == 0) {
        if (generation == manager->GetSceneGeneration()) return;
        generation = manager->GetSceneGeneration();
        log << "ENTER " << step << ' ' << scenes[step] << " generation=" << generation << std::endl;
        for (const auto& effect : MeshEffectManager::GetInstance()->GetActiveEffects()) {
            if (effect && effect->GetName() == "SceneTransitionLifetimeProbe") {
                finish("FAIL: previous scene effect survived its owner"); return;
            }
        }
        // 遷移時にも生存する長寿命エフェクトで、描画基盤への古い参照を検出します。
        MeshEffectManager::GetInstance()->SpawnPortalEffect({0, 1, 0}, 120.0f);
        if (MeshEffectManager::GetInstance()->GetActiveEffects().empty()) {
            finish("FAIL: mesh effect was not created"); return;
        }
        MeshEffectManager::GetInstance()->GetActiveEffects().back()->SetName("SceneTransitionLifetimeProbe");
    }
    if (++frames < 120) return;
    log << "DRAWN " << scenes[step] << " frames=" << frames
        << " activeMesh=" << MeshEffectManager::GetInstance()->GetActiveEffects().size() << std::endl;
    if (++step == 5) { finish("PASS: loading transitions and direct reload rendered successfully"); return; }
    frames = 0;
    phaseStarted = GetTickCount64();
    if (step == 4) manager->ChangeSceneWithoutLoading(scenes[step]);
    else manager->ChangeScene(scenes[step]);
    log << "REQUEST " << scenes[step] << std::endl;
}
}
#endif
