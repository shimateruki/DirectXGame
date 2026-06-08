#define NOMINMAX
#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "engine/utility/math/Math.h"
#include "DebugConsole.h"
#include "engine/graphics/postprocess/Fade.h"
#include "engine/graphics/postprocess/PostEffect.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include"BaseEnemy.h"
#include "EnemyGiantSlime.h"
#include "GimmickHookPullBlock.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include "GamePlayScene.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
Vector2 CalculatePlayerIrisCenter(Player* player) {
    Vector2 irisCenter = { 0.5f, 0.5f };
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (!player || !cam) {
        return irisCenter;
    }

    Vector3 worldPos = player->GetWorldPosition();
    worldPos.y += 1.0f;
    Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
    const bool isOnScreen =
        std::isfinite(ndc.x) &&
        std::isfinite(ndc.y) &&
        std::isfinite(ndc.z) &&
        ndc.z >= 0.0f &&
        std::abs(ndc.x) <= 1.15f &&
        std::abs(ndc.y) <= 1.15f;
    if (isOnScreen) {
        irisCenter = {
            std::clamp((ndc.x + 1.0f) * 0.5f, 0.18f, 0.82f),
            std::clamp((1.0f - ndc.y) * 0.5f, 0.16f, 0.78f)
        };
    }
    return irisCenter;
}

GamePlayScene* GetCurrentGamePlayScene() {
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager) {
        return nullptr;
    }
    return dynamic_cast<GamePlayScene*>(sceneManager->GetCurrentScene());
}

void StartLifeLostPresentationOnScene() {
    if (GamePlayScene* scene = GetCurrentGamePlayScene()) {
        const int afterLives = GameDataManager::GetInstance()->GetLives();
        scene->StartLifeLostPresentation(afterLives + 1, afterLives);
    }
}
}

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
    timer_ = 0.0f;
    sceneChangeRequested_ = false;
    lifePresentationStarted_ = false;
    Fade::GetInstance()->Stop();
    if (player) {
        player->SetVelocity({ 0.0f, 7.0f, 0.0f });
        Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
        if (cam) {
            cam->SetFreezeEye(true);
        }
        irisCenter_ = CalculatePlayerIrisCenter(player);
    }
    if (player) player->SetIsControlActive(false); // 操作不能にする
}

void PlayerStateDead::Update(Player* player) {
    if (!player || sceneChangeRequested_) return;

    constexpr float kDeltaTime = 1.0f / 60.0f;
    timer_ += kDeltaTime;

    if (timer_ >= 1.15f && !lifePresentationStarted_ && Fade::GetInstance()->GetStatus() == Fade::Status::None) {
        Fade::GetInstance()->StartIrisOut(1.35f, irisCenter_);
    }

    if (!lifePresentationStarted_ && Fade::GetInstance()->IsFinished()) {
        Fade::GetInstance()->Stop();
        lifePresentationStarted_ = true;
        StartLifeLostPresentationOnScene();
        return;
    }

    if (!lifePresentationStarted_) {
        return;
    }

    GamePlayScene* scene = GetCurrentGamePlayScene();
    if (scene && !scene->IsLifeLostPresentationFinished()) {
        return;
    }

    sceneChangeRequested_ = true;
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetFreezeEye(false);
    }

    if (GameDataManager::GetInstance()->GetLives() <= 0) {
        Fade::GetInstance()->Stop();
        SceneManager::GetInstance()->ChangeScene("GAMEOVER");
    } else {
        GameDataManager::GetInstance()->RequestRespawnIrisIn();
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
    }

    // 死亡中は特に何もしない
}

void PlayerStateDead::Exit(Player* player) {
    if (player) {
        player->SetIsControlActive(true);
    }
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetFreezeEye(false);
    }
}

// ========================================================
// 被弾・ノックバック状態 (Damage)
// ========================================================
void PlayerStateDamage::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Damage (Knockback)");
    if (player) {
        player->SetIsControlActive(false); // 操作不能にする
        
        // 初速をノックバック方向に与える
        timer_ = 0.0f;
        baseScale_ = player->GetScale();
        baseRotation_ = player->GetRotation();

        knockbackDir_.y = 0.0f;
        float dirLength = Math::Length(knockbackDir_);
        if (dirLength > 0.001f) {
            knockbackDir_ = knockbackDir_ * (1.0f / dirLength);
        } else {
            knockbackDir_ = {
                -std::sin(baseRotation_.y),
                0.0f,
                -std::cos(baseRotation_.y)
            };
        }

        float force = 18.0f;
        Vector3 v = knockbackDir_ * force;
        v.y = 9.0f;
        player->SetVelocity(v);
    }
}

