#pragma once
#ifdef USE_IMGUI
#include "DebugEditor.h"
#include "EditorManager.h"
#include "game/editor/EnemyAttackPreviewRegistry.h"
#include "game/editor/CharacterStatusEditor.h"
#include "game/system/rules/GameplayStatusManager.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "BaseEnemy.h"
#include "Player.h"
#include "SceneManager.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>
#include <cmath>

// 専用実行で、統合UIと同じ対象選択・反映・再生成・保存の経路を確認します。
struct StatusPreviewRuntimeProbe {
    inline static int drawnStatusFrames = 0;
    static bool Enabled() { return GetEnvironmentVariableA("CG2_STATUS_PREVIEW_TEST", nullptr, 0) > 0; }
    static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    static void Near(float a, float b, const char* message) { Require(std::abs(a-b) < 0.01f, message); }

    static void Check(SceneManager* scenes, DebugEditor* editor, std::ofstream& log) {
        auto* scene = scenes->GetCurrentScene();
        auto* manager = GameplayStatusManager::GetInstance();
        auto& service = EnemyAttackPreviewRegistry::GetStatusEditor();
        Require(service.targets && service.draw && service.savePending, "status editor registration");
        const auto targets = service.targets();
        for (const auto& type : manager->GetEnemyTypes()) {
            const auto count = std::count_if(targets.begin(), targets.end(), [&](const auto& t) { return t.type == type; });
            Require(count == 1, "missing or duplicated status target");
        }
        log << "OK all " << targets.size() << " status targets include preview-only and status-only entries" << std::endl;

        const auto originalFire = *manager->FindEnemyStatus("FireSlime");
        const auto originalPlayer = manager->GetPlayerStatus();
        auto fire = EnemyFactory::GetInstance()->CreateEnemy("FireSlime", scene->GetObject3DCommon());
        auto thunder = EnemyFactory::GetInstance()->CreateEnemy("ThunderSlime", scene->GetObject3DCommon());
        auto dummy = EnemyFactory::GetInstance()->CreateEnemy("FireSlime", scene->GetObject3DCommon());
        auto* firePtr = fire.get(); auto* otherPtr = thunder.get(); auto* dummyPtr = dummy.get();
        dummy->SetEditorInternal(true);
        dummy->SetParameter(&engine::EntityParameter::hp, dummy->SetParameter(&engine::EntityParameter::maxHp, 100000.0f));
        fire->SetParameter(&engine::EntityParameter::hp, fire->GetParameters()->maxHp * 0.5f);
        const auto otherHp = thunder->GetParameters()->hp;
        scene->AddObjectImmediate(std::move(fire)); scene->AddObjectImmediate(std::move(thunder)); scene->AddObjectImmediate(std::move(dummy));
        // 旧形式でParamにだけ敵タイプが残った対象も、既存Managerと同じ規則で扱います。
        firePtr->SetEnemyType("");
        firePtr->SetParameter(&engine::EntityParameter::enemyType, "FireSlime");
        auto* status = manager->FindMutableEnemyStatus("FireSlime");
        status->maxHp = 140; status->speed = 17;
        manager->MarkDirty();
        Require(CharacterStatusEditor::Apply(scene, "FireSlime") >= 1, "live status target missing");
        Near(firePtr->GetParameters()->hp, 70, "current HP ratio changed");
        Near(firePtr->GetParameters()->speed, 17, "selected type speed not applied");
        Near(otherPtr->GetParameters()->hp, otherHp, "other type modified");
        Near(dummyPtr->GetParameters()->hp, 100000, "editor dummy overwritten");
        firePtr->SetEnemyType("FireSlime");
        log << "OK live apply preserves HP ratio, other enemy types and editor dummy" << std::endl;

        auto& preview = *GetGameActorDebugPanel();
        preview.SelectStatusTarget("FireSlime");
        EditorManager::GetInstance()->SetSelectedObject(&preview);
        preview.enabled_ = true; preview.CreatePreviewObjects(); preview.playing_ = false;
        Require(preview.previewEnemy_ && !preview.IsPlayerCopyMode(), "enemy target selection");
        Near(preview.previewEnemy_->GetParameters()->maxHp, 140, "enemy preview did not use edited status");
        status->maxHp = 100; status->speed = 9; manager->MarkDirty();
        CharacterStatusEditor::Apply(scene, "FireSlime");
        Near(preview.previewEnemy_->GetParameters()->maxHp, 140, "live apply touched preview actor");
        preview.RefreshStatusPreview();
        Near(preview.previewEnemy_->GetParameters()->maxHp, 100, "restart did not refresh status");
        Near(preview.previewEnemy_->GetParameters()->speed, 9, "restart speed stale");
        Require(!preview.playing_ && preview.elapsedTime_ == 0, "paused refresh state");

        preview.SelectStatusTarget("RingBurner");
        Require(preview.enemyType_ == "RingBurner" && !preview.GetPreviewDescriptor(), "status-only target missing");
        preview.enabled_ = true; preview.CreatePreviewObjects();
        Require(!preview.enabled_ && !preview.previewEnemy_, "unregistered preview spawned wrong actor");

        auto& playerStatus = manager->GetMutablePlayerStatus();
        playerStatus.maxHp = 19; playerStatus.gravity = 21; playerStatus.maxFallSpeed = 34;
        manager->MarkDirty();
        preview.SelectStatusTarget("");
        preview.copyActionId_ = "acquire";
        preview.enabled_ = true; preview.CreatePreviewObjects(); preview.playing_ = false;
        Require(preview.previewPlayer_ && preview.IsPlayerCopyMode(), "player target selection");
        Near(preview.previewPlayer_->GetParameters()->maxHp, 19, "player preview HP forced to old constant");
        Near(preview.previewPlayer_->GetParameters()->gravity, 21, "player preview gravity forced to old constant");
        Near(preview.previewPlayer_->GetParameters()->maxFallSpeed, 34, "player preview fall speed forced to old constant");
        if (preview.previewEnemy_) Near(preview.previewEnemy_->GetParameters()->maxHp, 100000, "copy hit dummy HP changed");
        log << "OK enemy/player selection, restart, pause preservation and Player initial values" << std::endl;

        service.savePending();
        Require(!manager->IsDirty(), "auto save did not clear dirty flag");
        status->maxHp = 999;
        Require(manager->Reload(), "saved settings reload failed");
        Near(manager->FindEnemyStatus("FireSlime")->maxHp, 100, "saved enemy status mismatch");
        Near(manager->GetPlayerStatus().maxHp, 19, "saved player status mismatch");
        log << "OK existing settings JSON save/reload contract" << std::endl;

        *manager->FindMutableEnemyStatus("FireSlime") = originalFire;
        manager->GetMutablePlayerStatus() = originalPlayer;
        manager->MarkDirty(); manager->SaveIfDirty();
        CharacterStatusEditor::Apply(scene, "*");
        preview.StopPreview();
        for (auto* object : {firePtr, otherPtr, dummyPtr}) scene->RequestRemoveObject(object);
        preview.SelectStatusTarget("FireSlime");
        // 通常プレイ中も対象だけ切り替えられ、隔離Actorは生成されないことを確認します。
        const bool wasPlaying = scenes->IsPlaying();
        scenes->SetIsPlaying(true);
        preview.SelectStatusTarget("");
        const bool playerSelected = preview.IsPlayerCopyMode();
        preview.SelectStatusTarget("FireSlime");
        const bool enemySelected = !preview.IsPlayerCopyMode() && preview.enemyType_ == "FireSlime";
        scenes->SetIsPlaying(wasPlaying);
        Require(playerSelected && enemySelected && !preview.enabled_, "live play status target switch");
        log << "OK live play target selection does not start isolated preview" << std::endl;
        // 実際のInspector描画経路を通った回数で、UIが非表示のまま成功するのを防ぎます。
        auto observedEditor = service;
        observedEditor.draw = [draw = service.draw](BaseScene* targetScene, const std::string& type) {
            ++drawnStatusFrames;
            return draw(targetScene, type);
        };
        EnemyAttackPreviewRegistry::RegisterStatusEditor(std::move(observedEditor));
        // 通常フレームで統合欄を描かせ、登録コールバックからImGuiへの接続も通します。
        EditorManager::GetInstance()->SetSelectedObject(&preview);
    }

    static void AfterDraw(SceneManager* scenes, DebugEditor* editor, const std::string& path) {
        if (!Enabled() || !scenes || !editor) return;
        static std::ofstream log(path, std::ios::trunc);
        static int phase = 0, frames = 0;
        static bool done = false;
        if (done || scenes->IsTransitioning()) return;
        try {
            if (phase == 0) { scenes->ChangeScene("GAMEPLAY"); phase = 1; return; }
            if (scenes->GetCurrentSceneName() != "GAMEPLAY" || ++frames < 3) return;
            if (phase == 1) { Check(scenes, editor, log); phase = 2; frames = 0; return; }
            if (phase == 2) {
                auto* preview = GetGameActorDebugPanel();
                Require(EditorManager::GetInstance()->GetSelectedObject() == preview, "integrated inspector selection lost");
                Require(drawnStatusFrames > 0, "integrated status panel was not drawn" );
                log << "PASS: integrated character status, live apply, preview refresh, persistence and normal UI frames" << std::endl;
                done = true; WinApp::CloseNow();
            }
        } catch (const std::exception& e) {
            log << "FAIL: " << e.what() << std::endl;
            done = true; WinApp::CloseNow();
        }
    }
};
#endif
