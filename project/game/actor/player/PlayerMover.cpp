#include "PlayerMover.h"
#include "Player.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "IMoveStrategy.h"
#include "MoveStrategy3D.h"
#include "Object3d.h"
#include <algorithm>
#include <cmath>
#include <PlayerState.h>
#include "EventManager.h"

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
    
    // 基本スケールを保存 (基準は2.0)
    baseScale_ = { 2.0f, 2.0f, 2.0f };
    if (player_) player_->GetTransform()->scale = baseScale_;
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

    // --- 2. 現在の速度を取得 (Character クラスの重力計算等は別) ---
    Vector3 velocity = player_->GetVelocity();

    // --- 3. 入力に基づく移動ベクトルの計算 ---
    Vector3 inputMove = strategy_->CalculateVelocity(player_);

    // --- 4. 移動入力の有無を確認 ---
    bool hasMoveInput = (std::abs(inputMove.x) > 0.001f) || (std::abs(inputMove.z) > 0.001f);

    if (dashPanelTimer_ > 0.0f) {
        dashPanelTimer_ = (std::max)(0.0f, dashPanelTimer_ - deltaTime);
    }
    if (iceTimer_ > 0.0f) {
        iceTimer_ = (std::max)(0.0f, iceTimer_ - deltaTime);
    }

    const bool isDashPanelActive = dashPanelTimer_ > 0.0f;
    const bool isIceActive = iceTimer_ > 0.0f;
    const float moveSpeedMultiplier = isDashPanelActive ? dashPanelSpeedMultiplier_ : 1.0f;
    const float turnMultiplier = isDashPanelActive ? dashPanelTurnMultiplier_ : 1.0f;

    // --- 5. 速度の決定 (ダッシュ中 or 通常) ---
    if (isDashing_)
    {
        float tRatio = dashTimer_ / currentDashDuration_; // 1.0 -> 0.0
        float currentSpeed = currentDashSpeed_ * (tRatio * tRatio);

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
        Vector3 targetMove = inputMove * moveSpeedMultiplier;

        if (isIceActive) {
            float steerAlpha = 1.0f - std::expf(-4.0f * iceSteering_ * deltaTime);
            velocity.x = Math::Lerp(velocity.x, targetMove.x, steerAlpha);
            velocity.z = Math::Lerp(velocity.z, targetMove.z, steerAlpha);

            if (!hasMoveInput) {
                float frictionDecay = std::expf(-iceFriction_ * deltaTime);
                velocity.x *= frictionDecay;
                velocity.z *= frictionDecay;
            }
        }
        else {
            velocity.x = targetMove.x;
            velocity.z = targetMove.z;
        }
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

        const float iceTurnMultiplier = isIceActive ? (std::max)(0.25f, iceSteering_) : 1.0f;
        const float turnSpeed = 12.0f * turnMultiplier * iceTurnMultiplier;
        float alpha = 1.0f - std::expf(-turnSpeed * deltaTime);
        float newY = currentY + diff * alpha;

        player_->SetRotationY(NormalizeAngle(newY));
    }

    // --- 7. ジャンプ・溜め攻撃処理 ---
    bool isGrounded = player_->IsGrounded();
    if (isGrounded) {
        hasAirDashed_ = false; // 地面に着いたら空中ダッシュ権をリセット
    }

    if (inputManager_->IsActionPressed("Jump")) {
        // 地上、または空中かつダッシュ未実行ならチャージ可能
        if (isGrounded || (!isGrounded && !hasAirDashed_)) {
            isJumpCharging_ = true;
            jumpChargeTimer_ += deltaTime;
            if (jumpChargeTimer_ > maxChargeTime_) jumpChargeTimer_ = maxChargeTime_;

            if (!isDashing_) {
                // 移動速度を落とす（空中でも溜め中は少し減速させる）
                velocity.x *= 0.2f;
                velocity.z *= 0.2f;
            }
        }
    }
    else if (isJumpCharging_) {
        // 解放！
        float chargeRatio = jumpChargeTimer_ / maxChargeTime_;

        // 溜めが十分か判定
        if (jumpChargeTimer_ >= 0.4f) {
            // 空中なら使用済みフラグをチェック
            if (isGrounded || !hasAirDashed_) {
                float yaw = player_->GetRotation().y;
                
                // ダッシュ（慣性移動）の仕組みに乗せる
                currentDashSpeed_ = dashSpeed_ * (0.5f + chargeRatio * 1.5f);
                currentDashDuration_ = dashDuration_ * (0.3f + chargeRatio * 1.2f);
                
                dashDirection_ = { std::sin(yaw), 0.0f, std::cos(yaw) };
                isDashing_ = true;
                dashTimer_ = currentDashDuration_;

                // 空中なら使用済みフラグを立てる
                if (!isGrounded) {
                    hasAirDashed_ = true;
                    // 空中ダッシュ時は少しだけ上方向にベクトルを足して滞空時間を稼ぐ（お好みで）
                    velocity.y = 2.0f; 
                }

                // 突進状態へ遷移
                if (player_) {
                    player_->SetDashInvincible(true);
                    player_->ChangeState(std::make_unique<PlayerStateDash>());

                    // 子パーツの衝突属性を一時退避して無効化
                    childOriginalAttributes_.clear();
                    for (Object3d* child : player_->GetChildren()) {
                        if (!child) continue;
                        uint32_t orig = child->GetCollisionAttribute();
                        childOriginalAttributes_.emplace(child, orig);
                        child->SetCollisionAttribute(0);
                    }
                }

                // 突進エフェクト
                if (particleSystem_) {
                    Vector3 pos = player_->GetWorldPosition();
                    particleSystem_->SpawnParticles(
                        pos, 20, 2.0f, &dashDirection_, 40.0f,
                        { 0.4f, 0.8f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f },
                        0.1f, 0.3f, 0.5f, 0.05f
                    );
                }
            }
        }
        else if (isGrounded) {
            // 地上で溜め不足なら通常ジャンプ
            velocity.y = player_->GetJumpPower();

            // ジャンプイベントを発行
            PlayerJumpEvent jumpEvent;
            jumpEvent.player = player_;
            player_->IncrementJumpCount();
            EventManager::GetInstance()->Dispatch(jumpEvent);

            // ジャンプ時の土煙エフェクト
            if (particleSystem_) {
                Vector3 footPos = player_->GetWorldPosition();
                footPos.y -= 1.0f;
                particleSystem_->SpawnParticles(
                    footPos, 15, 1.0f, nullptr, 1.0f,
                    { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f },
                    0.2f, 0.5f, 1.0f, 0.1f
                );
            }

            // 水平方向の速度を現在の向きに合わせる
            if (hasMoveInput) {
                float len = std::sqrt(inputMove.x * inputMove.x + inputMove.z * inputMove.z);
                if (len > 0.001f) {
                    float s = player_->GetMoveSpeed() / len;
                    velocity.x = inputMove.x * s;
                    velocity.z = inputMove.z * s;
                }
            } else {
                float yaw = player_->GetRotation().y;
                velocity.x = std::sin(yaw) * player_->GetMoveSpeed();
                velocity.z = std::cos(yaw) * player_->GetMoveSpeed();
            }

            // 状態遷移
            if (player_) player_->ChangeState(std::make_unique<PlayerStateJump>());
        }

        // チャージリセット
        isJumpCharging_ = false;
        jumpChargeTimer_ = 0.0f;
    }
    else {
        // チャージ中でない場合、空中ではフラグ管理のみ（着地リセットは上記で行っている）
    }

    // --- 8. スライム特有のホッピング移動 ---
    if (player_->IsGrounded() && hasMoveInput && !isDashing_ && !isIceActive) {
        hopTimer_ += deltaTime;
        if (hopTimer_ > 0.4f) { // 0.4秒おきに跳ねる
            velocity.y = 5.0f;  // 小ジャンプ
            hopTimer_ = 0.0f;
            
            // 跳ねる瞬間の土煙（少し控えめ）
            if (particleSystem_) {
                Vector3 footPos = player_->GetWorldPosition();
                footPos.y -= 1.0f;
                particleSystem_->SpawnParticles(footPos, 3, 0.3f, nullptr, 0.2f,
                    { 1,1,1,1 }, { 1,1,1,0 }, 0.1f, 0.3f, 0.5f, 0.02f);
            }
        }
    } else if (!hasMoveInput) {
        hopTimer_ = 0.3f; // 次の移動開始時にすぐ跳ねるように調整
    }

    // --- 9. Squash & Stretch (伸縮アニメーション) ---
    slimeTimer_ += deltaTime;
    Vector3 targetScale = baseScale_;
    
    // チャージ中の潰れ
    if (isJumpCharging_) {
        float chargeRatio = jumpChargeTimer_ / maxChargeTime_;
        float squash = chargeRatio * 1.2f; // 最大で1.2（基準2.0から1.2引いて0.8になる）
        targetScale.y -= squash;
        targetScale.x += squash * 0.4f;
        targetScale.z += squash * 0.4f;
    }

    float vy = velocity.y;
    // 上昇・下降による伸縮
    if (std::abs(vy) > 0.1f) {
        float stretch = 0.0f;
        if (vy > 0.0f) {
            stretch = vy * 0.08f; // 上昇時は勢いよく伸ばす
        } else {
            stretch = std::abs(vy) * 0.02f; // 落下時も少しだけ伸ばして「落下感」を出す（潰さない）
        }
        targetScale.y += stretch;
        targetScale.x -= stretch * 0.5f;
        targetScale.z -= stretch * 0.5f;
        
        // 潰れ・伸びの限界値を設定 (最小1.2, 最大3.5くらい)
        targetScale.y = std::clamp(targetScale.y, 1.2f, 3.5f);
        targetScale.x = std::clamp(targetScale.x, 1.0f, 2.5f);
        targetScale.z = std::clamp(targetScale.z, 1.0f, 2.5f);
    } 
    // 接地中の「ぷるぷる」
    else if (player_->IsGrounded()) {
        // 着地時の潰れを再現
        float wobble = std::sin(slimeTimer_ * 15.0f) * 0.15f; // 基準2.0に合わせて少し強化
        if (hasMoveInput) wobble *= 1.2f; 
        
        targetScale.y += wobble;
        targetScale.x -= wobble * 0.5f;
        targetScale.z -= wobble * 0.5f;
    }

    // スケールをなめらかに適用 (Lerp)
    float scaleLerpSpeed = 15.0f;
    Vector3 currentScale = player_->GetTransform()->scale;
    player_->GetTransform()->scale.x += (targetScale.x - currentScale.x) * (1.0f - std::expf(-scaleLerpSpeed * deltaTime));
    player_->GetTransform()->scale.y += (targetScale.y - currentScale.y) * (1.0f - std::expf(-scaleLerpSpeed * deltaTime));
    player_->GetTransform()->scale.z += (targetScale.z - currentScale.z) * (1.0f - std::expf(-scaleLerpSpeed * deltaTime));

    // --- 10. 最終的な速度をプレイヤーへ適用 ---
    player_->SetVelocity(velocity);
}