void PlayerStateDamage::Update(Player* player) {
    if (!player) return;

    const float deltaTime = 1.0f / 60.0f;
    timer_ += deltaTime;

    float t = timer_ / duration_;
    if (t > 1.0f) t = 1.0f;

    Vector3 targetScale = baseScale_;
    Vector3 targetRotation = baseRotation_;
    const float roll = t * 6.28318531f;
    const float rollEaseOut = 1.0f - (1.0f - t) * (1.0f - t);

    if (t < 0.16f) {
        float p = t / 0.16f;
        float ease = 1.0f - (1.0f - p) * (1.0f - p);
        targetScale = {
            baseScale_.x * (1.0f + 0.62f * ease),
            baseScale_.y * (1.0f - 0.52f * ease),
            baseScale_.z * (1.0f + 0.62f * ease)
        };
    } else if (t < 0.48f) {
        float p = (t - 0.16f) / 0.32f;
        float ease = 1.0f - (1.0f - p) * (1.0f - p);
        targetScale = {
            Math::Lerp(baseScale_.x * 1.62f, baseScale_.x * 0.72f, ease),
            Math::Lerp(baseScale_.y * 0.48f, baseScale_.y * 1.48f, ease),
            Math::Lerp(baseScale_.z * 1.62f, baseScale_.z * 0.72f, ease)
        };
    } else {
        float p = (t - 0.48f) / 0.52f;
        if (p > 1.0f) p = 1.0f;
        float wobble = std::sin(p * 3.14159265f * 4.0f) * (1.0f - p);
        targetScale = {
            baseScale_.x * (1.0f + wobble * 0.28f),
            baseScale_.y * (1.0f - wobble * 0.22f),
            baseScale_.z * (1.0f + wobble * 0.28f)
        };
    }

    targetRotation.x += knockbackDir_.z * roll;
    targetRotation.z -= knockbackDir_.x * roll;
    targetRotation.y += std::sin(t * 3.14159265f) * 0.35f * (knockbackDir_.x + knockbackDir_.z);
    targetScale.x += std::sin(rollEaseOut * 3.14159265f * 2.0f) * baseScale_.x * 0.08f;
    targetScale.z += std::cos(rollEaseOut * 3.14159265f * 2.0f) * baseScale_.z * 0.06f;

    player->SetScale(targetScale);
    player->SetRotation(targetRotation);

    // 時間経過で操作可能に戻し、Idleへ
    if (timer_ >= duration_) {
        player->SetIsControlActive(true);
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateDamage::Exit(Player* player) {
    if (player) {
        player->SetIsControlActive(true);
        player->SetScale(baseScale_);
        player->SetRotation(baseRotation_);
    }
}

// ========================================================
// 落下演出状態 (FallingOut)
// ========================================================
void PlayerStateFallingOut::Enter(Player* player) {
    if (!player) return;
    player->SetIsControlActive(false);
    irisCenter_ = CalculatePlayerIrisCenter(player);
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetFreezeEye(true);
    }

    phase_ = Phase::Waiting;
    waitTimer_ = 0.0f;
}

void PlayerStateFallingOut::Update(Player* player) {
    if (!player) return;

    // 共通処理: アイリスの中心をプレイヤーに合わせ続ける (IrisOut/IrisIn中)
    if (phase_ == Phase::IrisOut || phase_ == Phase::IrisIn) {
        PostEffect::GetInstance()->GetParams()->irisCenterX = irisCenter_.x;
        PostEffect::GetInstance()->GetParams()->irisCenterY = irisCenter_.y;
    }

    // フェーズごとの処理
    switch (phase_) {
    case Phase::Waiting:
        waitTimer_ += 1.0f / 60.0f;
        if (waitTimer_ >= 1.55f) {
            // アイリスアウト開始
            Fade::GetInstance()->StartIrisOut(1.35f, irisCenter_);
            phase_ = Phase::IrisOut;
        }
        break;

    case Phase::IrisOut:
        if (Fade::GetInstance()->IsFinished()) {
            Fade::GetInstance()->Stop();
            StartLifeLostPresentationOnScene();
            phase_ = Phase::LifeLost;
            return;
            if (GameDataManager::GetInstance()->GetLives() <= 0) {
                CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
                SceneManager::GetInstance()->ChangeScene("GAMEOVER");
                return;
            }

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

    case Phase::LifeLost:
        if (GamePlayScene* scene = GetCurrentGamePlayScene()) {
            if (!scene->IsLifeLostPresentationFinished()) {
                break;
            }
        }

        if (GameDataManager::GetInstance()->GetLives() <= 0) {
            CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
            Fade::GetInstance()->Stop();
            SceneManager::GetInstance()->ChangeScene("GAMEOVER");
            return;
        }

        player->SetTranslate(player->GetRespawnPosition());
        player->SetVelocity({ 0,0,0 });

        CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
        CameraManager::GetInstance()->GetActiveCamera()->Update();
        CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(true);

        irisCenter_ = CalculatePlayerIrisCenter(player);
        if (GamePlayScene* scene = GetCurrentGamePlayScene()) {
            scene->HideLifeLostPresentationOverlay();
        }
        Fade::GetInstance()->StartIrisIn(1.25f, irisCenter_);
        phase_ = Phase::IrisIn;
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
        // フックを射出している間、プレイヤーは空中で静止する
        player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    }

    // FOV退避
    auto* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        oldFovY_ = cam->GetFovY();
    }
    spawnTimer_ = 0.0f;
    wobbleTimer_ = 0.0f;

    // フック射出フェーズの初期化
    phase_ = Phase::kShootHook;
    if (player) {
        hookTipPos_ = player->GetWorldPosition();

        // 予測線用マーカーをスライムの伸びる手として再利用
        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(true);
            marker->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // フックとして使う時は白色に戻す
            marker->GetTransform()->translate = hookTipPos_;
            marker->GetTransform()->scale = { 1.5f, 1.5f, 1.5f }; // 先端を少し大きめの手にする
        }
    }
}

