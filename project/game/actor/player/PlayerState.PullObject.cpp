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

// フックでオブジェクトを引く状態をまとめています。

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

