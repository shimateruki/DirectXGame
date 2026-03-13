#pragma once
#include "engine/utility/math/Math.h"
#include <memory>
#include <unordered_map>

// 前方宣言
class Player;
class InputManager;
class ParticleSystem; // ジャンプエフェクト用
class IMoveStrategy;  // 戦略インターフェース
class Object3d;       // 子オブジェクト用

/// <summary>
/// プレイヤーの移動制御コンポーネント
/// </summary>
class PlayerMover
{
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

	// --- ダッシュ回避関連 ---
	bool isDashing_ = false;
	float dashSpeed_ = 25.0f;       // ダッシュ速度
	float dashDuration_ = 0.20f;    // ダッシュ継続時間（秒）
	float dashTimer_ = 0.0f;        // 残りダッシュ時間
	float dashCooldown_ = 3.5f;     // ダッシュクールダウン（秒）
	float dashCooldownTimer_ = 0.0f;// クールダウン残り時間
	bool dashAvailable_ = true;     // ダッシュが使えるかどうか
	Vector3 dashDirection_{};       // 値初期化（{0,0,0}）

	// 子オブジェクトの元の衝突属性を保存して復元するためのマップ
	std::unordered_map<Object3d*, uint32_t> childOriginalAttributes_;
};