void PlayerStateHook::Update(Player* player) {
    if (!player) return;

    float deltaTime = 1.0f / 60.0f; // Updateは固定フレーム想定

    if (phase_ == Phase::kShootHook) {
        // フェーズ1：フック（腕）が目標地点へ飛んでいく
        Vector3 toTarget = targetPos_ - hookTipPos_;
        float dist = Math::Length(toTarget);

        if (dist < 5.0f) {
            // 目標に到達したらフェーズ2へ
            hookTipPos_ = targetPos_;
            phase_ = Phase::kPullPlayer;
        }
        else {
            // フック先端の移動
            Vector3 dir = Math::Normalize(toTarget);
            float moveDist = 150.0f * deltaTime;
            hookTipPos_ = hookTipPos_ + dir * moveDist;
        }

            // マーカーをスライムから先端まで伸びる腕として変形
            Object3d* marker = player->GetHookMarker();
            if (marker) {
                Vector3 startPos = player->GetWorldPosition();
                Vector3 diff = hookTipPos_ - startPos;
                float len = Math::Length(diff);

                // 本体と先端の中間地点に配置
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

                // 距離に応じた太さの変化（体積保存のシミュレーション）
                float baseThickness = std::max(0.15f, 0.8f - len * 0.015f);
                // サイン波による有機的な波打ち
                float wobble = std::sin(len * 0.2f) * 0.1f;
                marker->GetTransform()->scale = { baseThickness + wobble, baseThickness - wobble, len };

                marker->UpdateLocalMatrix();
                marker->UpdateWorldMatrix();
            }
    }
    else if (phase_ == Phase::kPullPlayer) {
        // フェーズ2：プレイヤー本体が目標地点へ引っ張られる
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

        // 移動中も腕をターゲット地点に繋いでおく
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
            // 近づくにつれて太さが戻る
            float t = std::max(0.0f, std::min(1.0f, 1.0f - (len / Math::Length(targetPos_ - hookTipPos_))));
            float thickness = Math::Lerp(0.15f, 1.0f, t);
            // テンションによる振動演出
            float wobble = std::sin(wobbleTimer_ * 40.0f) * (1.0f - t) * 0.2f;
            marker->GetTransform()->scale = { thickness + wobble, thickness - wobble, len };
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

        // 使用していたマーカーを非表示にし、形状をリセット
        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(false);
            marker->GetTransform()->scale = { 1.0f, 1.0f, 1.0f }; 
            marker->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f }; 
            marker->GetTransform()->isQuaternionMaster = true; // 回転モードを元に戻す
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }

    // FOVを元に戻す
    auto* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetFovY(oldFovY_);
    }
}

// ========================================================
// 敵引き寄せ状態 (Pull Enemy)
// ========================================================
void PlayerStateSwingHook::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: SwingHook");
    if (!player) return;

    player->SetIsControlActive(false);
    if (player->param_.has_value()) {
        oldGravity_ = player->param_->gravity;
        player->param_->gravity = 0.0f;
    }

    auto* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        oldFovY_ = cam->GetFovY();
        cam->SetAimCameraSuppressed(true);
        cam->SnapToThirdPerson(15.0f, 3.2f, 0.28f);
        cam->SetFovY(oldFovY_);
        cam->Update();
    }

    released_ = false;
    timer_ = 0.0f;
    phase_ = Phase::kShootHook;
    hookTipPos_ = player->GetWorldPosition();
    releaseVelocity_ = player->GetVelocity();
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });

    Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(true);
        marker->SetColor({ 0.2f, 0.85f, 1.0f, 1.0f });
        marker->GetTransform()->translate = hookTipPos_;
        marker->GetTransform()->scale = { 0.35f, 0.35f, 1.0f };
        marker->GetTransform()->isQuaternionMaster = false;
    }
}

