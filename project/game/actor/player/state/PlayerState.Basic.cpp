#define NOMINMAX
#include "game/actor/player/state/PlayerState.h"
#include "game/actor/player/Player.h"
#include "engine/utility/math/Math.h"
#include "DebugConsole.h"
#include <memory>

// 基本移動系の状態をまとめています。

// ========================================================
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player* player) {
    // 待機へ戻る時はジャンプの溜め表示を消す。
    DebugConsole::GetInstance()->AddLog("Enter: Idle");
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
        player->SetSlimeJumpCharge(0.0f);
    }
}

void PlayerStateIdle::Update(Player* player, float deltaTime) {
    // 接地を先に見て、水平移動だけを走り状態への条件に使う。
    (void)deltaTime;
    if (!player)
        return;

    if (!player->IsGrounded()) {
        player->ChangeState(std::make_unique<PlayerStateJump>());
        return;
    }

    // 速度があれば Run へ遷移
    engine::Vector3 vel = player->GetVelocity();
    vel.y = 0.0f;
    if (engine::Math::Length(vel) > 0.1f) {
        player->ChangeState(std::make_unique<PlayerStateRun>());
    }
}

void PlayerStateIdle::Exit(Player* player) {
    // 待機状態には退出時に戻す一時的な値がない。
    (void)player;
}

// ========================================================
// 走り状態 (Run)
// ========================================================
void PlayerStateRun::Enter(Player* player) {
    // 走行モーションへ切り替え、移動速度はプレイヤー本体側で管理する。
    DebugConsole::GetInstance()->AddLog("Enter: Run");
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Run);
    }
}

void PlayerStateRun::Update(Player* player, float deltaTime) {
    // 空中ではジャンプを優先し、水平速度が下がったら待機へ戻る。
    (void)deltaTime;
    if (!player)
        return;

    if (!player->IsGrounded()) {
        player->ChangeState(std::make_unique<PlayerStateJump>());
        return;
    }

    // 速度が落ちたら Idle へ遷移
    engine::Vector3 vel = player->GetVelocity();
    vel.y = 0.0f;
    if (engine::Math::Length(vel) <= 0.1f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateRun::Exit(Player* player) {
    // 速度は共有の移動処理が保持するため、この状態では変更しない。
    (void)player;
}

// ========================================================
// ジャンプ状態 (Jump)
// ========================================================
void PlayerStateJump::Enter(Player* player) {
    // 空中の姿勢をアニメーターへ知らせる。
    DebugConsole::GetInstance()->AddLog("Enter: Jump");
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);
    }
}

void PlayerStateJump::Update(Player* player, float deltaTime) {
    // 着地直後は下降中の接地だけを待機へ戻す条件とする。
    (void)deltaTime;
    if (!player)
        return;

    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);

    // 地面に着地したら Idle へ遷移
    if (player->IsGrounded() && player->GetVelocity().y <= 0.0f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateJump::Exit(Player* player) {
    // 着地後の姿勢変更は遷移先の Enter が担当する。
    (void)player;
}

// ========================================================
// 回避ダッシュ状態 (Dash)
// ========================================================
void PlayerStateDash::Enter(Player* player) {
    // ダッシュ開始時の進行方向を変形アニメーションへ渡す。
    DebugConsole::GetInstance()->AddLog("Enter: Dash");
    timer_ = 0.0f;
    if (player) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
        player->SetSlimeAnimationDirection(player->GetVelocity());
    }
}

void PlayerStateDash::Update(Player* player, float deltaTime) {
    // ダッシュ状態の滞在時間だけを計り、移動自体は本体に任せる。
    if (!player)
        return;

    // 約0.2秒(ダッシュ時間)経過で Idle へ戻す
    timer_ += deltaTime;
    if (timer_ >= 0.2f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateDash::Exit(Player* player) {
    // 終了時に弾性の反動を与えて、通常姿勢へつなげる。
    if (player) {
        player->TriggerSlimeImpulse({2.55f, 1.25f, 2.55f}, 0.16f);
    }
}
