#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "engine/utility/math/Math.h"
#include "DebugConsole.h"
#include "engine/graphics/postprocess/Fade.h"
#include "engine/graphics/postprocess/PostEffect.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include <memory>

// ========================================================
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Idle");
    // TODO: アニメーション基盤ができたら player->PlayAnimation("Idle") を呼ぶ
}

void PlayerStateIdle::Update(Player* player) {
    if (!player) return;

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
}

void PlayerStateRun::Update(Player* player) {
    if (!player) return;

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
}

void PlayerStateJump::Update(Player* player) {
    if (!player) return;

    // 地面に着地したら Idle へ遷移
    if (player->IsGrounded()) {
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
}

void PlayerStateDash::Update(Player* player) {
    if (!player) return;

    // 約0.2秒(ダッシュ時間)経過で Idle へ戻す
    timer_ += 1.0f / 60.0f;
    if (timer_ >= 0.2f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateDash::Exit(Player* player) {}

// ========================================================
// 死亡状態 (Dead)
// ========================================================
void PlayerStateDead::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Dead");
    if (player) player->SetIsControlActive(false); // 操作不能にする
}

void PlayerStateDead::Update(Player* player) {
    // 死亡中は特に何もしない
}

void PlayerStateDead::Exit(Player* player) {}

// ========================================================
// 被弾・ノックバック状態 (Damage)
// ========================================================
void PlayerStateDamage::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Damage (Knockback)");
    if (player) {
        player->SetIsControlActive(false); // 操作不能にする
        
        // 初速をノックバック方向に与える
        float force = 15.0f;
        Vector3 v = knockbackDir_ * force;
        v.y = 8.0f; // 少し浮かす
        player->SetVelocity(v);
    }
}

void PlayerStateDamage::Update(Player* player) {
    if (!player) return;

    timer_ += 1.0f / 60.0f;

    // 時間経過で操作可能に戻し、Idleへ
    if (timer_ >= duration_) {
        player->SetIsControlActive(true);
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateDamage::Exit(Player* player) {
    if (player) player->SetIsControlActive(true);
}

// ========================================================
// 落下演出状態 (FallingOut)
// ========================================================
void PlayerStateFallingOut::Enter(Player* player) {
    if (!player) return;
    player->SetIsControlActive(false);

    // カメラの座標追従を停止 (位置固定・角度のみ追従)
    CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(true);

    phase_ = Phase::Waiting;
    waitTimer_ = 0.0f;
}

void PlayerStateFallingOut::Update(Player* player) {
    if (!player) return;

    // 共通処理: アイリスの中心をプレイヤーに合わせ続ける (IrisOut/IrisIn中)
    if (phase_ == Phase::IrisOut || phase_ == Phase::IrisIn) {
        Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
        Matrix4x4 vp = cam->GetViewProjectionMatrix();
        Vector3 worldPos = player->GetWorldPosition();
        worldPos.y += 1.0f;

        Vector3 ndc = Math::Transform(worldPos, vp);
        PostEffect::GetInstance()->GetParams()->irisCenterX = (ndc.x + 1.0f) * 0.5f;
        PostEffect::GetInstance()->GetParams()->irisCenterY = (1.0f - ndc.y) * 0.5f;
    }

    // フェーズごとの処理
    switch (phase_) {
    case Phase::Waiting:
        waitTimer_ += 1.0f / 60.0f;
        if (waitTimer_ >= 0.6f) {
            // アイリスアウト開始
            Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
            Matrix4x4 vp = cam->GetViewProjectionMatrix();
            Vector3 worldPos = player->GetWorldPosition();
            worldPos.y += 1.0f;
            Vector3 ndc = Math::Transform(worldPos, vp);
            Vector2 irisCenter = { (ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f };

            Fade::GetInstance()->StartIrisOut(1.0f, irisCenter);
            phase_ = Phase::IrisOut;
        }
        break;

    case Phase::IrisOut:
        if (Fade::GetInstance()->IsFinished()) {
            // 画面が閉じきった -> ワープ
            player->SetTranslate(player->GetRespawnPosition());
            player->SetVelocity({ 0,0,0 });

            // カメラの座標追従を一時的に戻して位置を同期
            CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
            CameraManager::GetInstance()->GetActiveCamera()->Update();
            CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(true); // 再び固定(演出用)

            // アイリスイン開始
            Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
            Matrix4x4 vp = cam->GetViewProjectionMatrix();
            Vector3 worldPos = player->GetWorldPosition();
            worldPos.y += 1.0f;
            Vector3 ndc = Math::Transform(worldPos, vp);
            Vector2 irisCenter = { (ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f };

            Fade::GetInstance()->StartIrisIn(0.5f, irisCenter);
            phase_ = Phase::IrisIn;
        }
        break;

    case Phase::IrisIn:
        if (Fade::GetInstance()->IsFinished()) {
            // 全ての演出終了
            Fade::GetInstance()->Stop();
            player->ChangeState(std::make_unique<PlayerStateIdle>());
            player->SetIsControlActive(true);
        }
        break;
    }
}

void PlayerStateFallingOut::Exit(Player* player) {
    if (player) {
        player->SetIsControlActive(true);
    }
    CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
}