void PlayerMover::SetStrategy(std::unique_ptr<IMoveStrategy> strategy)
{
    strategy_ = std::move(strategy);
}

void PlayerMover::ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier)
{
    dashPanelTimer_ = (std::max)(dashPanelTimer_, duration);
    dashPanelSpeedMultiplier_ = (std::max)(1.0f, speedMultiplier);
    dashPanelTurnMultiplier_ = std::clamp(turnMultiplier, 0.05f, 1.0f);

    if (!player_) return;

    Vector3 velocity = player_->GetVelocity();
    float horizontalSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    float targetSpeed = player_->GetMoveSpeed() * dashPanelSpeedMultiplier_;
    if (horizontalSpeed >= targetSpeed) return;

    Vector3 dir{};
    if (horizontalSpeed > 0.05f) {
        dir.x = velocity.x / horizontalSpeed;
        dir.z = velocity.z / horizontalSpeed;
    }
    else {
        float yaw = player_->GetRotation().y;
        dir.x = std::sin(yaw);
        dir.z = std::cos(yaw);
    }

    velocity.x = dir.x * targetSpeed;
    velocity.z = dir.z * targetSpeed;
    player_->SetVelocity(velocity);
}

void PlayerMover::ApplyIceSurface(float duration, float friction, float steering)
{
    iceTimer_ = (std::max)(iceTimer_, duration);
    iceFriction_ = std::clamp(friction, 0.02f, 8.0f);
    iceSteering_ = std::clamp(steering, 0.05f, 1.0f);
}
