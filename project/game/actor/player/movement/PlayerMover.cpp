#include "game/actor/player/movement/PlayerMover.h"
#include "game/actor/player/Player.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "IMoveStrategy.h"
#include "MoveStrategy3D.h"
#include "Object3d.h"
#include <algorithm>
#include <cmath>
#include <game/actor/player/state/PlayerState.h>
#include "EventManager.h"
#include "DebugConsole.h"
#include "GPUParticleManager.h"
#include "GroundEffectLocator.h"
#include "MeshEffectManager.h"
#include "engine/utility/math/AnimationInterpolation.h"

// 移動入力と接地状態を統合します。ジャンプ入力はPlayer本体で先に採取し、更新順による取りこぼしを避けます。

PlayerMover::PlayerMover() {}

PlayerMover::~PlayerMover() {
    // 念のためデストラクタでも子の衝突属性を復帰する
    if (player_) {
        for (auto& kv : childOriginalAttributes_) {
            Object3d* child = kv.first;
            uint32_t attr = kv.second;
            if (child)
                child->SetCollisionAttribute(attr);
        }
        childOriginalAttributes_.clear();
    }
}

void PlayerMover::StopDashOnImpact() {
    if (!isDashing_) {
        return;
    }

    isDashing_ = false;
    dashTimer_ = 0.0f;
    if (player_) {
        player_->SetDashInvincible(false);
    }

    for (auto& kv : childOriginalAttributes_) {
        Object3d* child = kv.first;
        if (child) {
            child->SetCollisionAttribute(kv.second);
        }
    }
    childOriginalAttributes_.clear();
}

void PlayerMover::ResetTransientState() {
    platformVelocity_ = {};
    platformAirVelocity_ = {};
    jumpRequest_.Reset();
    jumpStatus_ = "リセット";
    StopDashOnImpact();
    isJumpCharging_ = false;
    jumpChargeTimer_ = 0.0f;
    hasAirDashed_ = false;
    dashDirection_ = {};
    currentDashSpeed_ = dashSpeed_;
    currentDashDuration_ = dashDuration_;

    dashPanelTimer_ = 0.0f;
    dashPanelSpeedMultiplier_ = 1.0f;
    dashPanelTurnMultiplier_ = 1.0f;
    hasGroundedHistory_ = false;
    wasGroundedLastFrame_ = false;

    if (player_) {
        player_->SetSlimeJumpCharge(0.0f);
        player_->SetDashInvincible(false);
    }
}

void PlayerMover::Initialize(Player* player, InputManager* inputManager, ParticleSystem* particleSystem) {
    player_ = player;
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;

    // デフォルト戦略
    strategy_ = std::make_unique<MoveStrategy3D>();

    // 基本スケールを保存 (基準は2.0)
    baseScale_ = {2.0f, 2.0f, 2.0f};
    if (player_)
        player_->SetScale(baseScale_);
}

void PlayerMover::SampleJumpInput(float deltaTime, bool discard) {
    if (!player_ || !inputManager_)
        return;
    if (discard) {
        jumpRequest_.Reset();
        isJumpCharging_ = false;
        jumpChargeTimer_ = 0.0f;
        jumpStatus_ = "死亡・演出・コピー切替中：予約破棄";
        return;
    }
    if (deltaTime <= 0.0f || !std::isfinite(deltaTime))
        return;
    const bool hadRequest = jumpRequest_.BufferRemaining() > 0.0f;
    jumpRequest_.Advance(deltaTime, inputManager_->IsActionPressed("Jump"), inputManager_->IsActionTriggered("Jump"),
                         player_->IsGrounded() && player_->GetVelocity().y <= 0.0f, player_->IsPinkSlimeMorphed());
    jumpStatus_ =
        jumpRequest_.IsCharging()
            ? "溜め中"
            : (jumpRequest_.BufferRemaining() > 0.0f ? "入力予約：接地または操作復帰待ち" : "入力待ち／予約期限切れ");
#ifdef USE_IMGUI
    if (inputManager_->IsActionTriggered("Jump")) {
        DebugConsole::GetInstance()->AddLog(std::string("[Jump] 入力受付 / ") +
                                            (player_->IsGrounded() ? "接地" : "空中") +
                                            (player_->IsControlActive() ? " / 操作有効" : " / 操作制限中"));
    }
    if (hadRequest && jumpRequest_.BufferRemaining() <= 0.0f) {
        DebugConsole::GetInstance()->AddLog("[Jump] 予約期限切れ：接地または操作復帰が間に合いませんでした");
    }
#endif
}

