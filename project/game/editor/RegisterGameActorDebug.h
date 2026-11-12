#pragma once

// ゲーム固有の登録入口：RegisterGameActorDebug。登録先の共通Editorに、個別の型名や分岐を持ち込まない。
#ifdef USE_IMGUI
#include "game/editor/ActorDebugPanel.h"
#include "game/editor/EnemyAttackPreviewRegistry.h"
#include "engine/editor/preview/SelectedActorSession.h"
#include "Object3D.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "imgui.h"
#include "CollisionManager.h"
#include "Character.h"
#include "EditorCommon.h"

// ゲーム固有の編集状態は一つにまとめる。敵本体にはImGuiや編集用の状態を増やさない。
inline ActorDebugPanel* GetGameActorDebugPanel() { static ActorDebugPanel panel; return &panel; }
inline void RegisterGameActorDebug() {
    auto* panel = GetGameActorDebugPanel();
    SelectedActorSession::Services services;
    services.initialize = [panel](SceneManager* manager, SelectedActorSession& session) {
        panel->session_ = &session; panel->Initialize(manager);
    };
    services.finalize = [panel] { panel->Finalize(); };
    services.identity = [](const engine::Object3D& object) { return object.GetClassName() + "#" + object.GetEnemyType(); };
    services.select = [panel](engine::Object3D& object) {
        const bool player = object.GetClassName() == "Player";
        const std::string type = player ? std::string{} : object.GetEnemyType();
        const bool changed = panel->playerCopyMode_ != player || (!player && panel->enemyType_ != type);
        panel->playerCopyMode_ = player; panel->crownMovieMode_ = false;
        panel->statusTab_ = false;
        panel->authoringActorSelected_ = false; panel->selectedTrack_ = -1;
        if (!player) panel->enemyType_ = type;
        if (changed) { panel->LoadSelectedProfile(); panel->ApplyRecommendedSettings(); if (player) panel->loopDuration_ = 6.0f; }
    };
    services.draw = [panel](ProjectWindow* assets) {
panel->DrawSelectedInspector(assets);
};
    services.begin = [panel] {
        auto* source = panel->session_->GetTarget();
        if (!source) return false;
        EditorCommon::DecomposeWorldPose(source->GetWorldMatrix(), panel->sourcePosition_, panel->sourceRotation_, panel->sourceScale_);
        panel->enabled_ = true;
        panel->BeginEffects();
        panel->BuildRuntime(false);
        if (panel->HasPreviewActor()) return true;
        panel->ReleaseRuntime(); panel->EndEffects(); panel->enabled_ = false;
        return false;
    };
    services.maintain = [panel](float deltaTime) { panel->Update(deltaTime); };
    services.step = [panel](float deltaTime) {
        auto* physics = CollisionManager::GetInstance();
        auto previous = physics->ExchangeEditorQueryFilter([](const engine::Object3D* object) {
            return object->IsEditorInternal() || dynamic_cast<const Character*>(object) == nullptr;
        });
        try { panel->StepSimulation(deltaTime); }
        catch (...) { physics->ExchangeEditorQueryFilter(std::move(previous)); throw; }
        physics->ExchangeEditorQueryFilter(std::move(previous));
    };
    services.end = [panel] {
        panel->ClearPreviewTransientState(); panel->ReleaseRuntime(); panel->EndEffects(); panel->enabled_ = false;
    };
    services.playback = [panel] {
        return SelectedActorSession::Playback{panel->playing_ && !panel->placementPreview_, panel->loop_,
            panel->playbackSpeed_, panel->loopDuration_, panel->elapsedTime_};
    };
    services.setPlaying = [panel](bool playing) { panel->playing_ = playing; };
    services.overlay = [panel](float x,float y,float w,float h) { panel->DrawAttackOverlay(x,y,w,h); };
    SelectedActorSession::RegisterServices(std::move(services));
}
#endif
