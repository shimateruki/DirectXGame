#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "engine/utility/math/Math.h"
#include "DebugConsole.h"
#include <sstream> // 数字を文字にする用

// ========================================================
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player* player) {
    player->PlayAnimation("Idle", false); // Tポーズ
    DebugConsole::GetInstance()->AddLog("★ ENTER: Idle State (aaaa)");
}

void PlayerStateIdle::Update(Player* player) {
    Vector3 vel = player->GetVelocity();
    vel.y = 0.0f;
    float speed = Math::Length(vel);

    // ログ確認用
    // DebugConsole::GetInstance()->AddLog("Idle Updating..."); 

    if (speed > 0.1f) {
        player->ChangeState(std::make_unique<PlayerStateRun>());
    }
}

void PlayerStateIdle::Exit(Player* player) {
}

// ========================================================
// 走り状態 (Run)
// ========================================================
void PlayerStateRun::Enter(Player* player) {
    player->PlayAnimation("Armature|mixamo.com|Layer0", true);
    DebugConsole::GetInstance()->AddLog("★ ENTER: Run State");
}

void PlayerStateRun::Update(Player* player) {
    // 1. 速度計算
    Vector3 rawVel = player->GetVelocity();
    Vector3 flatVel = rawVel;
    flatVel.y = 0.0f; // 重力無視
    float speed = Math::Length(flatVel);

    // 2. なぜ止まらないのか、証拠を表示！
    std::stringstream ss;
    ss << "RUN Check | Speed: " << speed
        << " (RawY: " << rawVel.y << ")";
    DebugConsole::GetInstance()->AddLog(ss.str());

    // 3. 判定
    if (speed <= 0.1f) {
        DebugConsole::GetInstance()->AddLog("SUCCESS! Transitioning to Idle...");
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    } else {
        // ここが出ているなら、速度が0.1より大きい（スティックのドリフトなどの可能性）
        // DebugConsole::GetInstance()->AddLog("Failed... Speed is > 0.1");
    }
}

void PlayerStateRun::Exit(Player* player) {
    DebugConsole::GetInstance()->AddLog("EXIT: Run State");
}