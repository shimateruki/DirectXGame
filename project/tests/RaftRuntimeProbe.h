#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "game/system/progress/StageManager.h"
#include "Player.h"
#include "BaseEnemy.h"
#include "WinApp.h"
#include <fstream>
#include "RollerRuntimeProbe.h"

namespace RaftRuntimeProbe {
inline ULONGLONG pauseUntil = 0;
inline bool Enabled() {
    // 明示した検証起動だけで配置を変え、通常プレイには介入しない。
    static const bool enabled = GetEnvironmentVariableA("CG2_RAFT_TEST", nullptr, 0) > 0;
    return enabled;
}
inline bool WantsPlay(SceneManager* manager) {
    // エディターの停止設定から独立して、実際の接地・死亡・描画経路を検証する。
    return Enabled() && manager && !manager->IsTransitioning() && manager->GetCurrentSceneName() == "GAMEPLAY";
}
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    // 同じ検証用起動経路で円柱も確認し、通常起動の更新処理を増やさない。
    if (GetEnvironmentVariableA("CG2_ROLLER_TEST", nullptr, 0)>0) {
        RollerRuntimeProbe::AfterDraw(manager, path);
        return;
    }
    // プレイヤーを最初だけ床へ置き、以後は通常のゲーム更新に任せる。
    static std::ofstream log(path, std::ios::trunc);
    static int phase = 0, frames = 0;
    static ULONGLONG began = GetTickCount64();
    static bool sawSea = false, sawPeak = false, done = false;
    if (done || !manager || manager->IsTransitioning()) return;
    auto finish = [&](const char* message) {
        log << message << std::endl;
        done = true;
        WinApp::CloseNow();
    };
    if (GetTickCount64() - began > 180000) { finish("FAIL timeout"); return; }
    if (phase == 0) {
        StageManager::GetInstance()->SetCurrentStageById("stage1");
        manager->ChangeScene("GAMEPLAY");
        phase = 1;
        return;
    }
    auto* scene = manager->GetCurrentScene();
    auto* player = GetScenePlayer(scene);
    if (!player || manager->GetCurrentSceneName() != "GAMEPLAY") return;
    engine::Object3d* deck = nullptr;
    for (auto& object : scene->GetObjects())
        if (object && object->GetName() == "Stage1_Coaster_Deck") deck = object.get();
    if (!deck) { finish("FAIL missing deck"); return; }
    if (phase == 1) {
        if (++frames < 180 || player->IsCinematicLocked()) return;
        const auto top = deck->GetAABB().max.y;
        const float feetOffset = player->GetAABB().min.y - player->GetTranslate().y;
        auto position = deck->GetTranslate();
        position.y = top - feetOffset + .05f;
        player->SetTranslate(position);
        player->SetVelocity({});
        player->UpdateWorldMatrix(true);
        phase = 2;
        frames = 0;
        log << "START raft" << std::endl;
        return;
    }
    const auto d = deck->GetTranslate();
    const auto p = player->GetTranslate();
    // 輸送の接地検証では戦闘AIだけ休止する。落下乗り込みと地形判定は実処理を通す。
    for (auto& object : scene->GetObjects())
        if (object && object->GetName().find("Stage1_Coaster_Boarder_") == 0)
            if (auto* enemy = dynamic_cast<BaseEnemy*>(object.get())) enemy->SetDormant(true);
    player->SetDamageInvincible(true);
    if (++frames % 30 == 0)
        log << frames << " raft=" << d.x << ',' << d.y << ',' << d.z
            << " player=" << p.x << ',' << p.y << ',' << p.z
            << " grounded=" << player->IsGrounded() << " dead=" << player->isDead << std::endl;
    if (player->isDead || p.y < -15) { finish("FAIL rider lost"); return; }
    sawSea |= d.y < -10;
    if (!sawPeak && d.y > 21.9f) {
        sawPeak = true;
        pauseUntil = GetTickCount64() + 15000;
        log << "PAUSE peak for visual inspection" << std::endl;
    }
    if (sawSea && sawPeak && d.y < 16.4f && d.x > 760 && d.z < 13.1f)
        finish("PASS sea travel, ascent and landing with live rider");
}
}
#endif
