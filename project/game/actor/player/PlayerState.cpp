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
// ========================================================
// フック移動状態 (Hook)
// ========================================================
void PlayerStateHook::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Hook");
    if (player) {
        player->SetIsControlActive(false);
        if (player->param_.has_value()) {
            oldGravity_ = player->param_->gravity;
            player->param_->gravity = 0.0f;
        }
        // ★ フックを撃っている間、プレイヤーは空中でピタッと静止する
        player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    }

    // FOV退避
    auto* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        oldFovY_ = cam->GetFovY();
    }
    spawnTimer_ = 0.0f;
    wobbleTimer_ = 0.0f;

    // ===============================================
    // ★ フック射出フェーズの初期化
    // ===============================================
    phase_ = Phase::kShootHook;
    if (player) {
        hookTipPos_ = player->GetWorldPosition();

        // 予測線用だったマーカーを「スライムの伸びる手」として再利用！
        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(true);
            marker->GetTransform()->translate = hookTipPos_;
            marker->GetTransform()->scale = { 1.5f, 1.5f, 1.5f }; // 少し大きめの手にする
        }
    }
}

void PlayerStateHook::Update(Player* player) {
    if (!player) return;

    float deltaTime = 1.0f / 60.0f; // Updateは固定フレーム想定

    if (phase_ == Phase::kShootHook) {
        // ===================================================
        // フェーズ1：フック（腕）が目標地点へ飛んでいく！
        // ===================================================
        Vector3 toTarget = targetPos_ - hookTipPos_;
        float dist = Math::Length(toTarget);

        if (dist < 5.0f) {
            // 目標に到達したらフェーズ2へ！
            hookTipPos_ = targetPos_;
            phase_ = Phase::kPullPlayer;
        }
        else {
            // フック先端の移動 (スピードは150.0fくらいがおすすめ)
            Vector3 dir = Math::Normalize(toTarget);
            float moveDist = 150.0f * deltaTime;
            hookTipPos_ = hookTipPos_ + dir * moveDist;
        }

        // ★ 魔法のコード：マーカーを「スライムから先端まで伸びる腕」に変形させる！
        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 startPos = player->GetWorldPosition();
            Vector3 diff = hookTipPos_ - startPos;
            float len = Math::Length(diff);

            // 本体と先端の中間地点に配置する
            marker->GetTransform()->translate = {
                startPos.x + diff.x * 0.5f,
                startPos.y + diff.y * 0.5f,
                startPos.z + diff.z * 0.5f
            };

            // 先端の方向に向かせる
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->GetTransform()->rotate = { angleX, angleY, 0.0f };
            marker->GetTransform()->isQuaternionMaster = false;

            // 長さを距離に合わせる (太さは0.5にしてスライムの腕っぽく)
            marker->GetTransform()->scale = { 0.5f, 0.5f, len };

            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
    else if (phase_ == Phase::kPullPlayer) {
        // ===================================================
        // フェーズ2：プレイヤー本体が目標地点へ引っ張られる！
        // ===================================================
        Vector3 currentPos = player->GetWorldPosition();
        Vector3 toTarget = targetPos_ - currentPos;
        float dist = Math::Length(toTarget);

        if (dist < 2.0f) {
            player->SetVelocity({ 0.0f, 0.0f, 0.0f });
            player->ChangeState(std::make_unique<PlayerStateIdle>());
            return;
        }

        Vector3 dir = Math::Normalize(toTarget);
        player->SetVelocity(dir * speed_);

        float targetAngle = std::atan2(dir.x, dir.z);
        player->SetRotationY(targetAngle);

        wobbleTimer_ += deltaTime;
        float wobble = std::sin(wobbleTimer_ * 20.0f) * 0.2f;
        Vector3 hookScale = { 1.0f + wobble, 1.0f + wobble, 4.5f - wobble * 2.0f };
        Vector3 currentScale = player->GetTransform()->scale;
        currentScale.x = Math::Lerp(currentScale.x, hookScale.x, 0.3f);
        currentScale.y = Math::Lerp(currentScale.y, hookScale.y, 0.3f);
        currentScale.z = Math::Lerp(currentScale.z, hookScale.z, 0.3f);
        player->SetScale(currentScale);

        // ★ 引っ張られている間も腕をピンと張ったまま繋いでおく！
        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 startPos = player->GetWorldPosition();
            Vector3 diff = targetPos_ - startPos;
            float len = Math::Length(diff);

            marker->GetTransform()->translate = {
                startPos.x + diff.x * 0.5f,
                startPos.y + diff.y * 0.5f,
                startPos.z + diff.z * 0.5f
            };
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->GetTransform()->rotate = { angleX, angleY, 0.0f };
            marker->GetTransform()->isQuaternionMaster = false;
            marker->GetTransform()->scale = { 0.5f, 0.5f, len };
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }

        auto* cam = CameraManager::GetInstance()->GetActiveCamera();
        if (cam) {
            float targetFov = oldFovY_ + 0.3f;
            cam->SetFovY(Math::Lerp(cam->GetFovY(), targetFov, 0.12f));
        }
    }
}

void PlayerStateHook::Exit(Player* player) {
    if (player) {
        player->SetIsControlActive(true);
        if (player->param_.has_value()) {
            player->param_->gravity = oldGravity_;
        }
        // 到達後に少し上に跳ねる
        Vector3 v = player->GetVelocity();
        player->SetVelocity({ v.x, 15.0f, v.z });
        player->SetScale({ 3.0f, 1.0f, 3.0f });

        // ★ 腕として使っていたマーカーを隠し、形を「元の球体」にリセットする！
        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(false);
            marker->GetTransform()->scale = { 1.0f, 1.0f, 1.0f }; // ←スケールを元に戻す（必要に応じて0.5fなどに調整してください）
            marker->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f }; // ←回転をリセット
        }
    }

    // FOVを元に戻す
    auto* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetFovY(oldFovY_);
    }
}