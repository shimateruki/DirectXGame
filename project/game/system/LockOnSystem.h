#pragma once
#include <vector>
#include <memory>

// 前方宣言
class Object3d;
class Camera;
class Player;
class InputManager;

/// <summary>
/// ロックオン機能（対象検索・カメラ制御・プレイヤー制御）を管理するクラス
/// </summary>
class LockOnSystem {
public:
	LockOnSystem();
	~LockOnSystem();

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(InputManager* inputManager);

	/// <summary>
	/// 毎フレームの更新処理
	/// </summary>
	/// <param name="objects">シーン内の全オブジェクトリスト</param>
	/// <param name="camera">メインカメラ</param>
	/// <param name="player">操作中のプレイヤー</param>
	void Update(const std::vector<std::unique_ptr<Object3d>>& objects, Camera* camera, Player* player);

	/// <summary>
	/// 現在ロックオン中かどうか
	/// </summary>
	bool IsLockingOn() const { return isLockingOn_; }

	/// <summary>
	/// 現在のターゲットを取得（弾の発射などで使用）
	/// </summary>
	Object3d* GetTarget() const { return lockOnTarget_; }
	/// <summary>
	/// ロックオンを強制するかどうかを設定する
	/// </summary>
	/// <param name="target">強制するターゲット（nullなら強制解除）</param>
	/// <param name="isForced">強制中フラグ</param>
	void SetForceLockOn(Object3d* target, bool isForced);

	/// <summary>
	/// ロックオン機能の有効・無効を設定する
	/// </summary>
	void SetEnabled(bool enabled) { isEnabled_ = enabled; }

private:
	/// <summary>
	/// 最も適したターゲットを検索する内部関数
	/// </summary>
	Object3d* FindBestTarget(const std::vector<std::unique_ptr<Object3d>>& objects, Camera* camera, Player* player);
	bool IsTargetOnScreen(Object3d* target, Camera* camera) const;

private:
	InputManager* inputManager_ = nullptr;
	Object3d* lockOnTarget_ = nullptr; // ロックオン対象
	bool isLockingOn_ = false;		   // ロックオン中フラグ

	// 定数パラメータ
	const float kMaxLockOnDistance_ = 80.0f; // ロックオン開始・解除の最大距離
	const float kMinLockOnDot_ = 0.0f;
	float lostSightTimer_ = 0.0f;
	bool isForced_ = false; // 強制ロックオン中か
	bool isEnabled_ = true; // システム自体が有効か
};
