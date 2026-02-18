#pragma once
#include "Math.h"
#include <memory>

// 前方宣言
class Player;
class InputManager;
class ParticleSystem; // ジャンプエフェクト用
class IMoveStrategy;  // 戦略インターフェース

/// <summary>
/// プレイヤーの移動制御コンポーネント
/// </summary>
class PlayerMover {
public:
	PlayerMover();
	~PlayerMover();

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(Player* player, InputManager* inputManager, ParticleSystem* particleSystem);

	/// <summary>
	/// 毎フレームの更新（移動・回転・ジャンプ）
	/// </summary>
	void Update(float deltaTime);

	/// <summary>
	/// 移動戦略（操作方法）を切り替える
	/// </summary>
	void SetStrategy(std::unique_ptr<IMoveStrategy> strategy);

private:
	// 参照用ポインタ
	Player* player_ = nullptr;
	InputManager* inputManager_ = nullptr;
	ParticleSystem* particleSystem_ = nullptr;

	// 移動戦略 (2D/3Dなど)
	std::unique_ptr<IMoveStrategy> strategy_ = nullptr;

	// パラメータ
	float jumpPower_ = 2.0f; // ジャンプ力
};