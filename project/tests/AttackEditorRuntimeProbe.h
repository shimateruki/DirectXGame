#pragma once
#ifdef USE_IMGUI
#include "engine/utility/SystemDebug/editor/windows/EnemyAttackPreviewWindow.h"
#include "engine/utility/SystemDebug/editor/windows/EffectPreviewStage.h"
#include "engine/utility/SystemDebug/editor/core/EditorManager.h"
#include "engine/utility/SystemDebug/editor/core/DebugEditor.h"
#include "SceneManager.h"
#include "WinApp.h"
#include <fstream>

// Updateを直接呼ばず、通常のGame View→DebugEditor経路で進むことを検証します。
struct AttackEditorRuntimeProbe {
    static bool Enabled() {
        static const bool enabled = GetEnvironmentVariableA("CG2_ATTACK_EDITOR_TEST", nullptr, 0) > 0;
        return enabled;
    }
    static void AfterDraw(SceneManager* manager, DebugEditor* editor, const std::string& path) {
        if (!Enabled() || !manager || !editor) return;
        static std::ofstream log(path, std::ios::trunc);
        static int phase = 0, frames = 0;
        static float start = 0;
        static bool done = false;
        static ULONGLONG began = GetTickCount64();
        if (done) return;
        auto finish = [&](const std::string& result) { log << result << std::endl; done = true; WinApp::CloseNow(); };
        if (GetTickCount64() - began > 300000) { finish("FAIL: editor timeout"); return; }
        if (manager->IsTransitioning()) return;
        if (phase == 0) { manager->ChangeScene("GAMEPLAY"); phase = 1; return; }
        if (manager->GetCurrentSceneName() != "GAMEPLAY") return;
        auto& preview = *editor->GetEnemyAttackPreviewWindow();
        if (phase == 1) {
            // Scene切替直後の選択リセットが終わってから、ユーザーが開くのと同じ順序にします。
            if (++frames < 3) return;
            preview.enemyTypeIndex_ = 1;
            preview.LoadSelectedProfile();
            for (int i = 0; i < static_cast<int>(preview.editableProfile_.attacks.size()); ++i)
                if (preview.editableProfile_.attacks[i].id == "fireball") preview.profileAttackIndex_ = i;
            preview.ApplyRecommendedSettings();
            preview.loop_ = false;
            EditorManager::GetInstance()->SetSelectedObject(&preview);
            preview.PreviewDraft(false);
            start = preview.elapsedTime_; frames = 0; phase = 2;
            log << "START draft playback time=" << start << std::endl; return;
        }
        if (++frames < 30) return;
        frames = 0;
        if (phase == 2 || phase == 5 || phase == 7) {
            log << "PLAY phase=" << phase << " elapsed=" << preview.elapsedTime_ << std::endl;
            if (!preview.IsAuthoringViewportActive()) { finish("FAIL: attack editor selection lost"); return; }
            if (preview.elapsedTime_ < start + 0.3f) {
                finish("FAIL: authoring playback clock did not advance"); return;
            }
            int gridObjects = 0;
            for (const auto& object : manager->GetCurrentScene()->GetObjects())
                if (object && object->GetIsVisible() && object->GetName().find("__Editor_EffectPreviewGrid") == 0) ++gridObjects;
            log << "GRID visible=" << gridObjects << std::endl;
            if (!EffectPreviewStage::GetInstance()->IsEnabled() || gridObjects < 20) {
                finish("FAIL: isolated preview grid missing"); return;
            }
            if (phase == 7) { finish("PASS: editor auto-play, pause, frame-step, resume, placement-to-play and visible grid"); return; }
            if (phase == 5) {
                preview.selectedTrack_ = -2;
                preview.BeginPlacementPreview();
                start = preview.elapsedTime_; phase = 6; return;
            }
            preview.playing_ = false; start = preview.elapsedTime_; phase = 3; return;
        }
        if (phase == 3) {
            if (std::abs(preview.elapsedTime_ - start) > 0.001f) { finish("FAIL: pause advanced time"); return; }
            preview.SeekPreview(start + 1.0f / 60.0f);
            if (std::abs(preview.elapsedTime_ - start - 1.0f / 60.0f) > 0.001f) { finish("FAIL: frame-step mismatch"); return; }
            start = preview.elapsedTime_; phase = 4; log << "STEP elapsed=" << start << std::endl; return;
        }
        if (phase == 4) {
            if (std::abs(preview.elapsedTime_ - start) > 0.001f) { finish("FAIL: frame-step resumed playback"); return; }
            preview.playing_ = true; phase = 5; return;
        }
        if (phase == 6) {
            if (!preview.placementPreview_ || std::abs(preview.elapsedTime_ - start) > 0.001f) {
                finish("FAIL: placement preview did not remain paused"); return;
            }
            preview.playing_ = true; start = 0; phase = 7;
        }
    }
};
#endif
