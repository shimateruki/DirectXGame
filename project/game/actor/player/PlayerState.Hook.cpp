#define NOMINMAX
#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "DebugConsole.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include "engine/utility/math/Math.h"
#include "BaseEnemy.h"
#include "EnemyGiantSlime.h"
#include "GimmickHookPullBlock.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

// フック移動とターザン挙動をまとめています。

void PlayerStateHook::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Hook");
    if (player) {
        player->SetIsControlActive(false);
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
        player->SetSlimeAnimationDirection(targetPos_ - player->GetWorldPosition());
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

void PlayerStateHook::Update(Player* player, float deltaTime) {
    if (!player) return;

    if (phase_ == Phase::kShootHook) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
        player->SetSlimeAnimationDirection(targetPos_ - player->GetWorldPosition());
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
        player->SetMoveYaw(targetAngle);
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
        player->SetSlimeAnimationDirection(dir);
        wobbleTimer_ += deltaTime;

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
            const float initialRopeLength = std::max(0.001f, Math::Length(targetPos_ - hookTipPos_));
            float t = std::max(0.0f, std::min(1.0f, 1.0f - (len / initialRopeLength)));
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
        player->TriggerSlimeImpulse({ 3.0f, 1.0f, 3.0f }, 0.18f);

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

void PlayerStateSwingHook::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: SwingHook");
    if (!player) return;

    player->SetIsControlActive(false);
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
    player->SetSlimeAnimationDirection(anchorPos_ - player->GetWorldPosition());
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

void PlayerStateSwingHook::Update(Player* player, float deltaTime) {
    if (!player) return;

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
        player->SetMoveYaw(std::atan2(flatVel.x, flatVel.z));
    }

    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
    player->SetSlimeAnimationDirection(swingVelocity_);

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
        player->TriggerSlimeImpulse({ 3.0f, 1.0f, 3.0f }, 0.18f);
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

