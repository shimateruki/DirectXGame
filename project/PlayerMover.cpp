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

	// 1. 現在の速度を取得 (Characterによる重力計算済み)
	Vector3 velocity = player_->GetVelocity();

	// 2. 入力移動ベクトルの計算
	Vector3 inputMove = strategy_->CalculateVelocity(player_);

	// ★重要: X, Z (移動) は上書きし、Y (重力) は維持する
	velocity.x = inputMove.x;
	velocity.z = inputMove.z;

	// 3. 回転処理 (移動入力がある場合のみ)
	if (!player_->IsLockingOn()) {
		if (std::abs(inputMove.x) > 0.001f || std::abs(inputMove.z) > 0.001f) {
			float targetAngle = std::atan2(inputMove.x, inputMove.z);
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