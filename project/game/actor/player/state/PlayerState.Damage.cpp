#define NOMINMAX
#include "game/actor/player/state/PlayerState.h"
#include <numbers>
#include "game/actor/player/Player.h"
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

    // 被弾で運搬を解除し、変形演出中は入力による移動を止めます。
    if (player) {
        player->ReleaseCarriedEnemy(true);
        player->SetIsControlActive(false); // 操作不能にする

        // 初速をノックバック方向に与える
        timer_ = 0.0f;
        baseScale_ = player->GetScale();
        baseRotation_ = player->GetRotation();

        // 渡された方向は水平にそろえ、水平成分が無ければプレイヤーの背後へ飛ばします。
        knockbackDir_.y = 0.0f;
        float dirLength = engine::Math::Length(knockbackDir_);

        if (dirLength > 0.001f) {
            knockbackDir_ = knockbackDir_ * (1.0f / dirLength);
        } else {
            engine::Vector3 forward = player->GetForwardDirection();
            knockbackDir_ = {-forward.x, 0.0f, -forward.z};
        }

        // 水平の吹き飛ばしとは別に上向きの初速を与え、地面から跳ねる反応にします。
        float force = 18.0f;
        engine::Vector3 v = knockbackDir_ * force;
        v.y = 9.0f;
        player->SetVelocity(v);
    }
}

void PlayerStateDamage::Update(Player* player, float deltaTime) {
    // 操作対象が無ければ、タイマーも姿勢も更新しません。
    if (!player)
        return;

    timer_ += deltaTime;

    // 被弾開始からの経過を0から1へ収め、変形と回転の基準にする。
    float t = timer_ / duration_;
    if (t > 1.0f)
        t = 1.0f;

    engine::Vector3 targetScale = baseScale_;
    engine::Vector3 targetRotation = baseRotation_;
    const float roll = t * (2.0f * std::numbers::pi_v<float>);
    const float rollEaseOut = 1.0f - (1.0f - t) * (1.0f - t);

    // 衝撃を受けた直後はスライムを横へ潰す。
    if (t < 0.16f) {
        float p = t / 0.16f;
        float ease = 1.0f - (1.0f - p) * (1.0f - p);
        targetScale = {baseScale_.x * (1.0f + 0.62f * ease), baseScale_.y * (1.0f - 0.52f * ease),
                       baseScale_.z * (1.0f + 0.62f * ease)};
    } else if (t < 0.48f) {
        // 潰れた状態から縦に伸ばし、衝撃で跳ねた反応を見せる。
        float p = (t - 0.16f) / 0.32f;
        float ease = 1.0f - (1.0f - p) * (1.0f - p);
        targetScale = {engine::Math::Lerp(baseScale_.x * 1.62f, baseScale_.x * 0.72f, ease),
                       engine::Math::Lerp(baseScale_.y * 0.48f, baseScale_.y * 1.48f, ease),
                       engine::Math::Lerp(baseScale_.z * 1.62f, baseScale_.z * 0.72f, ease)};
    } else {
        // 残り時間で揺れを弱め、基準の大きさへ近づける。
        float p = (t - 0.48f) / 0.52f;
        if (p > 1.0f)
            p = 1.0f;
        float wobble = std::sin(p * std::numbers::pi_v<float> * 4.0f) * (1.0f - p);
        targetScale = {baseScale_.x * (1.0f + wobble * 0.28f), baseScale_.y * (1.0f - wobble * 0.22f),
                       baseScale_.z * (1.0f + wobble * 0.28f)};
    }

    // ノックバック方向に応じて転がす軸を変え、回転に合わせて横の伸縮も加える。
    targetRotation.x += knockbackDir_.z * roll;
    targetRotation.z -= knockbackDir_.x * roll;
    targetRotation.y += std::sin(t * std::numbers::pi_v<float>) * 0.35f * (knockbackDir_.x + knockbackDir_.z);
    targetScale.x += std::sin(rollEaseOut * std::numbers::pi_v<float> * 2.0f) * baseScale_.x * 0.08f;
    targetScale.z += std::cos(rollEaseOut * std::numbers::pi_v<float> * 2.0f) * baseScale_.z * 0.06f;

    // 変形と回転を一緒に反映してから、時間を満たしたときだけ通常操作へ戻します。
    player->SetScale(targetScale);
    player->SetRotation(targetRotation);

    // 時間経過で操作可能に戻し、Idleへ
    if (timer_ >= duration_) {
        player->SetIsControlActive(true);
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
}

void PlayerStateDamage::Exit(Player* player) {
    // ノックバックの変形を次状態へ持ち越さず、入場前の姿勢へ戻す。
    if (player) {
        player->SetIsControlActive(true);
        player->SetScale(baseScale_);
        player->SetRotation(baseRotation_);
    }
}
