#define NOMINMAX
#include "PlayerState.h"
#include "Player.h"
#include "engine/utility/math/Math.h"
#include "DebugConsole.h"
#include <memory>

// 基本移動系の状態をまとめています。


// ========================================================
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Idle");
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
        player->SetSlimeJumpCharge(0.0f);
    }
}

void PlayerStateIdle::Update(Player* player, float deltaTime) {
    (void)deltaTime;
    if (!player) return;

    if (!player->IsGrounded()) {
        player->ChangeState(std::make_unique<PlayerStateJump>());
        return;
    }

    // 速度があれば Run へ遷移
    Vector3 vel = player->GetVelocity();
    vel.y = 0.0f;
    if (Math::Length(vel) > 0.1f) {
        player->ChangeState(std::make_unique<PlayerStateRun>());
    }
}

void PlayerStateIdle::Exit(Player* player) {}

// ========================================================
// 走り状態 (Run)
// ========================================================
void PlayerStateRun::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Run");
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Run);
    }
}

void PlayerStateRun::Update(Player* player, float deltaTime) {
    (void)deltaTime;
    if (!player) return;

    if (!player->IsGrounded()) {
        player->ChangeState(std::make_unique<PlayerStateJump>());
        return;
    }

    // 速度が落ちたら Idle へ遷移
    Vector3 vel = player->GetVelocity();
    vel.y = 0.0f;
    if (Math::Length(vel) <= 0.1f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateRun::Exit(Player* player) {}

// ========================================================
// ジャンプ状態 (Jump)
// ========================================================
void PlayerStateJump::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Jump");
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);
    }
}

void PlayerStateJump::Update(Player* player, float deltaTime) {
    (void)deltaTime;
    if (!player) return;

    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);

    // 地面に着地したら Idle へ遷移
    if (player->IsGrounded() && player->GetVelocity().y <= 0.0f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateJump::Exit(Player* player) {}

// ========================================================
// 回避ダッシュ状態 (Dash)
// ========================================================
void PlayerStateDash::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Dash");
    timer_ = 0.0f;
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
        player->SetSlimeAnimationDirection(player->GetVelocity());
    }
}

void PlayerStateDash::Update(Player* player, float deltaTime) {
    if (!player) return;

    // 約0.2秒(ダッシュ時間)経過で Idle へ戻す
    timer_ += deltaTime;
    if (timer_ >= 0.2f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateDash::Exit(Player* player) {
    if (player) {
        player->TriggerSlimeImpulse({ 2.55f, 1.25f, 2.55f }, 0.16f);
    }
}
