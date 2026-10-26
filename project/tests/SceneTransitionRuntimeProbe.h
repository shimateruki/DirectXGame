#pragma once

#ifdef USE_IMGUI
#include "SceneManager.h"
#include "StatusPreviewRuntimeProbe.h"
#include "PreviewCompositionRuntimeProbe.h"
#include "AttackPreviewRegistryRuntimeProbe.h"
#include "EditorEventRuntimeProbe.h"
#include "MeshEffectManager.h"
#include "WinApp.h"
#include "SdlStickRuntimeProbe.h"
#include "TutorialPromptRuntimeProbe.h"
#include <Windows.h>
#include <fstream>
#include <string>
#include "AttackLifecycleRuntimeProbe.h"
#include "AttackEditorRuntimeProbe.h"
#include "RaftRuntimeProbe.h"
#include "CinematicRuntimeProbe.h"
#include "GimmickPropertyRuntimeProbe.h"
#include "GateEntryRuntimeProbe.h"
#include "TutorialExitGuideRuntimeProbe.h"
#include "PlayerAttackRuntimeProbe.h"
#include "PlayerLocomotionRuntimeProbe.h"
#include "CannonDamageRuntimeProbe.h"
#include "GoalPresentationRuntimeProbe.h"
#include "StarCoinRuntimeProbe.h"
#include "EnemyProjectileRuntimeProbe.h"
#include "CarryHookRuntimeProbe.h"
#include "CameraBarrierRuntimeProbe.h"

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
    if (!Enabled() || !manager)
        return;
    if (StatusPreviewRuntimeProbe::Enabled()) {
        StatusPreviewRuntimeProbe::AfterDraw(manager, editor, LogPath());
        return;
    }
    if (PreviewCompositionRuntimeProbe::Enabled()) {
        PreviewCompositionRuntimeProbe::AfterDraw(manager, editor, LogPath());
        return;
    }
    if (AttackPreviewRegistryRuntimeProbe::Enabled()) {
        AttackPreviewRegistryRuntimeProbe::AfterDraw(manager, editor, LogPath());
        return;
    }
    if (EditorEventRuntimeProbe::Enabled()) {
        EditorEventRuntimeProbe::AfterDraw(manager, editor, LogPath());
        return;
    }
    if (CameraBarrierRuntimeProbe::Enabled()) {
        CameraBarrierRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (CarryHookRuntimeProbe::Enabled()) {
        CarryHookRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (EnemyProjectileRuntimeProbe::Enabled()) {
        EnemyProjectileRuntimeProbe::AfterDraw(manager,LogPath());
        return;
    }
    if (StarCoinRuntimeProbe::Enabled()) {
        StarCoinRuntimeProbe::AfterDraw(manager,LogPath());
        return;
    }
    if (GoalPresentationRuntimeProbe::Enabled()) {
        GoalPresentationRuntimeProbe::AfterDraw(manager,LogPath());
        return;
    }
    if (SdlStickRuntimeProbe::Enabled()) {
        SdlStickRuntimeProbe::AfterDraw(LogPath());
        return;
    }
    if (GateEntryRuntimeProbe::Enabled()) {
        GateEntryRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (CannonDamageRuntimeProbe::Enabled()) {
        CannonDamageRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (TutorialExitGuideRuntimeProbe::Enabled()) {
        TutorialExitGuideRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (TutorialPromptRuntimeProbe::Enabled()) {
        TutorialPromptRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (PlayerLocomotionRuntimeProbe::Enabled()) {
        PlayerLocomotionRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (PlayerAttackRuntimeProbe::Enabled()) {
        PlayerAttackRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (GimmickPropertyRuntimeProbe::Enabled()) {
        GimmickPropertyRuntimeProbe::AfterDraw(manager, editor, LogPath());
        return;
    }
    if (CinematicRuntimeProbe::Enabled()) {
        CinematicRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (RaftRuntimeProbe::Enabled()) {
        RaftRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    if (AttackEditorRuntimeProbe::Enabled()) {
        AttackEditorRuntimeProbe::AfterDraw(manager, editor, LogPath());
        return;
    }
    if (AttackLifecycleRuntimeProbe::Enabled()) {
        AttackLifecycleRuntimeProbe::AfterDraw(manager, LogPath());
        return;
    }
    static std::ofstream log(LogPath(), std::ios::trunc);
    static ULONGLONG phaseStarted = GetTickCount64();
    static int step = 0;
    static int frames = 0;
    static bool finished = false;
    static uint64_t generation = 0;
    if (finished)
        return;
    auto finish = [&](const char* message) {
        log << message << std::endl;
        finished = true;

        // 検証専用プロセスなので保存確認UIを挟まず、通常のFinalizeへ進めます。
        WinApp::CloseNow();
    };

    // 複数回の実アセット読込を合算せず、各遷移が進まなくなった場合を検出します。
    if (GetTickCount64() - phaseStarted > 180000) {
        finish("FAIL: transition timeout");
        return;
    }
    if (manager->IsTransitioning())
        return;
    const char* scenes[] = {"TITLE", "GAMEPLAY", "SELECT", "GAMEPLAY", "GAMEPLAY"};
    if (manager->GetCurrentSceneName() != scenes[step]) {
        finish("FAIL: unexpected scene");
        return;
    }
    if (frames == 0) {
        if (generation == manager->GetSceneGeneration())
            return;
        generation = manager->GetSceneGeneration();
        log << "ENTER " << step << ' ' << scenes[step] << " generation=" << generation << std::endl;
        for (const auto& effect : MeshEffectManager::GetInstance()->GetActiveEffects()) {
            if (effect && effect->GetName() == "SceneTransitionLifetimeProbe") {
                finish("FAIL: previous scene effect survived its owner");
                return;
            }
        }

        // 遷移時にも生存する長寿命エフェクトで、描画基盤への古い参照を検出します。
        MeshEffectManager::GetInstance()->SpawnPortalEffect({0, 1, 0}, 120.0f);
        if (MeshEffectManager::GetInstance()->GetActiveEffects().empty()) {
            finish("FAIL: mesh effect was not created");
            return;
        }
        MeshEffectManager::GetInstance()->GetActiveEffects().back()->SetName("SceneTransitionLifetimeProbe");
    }
    if (++frames < 120)
        return;
    log << "DRAWN " << scenes[step] << " frames=" << frames
        << " activeMesh=" << MeshEffectManager::GetInstance()->GetActiveEffects().size() << std::endl;
    if (++step == 5) {
        finish("PASS: loading transitions and direct reload rendered successfully");
        return;
    }
    frames = 0;
    phaseStarted = GetTickCount64();
    if (step == 4)
        manager->ChangeSceneWithoutLoading(scenes[step]);
    else manager->ChangeScene(scenes[step]);
    log << "REQUEST " << scenes[step] << std::endl;
}
}
#endif