void PlayerStateSwingHook::Update(Player* player) {
    if (!player) return;

    const float deltaTime = 1.0f / 60.0f;
    timer_ += deltaTime;

    if (phase_ == Phase::kShootHook) {
        Vector3 toAnchor = anchorPos_ - hookTipPos_;
        float dist = Math::Length(toAnchor);
        if (dist < 4.0f) {
            hookTipPos_ = anchorPos_;
            phase_ = Phase::kSwing;

            Vector3 playerPos = player->GetWorldPosition();
            Vector3 radial = playerPos - anchorPos_;
            ropeLength_ = Math::Length(radial);
            if (ropeLength_ < 5.0f) {
                ropeLength_ = 5.0f;
                radial = { 0.0f, -1.0f, 0.0f };
                player->SetTranslate(anchorPos_ + radial * ropeLength_);
            } else {
                radial = Math::Normalize(radial);
            }

            Vector3 tangent = { 1.0f, 0.0f, 0.0f };
            auto* cam = CameraManager::GetInstance()->GetActiveCamera();
            if (cam) {
                Vector3 camRot = cam->GetRotation();
                Vector3 camForward = {
                    std::sin(camRot.y) * std::cos(camRot.x),
                    -std::sin(camRot.x),
                    std::cos(camRot.y) * std::cos(camRot.x)
                };
                tangent = camForward - radial * Math::Dot(camForward, radial);
            }
            if (Math::Length(tangent) < 0.01f) {
                tangent = Math::Cross({ 0.0f, 1.0f, 0.0f }, radial);
            }
            if (Math::Length(tangent) < 0.01f) {
                tangent = { 1.0f, 0.0f, 0.0f };
            }
            swingVelocity_ = Math::Normalize(tangent) * 34.0f;
            if (swingVelocity_.y < 6.0f) {
                swingVelocity_.y = 6.0f;
            }
            releaseVelocity_ = swingVelocity_;
            if (cam) {
                cam->SnapToThirdPerson(15.0f, 3.2f, 0.28f);
                cam->SetFovY(oldFovY_);
                cam->Update();
            }
            DebugConsole::GetInstance()->AddLog("SwingHook attached");
        } else {
            Vector3 dir = Math::Normalize(toAnchor);
            hookTipPos_ = hookTipPos_ + dir * (190.0f * deltaTime);
        }

        UpdateRopeMarker(player, hookTipPos_, 0.22f);
        return;
    }

    Vector3 pos = player->GetWorldPosition();
    Vector3 radial = pos - anchorPos_;
    if (Math::Length(radial) < 0.01f) {
        radial = { 0.0f, -1.0f, 0.0f };
    } else {
        radial = Math::Normalize(radial);
    }

    swingVelocity_.y -= 14.0f * deltaTime;

    InputManager* input = player->GetInputManager();
    if (input) {
        Vector3 pump = { 0.0f, 0.0f, 0.0f };
        auto* cam = CameraManager::GetInstance()->GetActiveCamera();
        if (cam) {
            Vector3 camRot = cam->GetRotation();
            Vector3 forward = { std::sin(camRot.y), 0.0f, std::cos(camRot.y) };
            Vector3 right = { std::cos(camRot.y), 0.0f, -std::sin(camRot.y) };
            if (input->IsActionPressed("Forward")) pump += forward;
            if (input->IsActionPressed("Backward")) pump = pump - forward;
            if (input->IsActionPressed("Right")) pump += right;
            if (input->IsActionPressed("Left")) pump = pump - right;
        }

        if (Math::Length(pump) > 0.01f) {
            pump = Math::Normalize(pump);
            pump = pump - radial * Math::Dot(pump, radial);
            if (Math::Length(pump) > 0.01f) {
                swingVelocity_ += Math::Normalize(pump) * (26.0f * deltaTime);
            }
        }

        if (timer_ > 0.18f && (input->IsActionTriggered("Jump") || input->IsMouseButtonTriggered(1))) {
            Release(player);
            return;
        }
    }

    swingVelocity_ = swingVelocity_ - radial * Math::Dot(swingVelocity_, radial);
    float speed = Math::Length(swingVelocity_);
    if (speed > 56.0f) {
        swingVelocity_ = Math::Normalize(swingVelocity_) * 56.0f;
    }

    Vector3 nextPos = pos + swingVelocity_ * deltaTime;
    Vector3 nextRadial = nextPos - anchorPos_;
    if (Math::Length(nextRadial) < 0.01f) {
        nextRadial = radial;
    } else {
        nextRadial = Math::Normalize(nextRadial);
    }
    nextPos = anchorPos_ + nextRadial * ropeLength_;
    swingVelocity_ = swingVelocity_ - nextRadial * Math::Dot(swingVelocity_, nextRadial);

    RaycastHit groundHit = CollisionManager::GetInstance()->Raycast(
        nextPos + Vector3{ 0.0f, 5.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f },
        12.0f,
        kGround
    );
    if (groundHit.isHit) {
        const float minPlayerY = groundHit.hitPoint.y + 1.15f;
        if (nextPos.y < minPlayerY) {
            nextPos.y = minPlayerY;
            if (swingVelocity_.y < 0.0f) {
                swingVelocity_.y = 0.0f;
            }
            releaseVelocity_.y = std::max(releaseVelocity_.y, 4.0f);
        }
    }

    Vector3 frameVelocity = (nextPos - pos) / deltaTime;
    player->SetVelocity(frameVelocity);
    releaseVelocity_ = frameVelocity;

    Vector3 flatVel = { swingVelocity_.x, 0.0f, swingVelocity_.z };
    if (Math::Length(flatVel) > 0.1f) {
        player->SetRotationY(std::atan2(flatVel.x, flatVel.z));
    }

    float stretch = std::min(Math::Length(swingVelocity_) / 56.0f, 1.0f);
    float flutter = std::sin(timer_ * 14.0f) * stretch;
    player->SetScale({
        3.0f + 0.10f * stretch + 0.07f * flutter,
        1.0f + 0.08f * stretch - 0.04f * flutter,
        3.0f - 0.06f * stretch + 0.05f * flutter
    });

    Vector3 rot = player->GetRotation();
    rot.x = -swingVelocity_.z * 0.006f;
    rot.z = swingVelocity_.x * 0.006f;
    player->SetRotation(rot);

    UpdateRopeMarker(player, anchorPos_, 0.18f);

    if (timer_ > 3.0f) {
        Release(player);
    }
}

