#define NOMINMAX
#include "PlayerState.h"
#include "Player.h"
#include "DebugConsole.h"
#include "engine/utility/math/Math.h"
#include <cmath>
#include <memory>

// 被弾時のノックバックとスライムらしい変形演出をまとめています。


// ========================================================
// 被弾・ノックバック状態 (Damage)
// ========================================================
void PlayerStateDamage::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Damage (Knockback)");
    if (player) {
        player->ReleaseCarriedEnemy(true);
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
            Vector3 forward = player->GetForwardDirection();
            knockbackDir_ = { -forward.x, 0.0f, -forward.z };
        }

        float force = 18.0f;
        Vector3 v = knockbackDir_ * force;
        v.y = 9.0f;
        player->SetVelocity(v);
    }
}

void PlayerStateDamage::Update(Player* player, float deltaTime) {
    if (!player) return;

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
