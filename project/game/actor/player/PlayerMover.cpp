#include "PlayerMover.h"
#include "Player.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "IMoveStrategy.h"
#include "MoveStrategy3D.h"
#include <cmath> // std::abs, std::atan2


PlayerMover::PlayerMover() {}
PlayerMover::~PlayerMover() {}

void PlayerMover::Initialize(Player* player, InputManager* inputManager, ParticleSystem* particleSystem) {
	player_ = player;
	inputManager_ = inputManager;
	particleSystem_ = particleSystem;

	// デフォルト戦略
	strategy_ = std::make_unique<MoveStrategy3D>();
}

void PlayerMover::Update(float deltaTime) {
	if (!player_ || !inputManager_ || !strategy_) return;

	// --- クールダウンタイマー更新（先に処理） ---
	if (dashCooldownTimer_ > 0.0f) {
		dashCooldownTimer_ -= deltaTime;
		if (dashCooldownTimer_ <= 0.0f) {
			dashCooldownTimer_ = 0.0f;
			dashAvailable_ = true; // クールダウン終了で再使用可能
		}
	}

	// 1. 現在の速度を取得 (Characterによる重力計算済み)
	Vector3 velocity = player_->GetVelocity();

	// 2. 入力移動ベクトルの計算
	Vector3 inputMove = strategy_->CalculateVelocity(player_);

	// ダッシュ開始判定: Shift 押下（トリガー）かつ移動入力ありかつダッシュ可能であること
	bool shiftTriggered = inputManager_->IsKeyTriggered(DIK_LSHIFT) || inputManager_->IsKeyTriggered(DIK_RSHIFT);
	bool hasMoveInput = (std::abs(inputMove.x) > 0.001f) || (std::abs(inputMove.z) > 0.001f);

	if (shiftTriggered && hasMoveInput && !isDashing_ && dashAvailable_) {
		// ダッシュ開始
		float len = std::sqrt(inputMove.x * inputMove.x + inputMove.z * inputMove.z);
		if (len > 0.001f) {
			dashDirection_.x = inputMove.x / len;
			dashDirection_.y = 0.0f;
			dashDirection_.z = inputMove.z / len;
			isDashing_ = true;
			dashTimer_ = dashDuration_;

			// クールタイムは開始と同時にカウントダウンを始める（これで連続回避を防止）
			dashAvailable_ = false;
			dashCooldownTimer_ = dashCooldown_;

			// 無敵付与（ダッシュ中のみ）
			if (player_) {
				player_->SetInvincible(true);
			}

			// エフェクト（任意）
			if (particleSystem_) {
				Vector3 pos = player_->GetWorldPosition();
				pos.y -= 1.0f;
				particleSystem_->SpawnParticles(
					pos, 6, 1.0f, &dashDirection_, 20.0f,
					{ 1,0.8f,0.2f,1 }, { 1,0.8f,0.2f,0 }, 0.1f, 0.4f, 0.6f, 0.05f
				);
			}
		}
	}

	// ★重要: X, Z (移動) は上書きし、Y (重力) は維持する
	if (isDashing_) {
		// ダッシュ中は指定速度で override
		velocity.x = dashDirection_.x * dashSpeed_;
		velocity.z = dashDirection_.z * dashSpeed_;

		// ダッシュタイマー更新
		dashTimer_ -= deltaTime;
		if (dashTimer_ <= 0.0f) {
			isDashing_ = false;
			// 無敵解除（ダッシュ終了時）
			if (player_) {
				player_->SetInvincible(false);
			}
			// クールダウンは既に開始済みなのでここでは何もしない
		}
	} else {
		// 通常時は入力移動を適用
		velocity.x = inputMove.x;
		velocity.z = inputMove.z;
	}

	// 3. 回転処理 (移動入力がある場合のみ)
	if (!player_->IsLockingOn()) {
		if (std::abs(velocity.x) > 0.001f || std::abs(velocity.z) > 0.001f) {
			float targetAngle = std::atan2(velocity.x, velocity.z);
			player_->SetRotationY(targetAngle);
		}
	}

	// 4. ジャンプ処理
	if (player_->IsGrounded()) {
		if (inputManager_->IsKeyTriggered(DIK_SPACE)) {
			// ★修正: Player(Editor)の設定値を使用
			velocity.y = player_->GetJumpPower();

			// エフェクト
			if (particleSystem_) {
				Vector3 footPos = player_->GetWorldPosition();
				footPos.y -= 1.0f;
				particleSystem_->SpawnParticles(
					footPos, 10, 0.5f, nullptr, 0.5f,
					{ 1,1,1,1 }, { 1,1,1,0 }, 0.2f, 0.5f, 1.0f, 0.1f
				);
			}
		}
	}

	// 5. 結果を適用
	player_->SetVelocity(velocity);
}

void PlayerMover::SetStrategy(std::unique_ptr<IMoveStrategy> strategy) {
	strategy_ = std::move(strategy);
}