void PlayerStateSwingHook::Exit(Player* player) {
    if (player) {
        player->SetIsControlActive(true);
        if (player->param_.has_value()) {
            player->param_->gravity = oldGravity_;
        }

        if (released_) {
            if (releaseVelocity_.y < 8.0f) releaseVelocity_.y = 8.0f;
            player->SetVelocity(releaseVelocity_);
        }
        player->SetScale({ 3.0f, 1.0f, 3.0f });
        Vector3 rot = player->GetRotation();
        rot.x = 0.0f;
        rot.z = 0.0f;
        player->SetRotation(rot);

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(false);
            marker->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
            marker->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
            marker->GetTransform()->isQuaternionMaster = true;
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }

    auto* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetAimCameraSuppressed(false);
        cam->SetFovY(oldFovY_);
    }
}

void PlayerStateSwingHook::UpdateRopeMarker(Player* player, const Vector3& endPos, float thickness) {
    if (!player) return;

    Object3d* marker = player->GetHookMarker();
    if (!marker) return;

    Vector3 startPos = player->GetWorldPosition();
    Vector3 diff = endPos - startPos;
    float len = Math::Length(diff);
    if (len < 0.01f) len = 0.01f;

    marker->SetIsVisible(true);
    marker->SetColor({ 0.2f, 0.85f, 1.0f, 1.0f });
    marker->GetTransform()->translate = startPos + diff * 0.5f;
    marker->GetTransform()->rotate = {
        std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z)),
        std::atan2(diff.x, diff.z),
        0.0f
    };
    marker->GetTransform()->isQuaternionMaster = false;
    marker->GetTransform()->scale = { thickness, thickness, len };
    marker->UpdateLocalMatrix();
    marker->UpdateWorldMatrix();
}

void PlayerStateSwingHook::Release(Player* player) {
    if (!player) return;
    released_ = true;
    DebugConsole::GetInstance()->AddLog("SwingHook released");
    player->ChangeState(std::make_unique<PlayerStateIdle>());
}

void PlayerStatePullEnemy::Enter(Player* player) {
    if (!player || !targetEnemy_) return;

    player->SetIsControlActive(false);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f }); // プレイヤー自身はその場で停止
    isHeavyPullTarget_ = dynamic_cast<EnemyGiantSlime*>(targetEnemy_) != nullptr;

    phase_ = Phase::kShootHook;
    hookTipPos_ = player->GetWorldPosition();

    Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(true);
        marker->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // フックとして使用する際は白色に設定
        marker->GetTransform()->translate = hookTipPos_;
    }
}

