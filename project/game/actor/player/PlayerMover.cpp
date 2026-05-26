#include "PlayerMover.h"
#include "Player.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "IMoveStrategy.h"
#include "MoveStrategy3D.h"
#include "Object3d.h"
#include <cmath>
#include <PlayerState.h>

PlayerMover::PlayerMover() {}

PlayerMover::~PlayerMover()
{
    // 念のためデストラクタでも子の衝突属性を復帰する
    if (player_)
    {
        for (auto& kv : childOriginalAttributes_)
        {
            Object3d* child = kv.first;
            uint32_t attr = kv.second;
            if (child) child->SetCollisionAttribute(attr);
        }
        childOriginalAttributes_.clear();
    }
}

void PlayerMover::Initialize(Player* player, InputManager* inputManager, ParticleSystem* particleSystem)
{
    player_ = player;
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;  

    // デフォルト戦略
    strategy_ = std::make_unique<MoveStrategy3D>();
}

void PlayerMover::Update(float deltaTime)
{
    if (!player_ || !inputManager_ || !strategy_) return;

    // --- 0. 死亡時は移動を完全停止 ---
    if (player_->param_.has_value() && player_->param_->hp <= 0.0f) {
        Vector3 v = player_->GetVelocity();
        player_->SetVelocity({ 0.0f, v.y, 0.0f });
        return;
    }

    // --- 1. ダッシュのクールダウン更新 ---
    if (dashCooldownTimer_ > 0.0f)
    {
        dashCooldownTimer_ -= deltaTime;
        if (dashCooldownTimer_ <= 0.0f)
        {
            dashCooldownTimer_ = 0.0f;
            dashAvailable_ = true;
        }
    }

    // --- 2. 現在の速度を取得 (Character クラスの重力計算等は別) ---
    Vector3 velocity = player_->GetVelocity();

    // --- 3. 入力に基づく移動ベクトルの計算 ---
    Vector3 inputMove = strategy_->CalculateVelocity(player_);

    // --- 4. ダッシュ(回避)開始判定 ---
    bool shiftTriggered = inputManager_->IsActionTriggered("Dash");
    bool hasMoveInput = (std::abs(inputMove.x) > 0.001f) || (std::abs(inputMove.z) > 0.001f);

    if (shiftTriggered && hasMoveInput && !isDashing_ && dashAvailable_)
    {
        float len = std::sqrt(inputMove.x * inputMove.x + inputMove.z * inputMove.z);
        if (len > 0.001f)
        {
            // ダッシュ方向とタイマー初期化
            dashDirection_.x = inputMove.x / len;
            dashDirection_.y = 0.0f;
            dashDirection_.z = inputMove.z / len;
            isDashing_ = true;
            dashTimer_ = dashDuration_;

            // クールタイム開始
            dashAvailable_ = false;
            dashCooldownTimer_ = dashCooldown_;

            if (player_)
            {
                // 回避専用の無敵フラグを ON
                player_->SetDashInvincible(true);
                // ダッシュアニメーションへ強制遷移
                player_->ChangeState(std::make_unique<PlayerStateDash>());
            }

            // 子パーツの衝突属性を一時退避して無効化
            childOriginalAttributes_.clear();
            for (Object3d* child : player_->GetChildren())
            {
                if (!child) continue;
                uint32_t orig = child->GetCollisionAttribute();
                childOriginalAttributes_.emplace(child, orig);
                child->SetCollisionAttribute(0);
            }

            // パーティクル（エフェクト）
            if (particleSystem_)
            {
                Vector3 pos = player_->GetWorldPosition();
                pos.y -= 1.0f;
                particleSystem_->SpawnParticles(
                    pos, 6, 1.0f, &dashDirection_, 20.0f,
                    { 1.0f, 0.8f, 0.2f, 1.0f }, { 1.0f, 0.8f, 0.2f, 0.0f },
                    0.1f, 0.4f, 0.6f, 0.05f
                );
            }
        }
    }

    // --- 5. 速度の決定 (ダッシュ中 or 通常) ---
    if (isDashing_)
    {
        float tRatio = dashTimer_ / dashDuration_; // 1.0 -> 0.0
        float currentSpeed = dashSpeed_ * (tRatio * tRatio);

        velocity.x = dashDirection_.x * currentSpeed;
        velocity.z = dashDirection_.z * currentSpeed;

        // ダッシュタイマー更新
        dashTimer_ -= deltaTime;
        if (dashTimer_ <= 0.0f)
        {
            isDashing_ = false;

            if (player_) {
                player_->SetDashInvincible(false); // 無敵解除
            }

            // 子パーツの衝突属性を復元
            for (auto& kv : childOriginalAttributes_)
            {
                Object3d* child = kv.first;
                uint32_t attr = kv.second;
                if (child) child->SetCollisionAttribute(attr);
            }
            childOriginalAttributes_.clear();
        }
    }
    else
    {
        // 通常時はキー入力による移動をそのまま適用
        velocity.x = inputMove.x;
        velocity.z = inputMove.z;
    }

    // --- 6. 回転処理 (移動方向へ滑らかに向ける) ---
    if (std::abs(velocity.x) > 0.001f || std::abs(velocity.z) > 0.001f)
    {
        float targetAngle = std::atan2(velocity.x, velocity.z);
        float currentY = player_->GetRotation().y;

        auto NormalizeAngle = [](float a) {
            while (a > 3.1415926535f) a -= 6.2831853071f;
            while (a < -3.1415926535f) a += 6.2831853071f;
            return a;
            };

        float diff = NormalizeAngle(targetAngle - currentY);

        const float turnSpeed = 12.0f;
        float alpha = 1.0f - std::expf(-turnSpeed * deltaTime);
        float newY = currentY + diff * alpha;

        player_->SetRotationY(NormalizeAngle(newY));
    }

    // --- 7. ジャンプ処理 ---
    if (player_->IsGrounded())
    {
        if (inputManager_->IsActionTriggered("Jump"))
        {
            velocity.y = player_->GetJumpPower();

            // ジャンプ時の土煙エフェクト
            if (particleSystem_)
            {
                Vector3 footPos = player_->GetWorldPosition();
                footPos.y -= 1.0f;
                particleSystem_->SpawnParticles(
                    footPos, 10, 0.5f, nullptr, 0.5f,
                    { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f },
                    0.2f, 0.5f, 1.0f, 0.1f
                );
            }

            // 水平方向の速度を「プレイヤーの向き」に合わせる
            // - 移動入力があればその方向を優先して速度を与える（正規化して moveSpeed を乗算）
            // - 無入力ならプレイヤーの向き（Y回転）に沿って moveSpeed を与える
            if (hasMoveInput)
            {
                float len = std::sqrt(inputMove.x * inputMove.x + inputMove.z * inputMove.z);
                if (len > 0.001f)
                {
                    float s = player_->GetMoveSpeed() / len;
                    velocity.x = inputMove.x * s;
                    velocity.z = inputMove.z * s;
                }
            }
            else
            {
                float yaw = player_->GetRotation().y;
                velocity.x = std::sin(yaw) * player_->GetMoveSpeed();
                velocity.z = std::cos(yaw) * player_->GetMoveSpeed();
            }

            // ジャンプ専用アニメーション状態へ遷移
            if (player_) {
                player_->ChangeState(std::make_unique<PlayerStateJump>());
            }
        }
    }

    // --- 8. 最終的な速度をプレイヤーへ適用 ---
    player_->SetVelocity(velocity);
}

void PlayerMover::SetStrategy(std::unique_ptr<IMoveStrategy> strategy)
{
    strategy_ = std::move(strategy);
}