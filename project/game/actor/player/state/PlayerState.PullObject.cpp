#define NOMINMAX
#include "game/actor/player/state/PlayerState.h"
#include "game/actor/player/Player.h"
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
    // フックの発射中は自身の移動を止め、マーカーをプレイヤー位置から伸ばす。
    DebugConsole::GetInstance()->AddLog("Enter: PullObject");
    if (!player)
        return;

    player->SetIsControlActive(false);
    player->SetVelocity({0.0f, 0.0f, 0.0f});
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
    player->SetSlimeAnimationDirection(targetPos_ - player->GetWorldPosition());
    hookTipPos_ = player->GetWorldPosition();
    timer_ = 0.0f;
    pullStarted_ = false;

    engine::Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(true);
        marker->SetColor({0.35f, 0.9f, 1.0f, 1.0f});
        marker->SetTranslate(hookTipPos_);
    }
}

void PlayerStatePullObject::Update(Player* player, float deltaTime) {
    if (!player || !targetObject_) {
        if (player)
            player->ChangeState(std::make_unique<PlayerStateIdle>());
        return;
    }

    timer_ += deltaTime;

    engine::Vector3 targetPos = targetObject_->GetWorldPosition();
    engine::Vector3 toTarget = targetPos - hookTipPos_;
    float dist = engine::Math::Length(toTarget);

    if (!pullStarted_) {
        // フック先端が対象に十分近付くまでは、対象の引き寄せを開始しない。
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
        player->SetSlimeAnimationDirection(targetPos - player->GetWorldPosition());
        if (dist < 3.0f) {
            hookTipPos_ = targetPos;
            // 専用ブロックだけが位置移動を持ち、通常オブジェクトには演出のみ行う。
            if (auto* block = dynamic_cast<GimmickHookPullBlock*>(targetObject_)) {
                block->StartHookPull(player->GetWorldPosition());
            }
            pullStarted_ = true;
            timer_ = 0.0f;
        } else {
            engine::Vector3 dir = engine::Math::Normalize(toTarget);
            hookTipPos_ = hookTipPos_ + dir * (180.0f * deltaTime);
        }
        UpdateRopeMarker(player, hookTipPos_, 0.22f);
        return;
    }

    UpdateRopeMarker(player, targetObject_->GetWorldPosition(), 0.18f);
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::PullEnemy);
    player->SetSlimePullDirection(targetObject_->GetWorldPosition() - player->GetWorldPosition());
    player->SetSlimePullProgress(std::clamp(timer_ / 0.35f, 0.0f, 1.0f));

    if (timer_ > 0.35f) {
        // 引き寄せ演出の終了時に Idle へ戻し、Exit で入力とマーカーを復元する。
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStatePullObject::Exit(Player* player) {
    // 成否にかかわらず、入力とフックマーカーの可視・回転モードを戻す。
    if (!player)
        return;

    player->SetIsControlActive(true);
    engine::Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(false);
        marker->SetScale({1.0f, 1.0f, 1.0f});
        marker->SetEulerAngles({0.0f, 0.0f, 0.0f});
        marker->SetQuaternionMaster(true);
        marker->UpdateLocalMatrix();
        marker->UpdateWorldMatrix();
    }
}

void PlayerStatePullObject::UpdateRopeMarker(Player* player, const engine::Vector3& endPos, float thickness) {
    // 両端の中点・方向・距離から細長い表示モデルの Transform を作る。
    if (!player)
        return;

    engine::Object3d* marker = player->GetHookMarker();
    if (!marker)
        return;

    engine::Vector3 startPos = player->GetWorldPosition();
    engine::Vector3 diff = endPos - startPos;
    float len = engine::Math::Length(diff);
    if (len < 0.01f)
        len = 0.01f;

    marker->SetIsVisible(true);
    marker->SetColor({0.35f, 0.9f, 1.0f, 1.0f});
    marker->SetTranslate(startPos + diff * 0.5f);
    marker->SetEulerAngles({std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z)),
                                      std::atan2(diff.x, diff.z), 0.0f});
    marker->SetQuaternionMaster(false);
    marker->SetScale({thickness, thickness, len});
    marker->UpdateLocalMatrix();
    marker->UpdateWorldMatrix();
}