void PlayerStatePullEnemy::Update(Player* player) {
    if (!player || !targetEnemy_) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
        return;
    }

    float deltaTime = 1.0f / 60.0f;
    Vector3 playerPos = player->GetWorldPosition();

    if (phase_ == Phase::kShootHook) {
        // 腕が敵に向かって伸びる
        Vector3 enemyPos = targetEnemy_->GetTransform()->translate;
        Vector3 toTarget = enemyPos - hookTipPos_;
        float dist = Math::Length(toTarget);

        if (dist < 5.0f) {
            hookTipPos_ = enemyPos;
            phase_ = Phase::kPullEnemy;
            enemyStartPos_ = enemyPos;
            pullTimer_ = -0.12f; // ヒットストップ：命中後0.12秒間タメる
            
            if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_)) {
                isHeavyPullTarget_ = true;
                giantSlime->BeginHookSplitPull(playerPos);
            } else {
                // 命中時に敵を「Carried（持ち運び）」状態へ移行させ、衝突判定を無効化
                BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(targetEnemy_);
                if (enemyBase) {
                    enemyBase->SetCarried(true);
                }
            }

            // 命中した瞬間の火花（パーティクル）
            if (player->GetParticleSystem()) {
                Vector3 toPlayer = Math::Normalize(playerPos - enemyPos);
                player->GetParticleSystem()->SpawnParticles(
                    enemyPos, 30, 2.0f, &toPlayer, 30.0f,
                    {1.0f, 1.0f, 0.8f, 1.0f}, {1.0f, 0.8f, 0.2f, 0.0f},
                    0.2f, 0.4f, 0.8f, 0.1f
                );
            }
        }
        else {
            Vector3 dir = Math::Normalize(toTarget);
            hookTipPos_ = hookTipPos_ + dir * (150.0f * deltaTime);
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 diff = hookTipPos_ - playerPos;
            marker->GetTransform()->translate = {
                playerPos.x + diff.x * 0.5f, playerPos.y + diff.y * 0.5f, playerPos.z + diff.z * 0.5f
            };
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->GetTransform()->rotate = { angleX, angleY, 0.0f };
            marker->GetTransform()->isQuaternionMaster = false;
            marker->GetTransform()->scale = { 0.5f, 0.5f, Math::Length(diff) };

            // ===============================================
            // フェーズ1のフック描画更新を行う
            // ===============================================
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
    else if (phase_ == Phase::kPullEnemy) {
        // --- 敵を自分の手元へ引っ張る（放物線＆巻き取り） ---
        pullTimer_ += deltaTime;

        // 【演出】ヒットストップ：時間がマイナスの間は引き寄せず、お互いに激しくブルブル震える
        if (pullTimer_ < 0.0f) {
            float shakeX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
            float shakeY = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
            float shakeZ = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
            
            targetEnemy_->GetTransform()->translate = {
                enemyStartPos_.x + shakeX, enemyStartPos_.y + shakeY, enemyStartPos_.z + shakeZ
            };
            targetEnemy_->GetTransform()->isQuaternionMaster = false;
            targetEnemy_->UpdateLocalMatrix();
            targetEnemy_->UpdateWorldMatrix();
            
            Object3d* marker = player->GetHookMarker();
            if (marker) {
                Vector3 diff = enemyStartPos_ - playerPos;
                marker->GetTransform()->translate = {
                    playerPos.x + diff.x * 0.5f + shakeX * 0.5f, 
                    playerPos.y + diff.y * 0.5f + shakeY * 0.5f, 
                    playerPos.z + diff.z * 0.5f + shakeZ * 0.5f
                };
                // ヒットストップ中は「ピンッ」と極限まで細く張り詰める
                float len = Math::Length(diff);
                marker->GetTransform()->scale = { 0.15f, 0.15f, len };

                marker->UpdateLocalMatrix();
                marker->UpdateWorldMatrix();
            }
            return; // これ以上は更新しない（時を止める）
        }

        if (isHeavyPullTarget_) {
            auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_);
            if (!giantSlime || giantSlime->HasSplit()) {
                player->ChangeState(std::make_unique<PlayerStateIdle>());
                return;
            }

            bool hasSplit = giantSlime->UpdateHookSplitPull(deltaTime, playerPos, player->GetParticleSystem());
            float progress = giantSlime->GetHookSplitProgress();
            float strain = (1.0f - progress * 0.45f) * 0.55f;
            player->GetTransform()->scale = { 1.0f + strain, 1.0f - strain * 0.7f, 1.0f + strain };

            Object3d* marker = player->GetHookMarker();
            if (marker) {
                Vector3 enemyCurrentPos = targetEnemy_->GetTransform()->translate;
                Vector3 diff = enemyCurrentPos - playerPos;
                float len = Math::Length(diff);
                if (len < 0.01f) len = 0.01f;

                marker->GetTransform()->translate = playerPos + diff * 0.5f;
                marker->GetTransform()->rotate = {
                    std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z)),
                    std::atan2(diff.x, diff.z),
                    0.0f
                };
                marker->GetTransform()->isQuaternionMaster = false;

                float tension = std::sin(pullTimer_ * 56.0f) * (1.0f - progress) * 0.16f;
                float thickness = Math::Lerp(0.18f, 0.08f, progress);
                marker->GetTransform()->scale = { thickness + tension, thickness - tension, len };
                marker->SetColor({ 0.35f + progress * 0.6f, 0.95f, 1.0f, 1.0f });
                marker->UpdateLocalMatrix();
                marker->UpdateWorldMatrix();
            }

            if (hasSplit) {
                player->GetTransform()->scale = { 2.4f, 0.45f, 2.4f };
                player->ChangeState(std::make_unique<PlayerStateIdle>());
            }
            return;
        }

        const float kPullDuration = 0.45f; // 少しだけ時間を長くしてタメを作る
        float t = pullTimer_ / kPullDuration;
        if (t > 1.0f) t = 1.0f;

        // 【案B：ふんばり演出】引き寄せている間、プレイヤーが力強く踏ん張る（少し潰れて横に広がる）
        float strain = (1.0f - (t * t)) * 0.5f; 
        player->GetTransform()->scale = { 1.0f + strain, 1.0f - strain, 1.0f + strain };

        // 【演出1】イージング：最初は重たく、後半一気に飛んでくる（Ease-In）
        float easeT = t * t * t; 

        // 【演出2】敵の回転：引き寄せられながら超高速できりもみ回転する
        Vector3 rot = targetEnemy_->GetTransform()->rotate;
        rot.x += 20.0f * deltaTime;
        rot.y += 35.0f * deltaTime;
        rot.z += 15.0f * deltaTime;
        targetEnemy_->GetTransform()->rotate = rot;
        targetEnemy_->GetTransform()->isQuaternionMaster = false; // 追加: クォータニオンを無視してオイラー角回転を適用

        // 【演出3】敵の縮小：手元に来るにつれて小さく圧縮される（毛糸玉化）
        // ※元の大きさが1.0として、手元で0.6くらいまで小さくなる
        float scale = Math::Lerp(1.0f, 0.6f, easeT);
        targetEnemy_->GetTransform()->scale = {scale, scale, scale};

        // 目標位置（プレイヤーの頭上）
        Vector3 headPos = { playerPos.x, playerPos.y + 2.5f, playerPos.z };

        // easeTを使って開始位置から目標位置への線形補間
        Vector3 basePos = {
            Math::Lerp(enemyStartPos_.x, headPos.x, easeT),
            Math::Lerp(enemyStartPos_.y, headPos.y, easeT),
            Math::Lerp(enemyStartPos_.z, headPos.z, easeT)
        };
        
        // サイン波でY軸に放物線のアーチを加える（easeTではなく純粋なtで綺麗なアーチにする）
        float arcHeight = 6.0f; 
        basePos.y += std::sin(t * 3.14159265f) * arcHeight;

        targetEnemy_->GetTransform()->translate = basePos;

        if (t >= 1.0f) {
            // 【演出】頭に乗った（キャッチした）瞬間の衝撃エフェクト
            if (player->GetParticleSystem()) {
                Vector3 headPos = { playerPos.x, playerPos.y + 2.5f, playerPos.z };
                player->GetParticleSystem()->SpawnParticles(
                    headPos, 20, 1.5f, nullptr, 20.0f,
                    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
                    0.2f, 0.4f, 0.6f, 0.05f
                );
            }

            // 【案B：着地時の潰れ演出】頭に乗った瞬間にベチャッと大きく潰れる！
            // このあと Player::Update の復元処理で自然にプルンと戻ります
            player->GetTransform()->scale = { 2.2f, 0.4f, 2.2f };

            player->SetCarriedEnemy(targetEnemy_);
            player->ChangeState(std::make_unique<PlayerStateIdle>());
            return;
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 enemyCurrentPos = targetEnemy_->GetTransform()->translate;
            Vector3 diff = enemyCurrentPos - playerPos;
            marker->GetTransform()->translate = {
                playerPos.x + diff.x * 0.5f, playerPos.y + diff.y * 0.5f, playerPos.z + diff.z * 0.5f
            };
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->GetTransform()->rotate = { angleX, angleY, 0.0f };
            marker->GetTransform()->isQuaternionMaster = false;
            // 手元に近づくにつれて太く戻る（イージングを利用）
            float len = Math::Length(diff);
            float thickness = Math::Lerp(0.15f, 1.0f, easeT);
            // 引っ張る反動でブルンブルン震える（近づくと収まる）
            float wobble = std::sin(pullTimer_ * 50.0f) * (1.0f - easeT) * 0.2f;
            marker->GetTransform()->scale = { thickness + wobble, thickness - wobble, len };

            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
}
void PlayerStatePullEnemy::Exit(Player* player) {
    player->SetIsControlActive(true);
    if (player) {
        if (isHeavyPullTarget_) {
            if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_)) {
                if (!giantSlime->HasSplit()) {
                    giantSlime->CancelHookSplitPull();
                }
            }
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(false);
            marker->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
            marker->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
            marker->GetTransform()->isQuaternionMaster = true; // 回転モードの復帰
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
}
// ========================================================
// 敵持ち運び状態 (Carry)
// ========================================================
void PlayerStatePullObject::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: PullObject");
    if (!player) return;

    player->SetIsControlActive(false);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    hookTipPos_ = player->GetWorldPosition();
    timer_ = 0.0f;
    pullStarted_ = false;

    Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(true);
        marker->SetColor({ 0.35f, 0.9f, 1.0f, 1.0f });
        marker->GetTransform()->translate = hookTipPos_;
    }
}

