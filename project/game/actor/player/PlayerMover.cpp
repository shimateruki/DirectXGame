#include "PlayerMover.h"
#include "Player.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "IMoveStrategy.h"
#include "MoveStrategy3D.h"
#include "Object3d.h"
#include <cmath>

PlayerMover::PlayerMover() {}
PlayerMover::~PlayerMover()
{
	// 念のためデストラクタでも子の衝突属性を復元する
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

	// --- クールダウンタイマー更新（先に処理） ---
	if (dashCooldownTimer_ > 0.0f)
	{
		dashCooldownTimer_ -= deltaTime;
		if (dashCooldownTimer_ <= 0.0f)
		{
			dashCooldownTimer_ = 0.0f;
			dashAvailable_ = true;
		}
	}

	// 1. 現在の速度を取得 (Characterによる重力計算済み)
	Vector3 velocity = player_->GetVelocity();

	// 2. 入力移動ベクトルの計算
	Vector3 inputMove = strategy_->CalculateVelocity(player_);

	// ダッシュ開始判定: Shift 押下（トリガー）かつ移動入力ありかつダッシュ可能であること
	bool shiftTriggered = inputManager_->IsKeyTriggered(DIK_LSHIFT) || inputManager_->IsKeyTriggered(DIK_RSHIFT);
	bool hasMoveInput = (std::abs(inputMove.x) > 0.001f) || (std::abs(inputMove.z) > 0.001f);

	if (shiftTriggered && hasMoveInput && !isDashing_ && dashAvailable_)
	{
		// ダッシュ開始
		float len = std::sqrt(inputMove.x * inputMove.x + inputMove.z * inputMove.z);
		if (len > 0.001f)
		{
			dashDirection_.x = inputMove.x / len;
			dashDirection_.y = 0.0f;
			dashDirection_.z = inputMove.z / len;
			isDashing_ = true;
			dashTimer_ = dashDuration_;

			// クールタイムは開始と同時にカウントダウンを始める（これで連続回避を防止）
			dashAvailable_ = false;
			dashCooldownTimer_ = dashCooldown_;

			// 無敵付与（ダッシュ中のみ）
			if (player_)
			{
				player_->SetInvincible(true);
			}

			// 子パーツの当たりを一時的に無効化する（元の属性は保存しておく）
			childOriginalAttributes_.clear();
			for (Object3d* child : player_->GetChildren())
			{
				if (!child) continue;
				uint32_t orig = child->GetCollisionAttribute();
				childOriginalAttributes_.emplace(child, orig);
				child->SetCollisionAttribute(0); // 完全に無効化（必要ならビットマスクで調整）
			}

			// エフェクト（任意）
			if (particleSystem_)
			{
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
	if (isDashing_)
	{
		// ダッシュ中は指定速度で override
		velocity.x = dashDirection_.x * dashSpeed_;
		velocity.z = dashDirection_.z * dashSpeed_;

		// ダッシュタイマー更新
		dashTimer_ -= deltaTime;
		if (dashTimer_ <= 0.0f)
		{
			isDashing_ = false;
			// 無敵解除（ダッシュ終了時）
			if (player_)
			{
				player_->SetInvincible(false);
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
		// 通常時は入力移動を適用
		velocity.x = inputMove.x;
		velocity.z = inputMove.z;
	}

	// 3. 回転処理 (移動入力がある場合のみ) - 滑らかに補間する
	if (!player_->IsLockingOn())
	{
		if (std::abs(velocity.x) > 0.001f || std::abs(velocity.z) > 0.001f)
		{
			float targetAngle = std::atan2(velocity.x, velocity.z);

			// 現在のY角度
			float currentY = player_->GetRotation().y;

			// 差分を [-PI,PI] に正規化
			auto NormalizeAngle = [](float a)
				{
					while (a > 3.14159265358979323846f) a -= 2.0f * 3.14159265358979323846f;
					while (a < -3.14159265358979323846f) a += 2.0f * 3.14159265358979323846f;
					return a;
				};
			float diff = NormalizeAngle(targetAngle - currentY);

			// 滑らかさ係数（大きいほど速く追従） - 必要なら値を調整
			const float turnSpeed = 12.0f; // [rad/s] 実用的な値
			// 指数的減衰で補間（フレームレートに依存しづらい）
			float alpha = 1.0f - std::expf(-turnSpeed * deltaTime);

			float newY = currentY + diff * alpha;
			newY = NormalizeAngle(newY);

			player_->SetRotationY(newY);
		}
	}

	// 4. ジャンプ処理
	if (player_->IsGrounded())
	{
		if (inputManager_->IsKeyTriggered(DIK_SPACE))
		{
			// ★修正: Player(Editor)の設定値を使用
			velocity.y = player_->GetJumpPower();

			// エフェクト
			if (particleSystem_)
			{
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

void PlayerMover::SetStrategy(std::unique_ptr<IMoveStrategy> strategy)
{
	strategy_ = std::move(strategy);
}