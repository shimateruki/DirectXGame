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
    // エディターの共通セッションへ、ゲーム固有の生成・更新・片付けを関数として渡す。
    SelectedActorSession::Services services;
    services.initialize = [panel](SceneManager* manager, SelectedActorSession& session) {
        panel->session_ = &session; panel->Initialize(manager);
    };
    services.finalize = [panel] { panel->Finalize(); };
    services.identity = [](const engine::Object3D& object) { return object.GetClassName() + "#" + object.GetEnemyType(); };
    // プレイヤーを選んだ時はコピー能力へ、敵を選んだ時は敵の型へ切り替え、型が変わった時だけ設定を読み直す。
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
    // 選択中のオブジェクトのワールド姿勢を基準に、隔離されたプレビューを作る。
    services.begin = [panel] {
        auto* source = panel->session_->GetTarget();
        if (!source) return false;
        EditorCommon::DecomposeWorldPose(source->GetWorldMatrix(), panel->sourcePosition_, panel->sourceRotation_, panel->sourceScale_);
        panel->enabled_ = true;
        panel->BeginEffects();
        panel->BuildRuntime(false);
        // 表示できる実体が作れなかった場合も、事前に始めた演出や生成物をその場で戻す。
        if (panel->HasPreviewActor()) return true;
        panel->ReleaseRuntime(); panel->EndEffects(); panel->enabled_ = false;
        return false;
    };
    services.maintain = [panel](float deltaTime) { panel->Update(deltaTime); };
    // 固定刻みの間だけ検索対象を限定し、本番シーンのキャラクターを攻撃対象にしない。
    services.step = [panel](float deltaTime) {
        auto* physics = CollisionManager::GetInstance();
        auto previous = physics->ExchangeEditorQueryFilter([](const engine::Object3D* object) {
            return object->IsEditorInternal() || dynamic_cast<const Character*>(object) == nullptr;
        });
        // 更新中に失敗しても、一時的な検索条件は例外を返す前に元へ戻す。
        try { panel->StepSimulation(deltaTime); }
        catch (...) { physics->ExchangeEditorQueryFilter(std::move(previous)); throw; }
        physics->ExchangeEditorQueryFilter(std::move(previous));
    };
    // 停止時は演技の一時状態、生成した実体、演出の順で片付ける。
    services.end = [panel] {
        panel->ClearPreviewTransientState(); panel->ReleaseRuntime(); panel->EndEffects(); panel->enabled_ = false;
    };
    // 配置編集中は再生を進めず、再生位置や速度の表示は現在のパネルの値を公開する。
    services.playback = [panel] {
        return SelectedActorSession::Playback{panel->playing_ && !panel->placementPreview_, panel->loop_,
            panel->playbackSpeed_, panel->loopDuration_, panel->elapsedTime_};
    };
    services.setPlaying = [panel](bool playing) { panel->playing_ = playing; };
    services.overlay = [panel](float x,float y,float w,float h) { panel->DrawAttackOverlay(x,y,w,h); };
    SelectedActorSession::RegisterServices(std::move(services));
}
#endif