void PlayerStatePullObject::Update(Player* player) {
    if (!player || !targetObject_) {
        if (player) player->ChangeState(std::make_unique<PlayerStateIdle>());
        return;
    }

    const float deltaTime = 1.0f / 60.0f;
    timer_ += deltaTime;

    Vector3 targetPos = targetObject_->GetWorldPosition();
    Vector3 toTarget = targetPos - hookTipPos_;
    float dist = Math::Length(toTarget);

    if (!pullStarted_) {
        if (dist < 3.0f) {
            hookTipPos_ = targetPos;
            if (auto* block = dynamic_cast<GimmickHookPullBlock*>(targetObject_)) {
                block->StartHookPull(player->GetWorldPosition());
            }
            pullStarted_ = true;
            timer_ = 0.0f;
        } else {
            Vector3 dir = Math::Normalize(toTarget);
            hookTipPos_ = hookTipPos_ + dir * (180.0f * deltaTime);
        }
        UpdateRopeMarker(player, hookTipPos_, 0.22f);
        return;
    }

    UpdateRopeMarker(player, targetObject_->GetWorldPosition(), 0.18f);

    if (timer_ > 0.35f) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStatePullObject::Exit(Player* player) {
    if (!player) return;

    player->SetIsControlActive(true);
    Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(false);
        marker->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
        marker->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
        marker->GetTransform()->isQuaternionMaster = true;
        marker->UpdateLocalMatrix();
        marker->UpdateWorldMatrix();
    }
}