// 水平移動とジャンプ要求を確定します。重力・衝突を含む Character の更新は呼び出し側で行います。
void PlayerMover::Update(float deltaTime) {
    if (!player_ || !inputManager_ || !strategy_)
        return;

    // 死亡時も落下速度は維持し、水平移動だけ止めます。
    if (player_->param_.has_value() && player_->param_->hp <= 0.0f) {
        Vector3 v = player_->GetVelocity();
        player_->SetVelocity({0.0f, v.y, 0.0f});
        return;
    }

    Vector3 velocity = player_->GetVelocity();

    Vector3 inputMove = strategy_->CalculateVelocity(player_);

    bool hasMoveInput = (std::abs(inputMove.x) > 0.001f) || (std::abs(inputMove.z) > 0.001f);

    if (dashPanelTimer_ > 0.0f) {
        dashPanelTimer_ = (std::max)(0.0f, dashPanelTimer_ - deltaTime);
    }
    if (landingDustCooldown_ > 0.0f) {
        landingDustCooldown_ = (std::max)(0.0f, landingDustCooldown_ - deltaTime);
    }

    const bool isDashPanelActive = dashPanelTimer_ > 0.0f;
    const float moveSpeedMultiplier = isDashPanelActive ? dashPanelSpeedMultiplier_ : 1.0f;
    const float turnMultiplier = isDashPanelActive ? dashPanelTurnMultiplier_ : 1.0f;

    // ダッシュ中は入力より既存の進行方向を優先し、終了時に通常移動へ戻します。
    if (isDashing_) {
        float tRatio = dashTimer_ / currentDashDuration_; // 1.0 -> 0.0
        float currentSpeed = currentDashSpeed_ * (tRatio * tRatio);

        velocity.x = dashDirection_.x * currentSpeed;
        velocity.z = dashDirection_.z * currentSpeed;

        dashTimer_ -= deltaTime;
        if (dashTimer_ <= 0.0f) {
            StopDashOnImpact();
        }
    } else {
        // 通常移動は入力速度を直接適用します。
        const Vector3 targetMove = inputMove * moveSpeedMultiplier;
        velocity.x = targetMove.x;
        velocity.z = targetMove.z;
    }

    // 表示方向は実際の水平速度を追従し、ダッシュパネルの応答倍率を反映します。
    if (std::abs(velocity.x) > 0.001f || std::abs(velocity.z) > 0.001f) {
        const float targetAngle = std::atan2(velocity.x, velocity.z);
        const float currentY = player_->GetMoveYaw();
        const float turnResponse = 6.5f * turnMultiplier;
        player_->SetMoveYaw(AnimationInterpolation::DampAngle(currentY, targetAngle, turnResponse, deltaTime));
    }

    // 接地した瞬間に着地演出を出し、空中ダッシュ権を戻します。
    bool isGrounded = player_->IsGrounded();
    if (hasGroundedHistory_ && isGrounded && !wasGroundedLastFrame_ && landingDustCooldown_ <= 0.0f) {
        EmitGroundDust("player_land_dust", 0.035f);
        landingDustCooldown_ = 0.12f;
    }
    if (isGrounded) {
        platformAirVelocity_ = {};
        hasAirDashed_ = false; // 地面に着いたら空中ダッシュ権をリセット
    }

    const bool usePinkSlimeJumpAbility = player_->IsPinkSlimeMorphed();
    isJumpCharging_ = jumpRequest_.IsCharging();
    jumpChargeTimer_ = jumpRequest_.Charge();
    player_->SetSlimeJumpCharge(jumpChargeTimer_);
    if (isJumpCharging_) {
        player_->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);
        if (!isDashing_) {
            velocity.x *= 0.2f;
            velocity.z *= 0.2f;
        }
    }

    float chargeRatio = 0.0f;
    if (jumpRequest_.Consume(chargeRatio)) {
        velocity.y = player_->GetJumpPower() * (usePinkSlimeJumpAbility ? (1.0f + chargeRatio * 0.65f) : 1.0f);
        player_->IncrementJumpCount();
        PlayerJumpEvent jumpEvent;
        jumpEvent.player = player_;
        EventManager::GetInstance()->Dispatch(jumpEvent);
        jumpStatus_ = "ジャンプ実行";
#ifdef USE_IMGUI
        DebugConsole::GetInstance()->AddLog("[Jump] ジャンプ実行");
#endif
        player_->SetSlimeJumpCharge(0.0f);

        if (usePinkSlimeJumpAbility && MeshEffectManager::GetInstance()) {
            Vector3 effectPos = GroundEffectLocator::ResolveGroundPosition(player_->GetWorldPosition());
            effectPos.y += 0.08f;
            MeshEffectManager::GetInstance()->SpawnEffectAt(
                "Resources/json/effect/effect_pink_slime_launch_kick_ring.json", effectPos, {0.0f, 0.0f, 0.0f},
                {1.25f, 0.9f, 1.25f});
        } else {
            EmitGroundDust("player_jump_dust", 0.035f);
        }
        // 水平方向の操作感は今回変更しません。
        if (hasMoveInput) {
            const float len = std::sqrt(inputMove.x * inputMove.x + inputMove.z * inputMove.z);
            if (len > 0.001f) {
                const float scale = player_->GetMoveSpeed() / len;
                velocity.x = inputMove.x * scale;
                velocity.z = inputMove.z * scale;
            }
        } else {
            const float yaw = player_->GetMoveYaw();
            velocity.x = std::sin(yaw) * player_->GetMoveSpeed();
            velocity.z = std::cos(yaw) * player_->GetMoveSpeed();
        }
        // 下降速度は渡さない。水平の勢いだけを残し、空中操作も維持する。
        platformAirVelocity_ = platformVelocity_ * 0.55f;
        platformAirVelocity_.y = 0.0f;
        const float carrySpeed = std::sqrt(platformAirVelocity_.x * platformAirVelocity_.x +
                                           platformAirVelocity_.z * platformAirVelocity_.z);
        if (carrySpeed > 24.0f)
            platformAirVelocity_ = platformAirVelocity_ * (24.0f / carrySpeed);
        player_->ChangeState(std::make_unique<PlayerStateJump>());
    }

    // 歩行演出で物理ジャンプを発生させない。
    // 旧ホッピングは接地を途切れさせ、同フレームのジャンプ速度も上書きしていました。
    // 見た目の伸縮はPlayerSlimeAnimatorが担当します。

    // --- 9. 最終的な速度をプレイヤーへ適用 ---
    if (!isDashing_) {
        velocity += platformAirVelocity_;
        platformAirVelocity_ = platformAirVelocity_ * std::exp(-0.8f * deltaTime);
    }
    platformVelocity_ = {};
    player_->SetVelocity(velocity);
    wasGroundedLastFrame_ = player_->IsGrounded();
    hasGroundedHistory_ = true;
}