void PlayerStatePullObject::UpdateRopeMarker(Player* player, const Vector3& endPos, float thickness) {
    if (!player) return;

    Object3d* marker = player->GetHookMarker();
    if (!marker) return;

    Vector3 startPos = player->GetWorldPosition();
    Vector3 diff = endPos - startPos;
    float len = Math::Length(diff);
    if (len < 0.01f) len = 0.01f;

    marker->SetIsVisible(true);
    marker->SetColor({ 0.35f, 0.9f, 1.0f, 1.0f });
    marker->GetTransform()->translate = startPos + diff * 0.5f;
    marker->GetTransform()->rotate = {
        std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z)),
        std::atan2(diff.x, diff.z),
        0.0f
    };
    marker->GetTransform()->isQuaternionMaster = false;
    marker->GetTransform()->scale = { thickness, thickness, len };
    marker->UpdateLocalMatrix();
    marker->UpdateWorldMatrix();
}

void PlayerStateCarry::Enter(Player* player) {
    if (player) {
        player->SetIsControlActive(true); // 持ち運び中もプレイヤーは動ける！
        struggleTimer_ = 0.0f;

        Object3d* enemy = player->GetCarriedEnemy();
        if (enemy) {
            // 敵の衝突判定を無効化（kNone等）して無力化する
            // ※ColliderのSetAttribute等があればここで当たり判定を無効化します
            // enemy->GetCollider()->SetAttribute(0); 
        }
    }
}

void PlayerStateCarry::Update(Player* player) {
    if (!player) return;

    struggleTimer_ += 1.0f / 60.0f;

    Object3d* enemy = player->GetCarriedEnemy();
    if (enemy) {
        Vector3 playerPos = player->GetWorldPosition();
        
        // 【抗っている感の演出（汎用プロシージャルアニメーション）】
        // 1. 座標の揺れ（ジタバタと細かく暴れる）
        float offsetX = std::sin(struggleTimer_ * 35.0f) * 0.15f;
        float offsetZ = std::cos(struggleTimer_ * 30.0f) * 0.15f;
        float offsetY = std::sin(struggleTimer_ * 45.0f) * 0.08f;

        enemy->GetTransform()->translate = { 
            playerPos.x + offsetX, 
            playerPos.y + 2.5f + offsetY, 
            playerPos.z + offsetZ 
        };

        // 2. 回転の揺れ（体をよじる、イヤイヤと暴れる動き）
        Vector3 rot;
        rot.x = std::sin(struggleTimer_ * 20.0f) * 0.2f;
        // プレイヤーの現在のY軸回転を基準に、首振り角度をオフセットとして適用する
        rot.y = player->GetRotation().y + std::sin(struggleTimer_ * 15.0f) * 0.4f; 
        rot.z = std::cos(struggleTimer_ * 22.0f) * 0.2f;
        enemy->GetTransform()->rotate = rot;

        // 3. スケールの伸縮（息遣いや力を込めるような Squash & Stretch）
        // 0.6 を基準サイズとして伸縮
        float baseScale = 0.6f;
        float stretch = std::sin(struggleTimer_ * 25.0f) * 0.05f;
        enemy->GetTransform()->scale = { 
            baseScale - stretch, 
            baseScale + stretch, 
            baseScale - stretch 
        };

        // 追加: クォータニオンを無視させ、手動でマトリックスを更新する
        enemy->GetTransform()->isQuaternionMaster = false;
        enemy->UpdateLocalMatrix();
        enemy->UpdateWorldMatrix();
    }

    // （※ここに後で「左クリックで投げる」処理を追加します）
}

void PlayerStateCarry::Exit(Player* player) {
    // 投げた時などに呼ばれる
    if (player) {
        Object3d* enemy = player->GetCarriedEnemy();
        if (enemy) {
            // 持ち運びが終わったら（投げる等）、姿勢とスケールを元に戻す
            enemy->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
            enemy->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
            enemy->GetTransform()->isQuaternionMaster = true; // クォータニオンモードに戻す
            enemy->UpdateLocalMatrix();
            enemy->UpdateWorldMatrix();
        }
        player->SetCarriedEnemy(nullptr);
    }
}