void PlayerMover::EmitGroundDust(const char* presetName, float yOffset) const {
    if (!player_ || !presetName)
        return;

    GPUParticleManager* particleManager = GPUParticleManager::GetInstance();
    if (!particleManager || !particleManager->IsInitialized())
        return;

    Vector3 emitPos = GroundEffectLocator::ResolveGroundPosition(player_->GetWorldPosition());
    emitPos.y += yOffset;
    particleManager->Emit(presetName, emitPos);
}

void PlayerMover::SetStrategy(std::unique_ptr<IMoveStrategy> strategy) { strategy_ = std::move(strategy); }

void PlayerMover::ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier) {
    dashPanelTimer_ = (std::max)(dashPanelTimer_, duration);
    dashPanelSpeedMultiplier_ = (std::max)(1.0f, speedMultiplier);
    dashPanelTurnMultiplier_ = std::clamp(turnMultiplier, 0.05f, 1.0f);

    if (!player_)
        return;

    Vector3 velocity = player_->GetVelocity();
    float horizontalSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    float targetSpeed = player_->GetMoveSpeed() * dashPanelSpeedMultiplier_;
    if (horizontalSpeed >= targetSpeed)
        return;

    Vector3 dir{};
    if (horizontalSpeed > 0.05f) {
        dir.x = velocity.x / horizontalSpeed;
        dir.z = velocity.z / horizontalSpeed;
    } else {
        float yaw = player_->GetMoveYaw();
        dir.x = std::sin(yaw);
        dir.z = std::cos(yaw);
    }

    velocity.x = dir.x * targetSpeed;
    velocity.z = dir.z * targetSpeed;
    player_->SetVelocity(velocity);
}
