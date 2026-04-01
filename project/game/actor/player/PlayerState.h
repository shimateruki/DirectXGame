#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h" 

// 前方宣言
class Object3d;

// --------------------------------------------------------
// 待機状態 (Idle)
// --------------------------------------------------------
class PlayerStateIdle : public IAnimationState
{
public:
	// 状態開始時に呼ばれる関数
    void Enter(Player* player) override;
	// 状態更新時に呼ばれる関数
    void Update(Player* player) override;
	// 状態終了時に呼ばれる関数
    void Exit(Player* player) override;
	// フレーム後処理（実時間 deltaTime が必要な分離メソッド）
    void ApplyPostUpdate(Player* player, float deltaTime);

private:
    // 足のID管理（見つからなければ nullptr のまま）
    Object3d* leftFootObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;

    // デフォルト回転を退避
    Vector3 leftFootDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootDefaultRot_{ 0.0f, 0.0f, 0.0f };

    // ブレンドの開始時の回転（遷移で使う）
    Vector3 leftFootStartRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootStartRot_{ 0.0f, 0.0f, 0.0f };

    // デフォルト回転が既に保存済みかどうか
    bool leftFootSaved_ = false;
    bool rightFootSaved_ = false;

    // --- 腕の管理 ---
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Vector3 leftArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftArmStartRot_{ 0.0f,0.0f,0.0f };
    Vector3 rightArmStartRot_{ 0.0f,0.0f,0.0f };
    bool leftArmSaved_ = false;
    bool rightArmSaved_ = false;

    // --- 剣の管理 ---
    Object3d* swordObj_ = nullptr;
    // ローカルのデフォルト座標（剣の Transform.translate）
    Vector3 swordDefaultLocalPos_{ 0.0f, 0.0f, 0.0f };
    // ワールドのデフォルト座標（GetWorldPosition）
    Vector3 swordDefaultWorldPos_{ 0.0f, 0.0f, 0.0f };
    bool swordSaved_ = false;

    // --- 頭の管理 ---
    Object3d* headObj_ = nullptr;
    Vector3 headDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 headStartRot_{ 0.0f, 0.0f, 0.0f };
    bool headSaved_ = false;

    // アニメーションの共通時間管理
    float animTimer_ = 0.0f;
    float animDuration_ = 1.0f;

    // 足・腕のアニメーション段階管理
    int footStage_ = 0;
    float targetAngleRad_ = 3.0f * 3.14159265f / 180.0f; // 3度をラジアンに変換

    // 頭の滑らか係数
    float headSmoothSpeed_ = 8.0f;

    // 遷移ブレンド用
    float blendTimer_ = 0.0f;
    float blendDuration_ = 0.15f; // 状態遷移のブレンド時間（秒）
};

// --------------------------------------------------------
// 走り状態 (Run)
// --------------------------------------------------------
class PlayerStateRun : public IAnimationState
{
public:
	// 状態開始時に呼ばれる関数
    void Enter(Player* player) override;
	// 状態更新時に呼ばれる関数
    void Update(Player* player) override;
	// 状態終了時に呼ばれる関数
    void Exit(Player* player) override;

    // フレーム後処理（実時間 deltaTime が必要な分離メソッド）
    void ApplyPostUpdate(Player* player, float deltaTime);

private:
    // 各パーツ
    Object3d* bodyObj_ = nullptr; // 体（通常は Player 本体）
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;

    // デフォルト（退避）値（位置・回転）
    Vector3 bodyDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 bodyDefaultRot_{ 0.0f, 0.0f, 0.0f };
    bool bodySaved_ = false;

    Vector3 headDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 headDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 headStartRot_{ 0.0f,0.0f,0.0f };
    bool headSaved_ = false;

    Vector3 rightArmDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmStartRot_{ 0.0f,0.0f,0.0f };
    bool rightArmSaved_ = false;

    Vector3 leftArmDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftArmStartRot_{ 0.0f,0.0f,0.0f };
    bool leftArmSaved_ = false;

    Vector3 rightFootDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootStartRot_{ 0.0f,0.0f,0.0f };
    bool rightFootSaved_ = false;

    Vector3 leftFootDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftFootDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftFootStartRot_{ 0.0f,0.0f,0.0f };
    bool leftFootSaved_ = false;

    // アニメタイマー（実時間）
    float animTimer_ = 0.0f;

    // 周期（秒） -- 1歩の往復にかける時間（調整可）
    float stepPeriod_ = 0.5f;

    // 振幅（ラジアン） - デフォルト: 右 ±2°, 左 ±3°
    float rightArmAmpRad_ = 2.0f * 3.14159265f / 180.0f; // ±2°
    float leftArmAmpRad_ = 3.0f * 3.14159265f / 180.0f; // ±3°
    float footAmpRad_ = 30.0f * 3.14159265f / 180.0f; // ±30°

    // 左腕へ毎フレーム適用する相対オフセット
    Vector3 leftArmOffset_{ 0.0f, 0.0f, 0.0f };

    // 遷移ブレンド用
    float blendTimer_ = 0.0f;
    float blendDuration_ = 0.12f; // Run 側のブレンド時間（秒）

    // --- Run -> Idle のための exit ブレンド制御 ---
    bool exitBlendActive_ = false;
    float exitBlendTimer_ = 0.0f;
    float exitBlendDuration_ = 0.15f; // 終了時のブレンド時間
    bool exitStartCaptured_ = false;
    Vector3 rightArmExitStartRot_{ 0.0f,0.0f,0.0f };
    Vector3 leftArmExitStartRot_{ 0.0f,0.0f,0.0f };
    Vector3 rightFootExitStartRot_{ 0.0f,0.0f,0.0f };
    Vector3 leftFootExitStartRot_{ 0.0f,0.0f,0.0f };
    Vector3 headExitStartRot_{ 0.0f,0.0f,0.0f };
    Vector3 bodyExitStartRot_{ 0.0f,0.0f,0.0f };

    // 追加: 頭回転の開始クォータニオン（Slerp 用）
    Quaternion headExitStartQuat_{ 0.0f, 0.0f, 0.0f, 1.0f };
};

// --------------------------------------------------------
// 攻撃1段目状態 (Attack1)
// --------------------------------------------------------
class PlayerStateAttack1 : public IAnimationState
{
public:
	//　アニメーションの開始の関数。
    void Enter(Player* player) override;
	// アニメーションの更新の関数
    void Update(Player* player) override;
	//　Exit 時に Idle の開始ポーズに戻すようにする関数
    void Exit(Player* player) override;

private:
    float animTimer_ = 0.0f;
    float animDuration_ = 0.55f; // アニメーションにかける時間

    // 各パーツ
    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;

    // 退避用（元のポーズ）
    Vector3 bodyDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 bodyDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 headDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 headDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 headStartRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftArmDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftFootDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftFootDefaultRot_{ 0.0f, 0.0f, 0.0f };

    bool initializedParts_ = false;

    // アニメ開始・終了ポーズ用ヘルパ
    void ApplyPose(float t);
};

// --------------------------------------------------------
// 攻撃2段目状態 (Attack2)
// --------------------------------------------------------
class PlayerStateAttack2 : public IAnimationState
{
public:
	// アニメーションの開始の関数。
    void Enter(Player* player) override;
	// アニメーションの更新の関数
    void Update(Player* player) override;
	// Exit 時に Idle の開始ポーズに戻すようにする関数
    void Exit(Player* player) override;

private:
    float animTimer_ = 0.0f;
    float animDuration_ = 0.4f;

    // 各パーツ
    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;

    // 退避用（元のポーズ）
    Vector3 bodyDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 bodyDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 headDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 headDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 headStartRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftArmDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightFootDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftFootDefaultPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 leftFootDefaultRot_{ 0.0f, 0.0f, 0.0f };

    bool initializedParts_ = false;

    // アニメ開始・終了ポーズ用ヘルパ
    void ApplyPose(float t);
};

// --------------------------------------------------------
// 攻撃3段目状態 (Attack3 - 突き攻撃)
// --------------------------------------------------------
class PlayerStateAttack3 : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    float animTimer_ = 0.0f;
    float animDuration_ = 1.0f;

    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;
    Vector3 bodyDefaultPos_{}; Vector3 bodyDefaultRot_{}; Vector3 bodyStartRot_{};
    Vector3 headDefaultPos_{}; Vector3 headDefaultRot_{}; Vector3 headStartRot_{};
    Vector3 rightArmDefaultPos_{}; Vector3 rightArmDefaultRot_{}; Vector3 rtArmStartRot_{};
    Vector3 leftArmDefaultPos_{}; Vector3 leftArmDefaultRot_{}; Vector3 ltArmStartRot_{};
    Vector3 rightFootDefaultPos_{}; Vector3 rightFootDefaultRot_{}; Vector3 rtFootStartRot_{};
    Vector3 leftFootDefaultPos_{}; Vector3 leftFootDefaultRot_{}; Vector3 ltFootStartRot_{};

    bool initializedParts_ = false;

    void ApplyPose(float t);
};


// --------------------------------------------------------
// 回避ダッシュ状態 (Dash / Slide Step)
// --------------------------------------------------------
class PlayerStateDash : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    // Mover側のダッシュ(0.2秒)より少し長くして、ブレーキの余韻(残心)を見せる
    float animTimer_ = 0.0f;
    float animDuration_ = 0.35f;

    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    // 足のID管理
    Object3d* leftFootObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;

    // デフォルト回転などを退避
    Vector3 bodyDefaultPos_{}; Vector3 bodyDefaultRot_{}; Vector3 bodyStartRot_{};
    Vector3 headDefaultPos_{}; Vector3 headDefaultRot_{}; Vector3 headStartRot_{};
    Vector3 rightArmDefaultPos_{}; Vector3 rightArmDefaultRot_{}; Vector3 rtArmStartRot_{};
    Vector3 leftArmDefaultPos_{}; Vector3 leftArmDefaultRot_{}; Vector3 ltArmStartRot_{};
    // 足のデフォルト回転などを退避
    Vector3 rightFootDefaultPos_{}; Vector3 rightFootDefaultRot_{}; Vector3 rtFootStartRot_{};
    Vector3 leftFootDefaultPos_{}; Vector3 leftFootDefaultRot_{}; Vector3 ltFootStartRot_{};

    bool initializedParts_ = false;
    void ApplyPose(float t);

    // --- 回避でのスピン制御 (X軸スピン) ---
    bool spinEnabled_ = true;                                     // 回避でスピンさせるかどうか
    float spinTotalRad_ = 2.0f * 3.14159265358979323846f;         // 1回転（ラジアン）
    float spinStartX_ = 0.0f;                                     // Enter 時の X 開始角度 (ラジアン)
    float spinTargetX_ = 0.0f;                                    // Enter 時の目標角度 (start + 2π)
};

// --------------------------------------------------------
// 死亡状態 (Dead - 絶望リーチ)
// --------------------------------------------------------
class PlayerStateDead : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    float animTimer_ = 0.0f;
    float animDuration_ = 3.5f; // ★ポストエフェクトの暗転時間に完全同期！

    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;
    Object3d* swordObj_ = nullptr;
    bool isSwordDropped_ = false;
    Vector3 swordDropPos_{};      // 手放した瞬間のワールド座標
    Vector3 swordDropRot_{};      // 手放した瞬間のワールド回転
    float dropStartTime_ = 0.0f;   // 手放したアニメーション時間
    Vector3 swordDropScale_{ 1.0f, 1.0f, 1.0f }; // ワールドスケールの維持
    Vector3 swordVelocity_{};                    // 吹っ飛ぶ速度
    float swordSpinSpeed_ = 15.0f;               // 回転の速さ
    bool isSwordStuck_ = false;                  // 地面に刺さったか
    Vector3 swordDefaultLocalPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 swordDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 bodyDefaultPos_{}; Vector3 bodyDefaultRot_{}; Vector3 bodyStartRot_{};
    Vector3 headDefaultPos_{}; Vector3 headDefaultRot_{}; Vector3 headStartRot_{};
    Vector3 rightArmDefaultPos_{}; Vector3 rightArmDefaultRot_{}; Vector3 rtArmStartRot_{};
    Vector3 leftArmDefaultPos_{}; Vector3 leftArmDefaultRot_{}; Vector3 ltArmStartRot_{};
    Vector3 rightFootDefaultPos_{}; Vector3 rightFootDefaultRot_{}; Vector3 rtFootStartRot_{};
    Vector3 leftFootDefaultPos_{}; Vector3 leftFootDefaultRot_{}; Vector3 ltFootStartRot_{};

    bool initializedParts_ = false;
    void ApplyPose(float t);
};

// --------------------------------------------------------
// 落下攻撃状態 (Plunge Attack)
// --------------------------------------------------------
class PlayerStatePlungeAttack : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    bool isPlunging_ = false;
    bool isLanded_ = false;
    float recoveryTimer_ = 0.0f;
    float recoveryDuration_ = 0.2f;

    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;
    Object3d* swordObj_ = nullptr;

    Vector3 bodyDefaultPos_{};
    Vector3 bodyDefaultRot_{};
    Vector3 headDefaultPos_{};
    Vector3 headDefaultRot_{};
    Vector3 rightArmDefaultPos_{};
    Vector3 rightArmDefaultRot_{};
    Vector3 leftArmDefaultPos_{};
    Vector3 leftArmDefaultRot_{};
    Vector3 rightFootDefaultPos_{};
    Vector3 rightFootDefaultRot_{};
    Vector3 leftFootDefaultPos_{};
    Vector3 leftFootDefaultRot_{};
    Vector3 swordDefaultRot_{};

    // ローカルのデフォルト座標（剣の Transform.translate）
    Vector3 swordDefaultLocalPos_{ 0.0f, 0.0f, 0.0f };
    // ワールドのデフォルト座標（GetWorldPosition）
    Vector3 swordDefaultWorldPos_{ 0.0f, 0.0f, 0.0f };
    // ワールド回転も保存しておく（Exitでワールド→ローカル変換に使う）
    Vector3 swordDefaultWorldRot_{ 0.0f, 0.0f, 0.0f };

    bool swordSaved_ = false;
    bool initializedParts_ = false;
    void ApplyPose(Player* player);
};

// --------------------------------------------------------
// ジャンプ状態 (Jump)
// --------------------------------------------------------
class PlayerStateJump : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    // 各パーツ
    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;
    Object3d* swordObj_ = nullptr;

    // 退避用（元のポーズ）
    Vector3 bodyDefaultRot_{ 0.0f,0.0f,0.0f };
    Vector3 headDefaultRot_{ 0.0f,0.0f,0.0f };
    Vector3 rightArmDefaultRot_{ 0.0f,0.0f,0.0f };
    Vector3 leftArmDefaultRot_{ 0.0f,0.0f,0.0f };
    Vector3 rightFootDefaultRot_{ 0.0f,0.0f,0.0f };
    Vector3 leftFootDefaultRot_{ 0.0f,0.0f,0.0f };
    Vector3 swordDefaultLocalPos_{ 0.0f,0.0f,0.0f };

    // 保存：head のローカル開始回転（復元用）
    Vector3 headStartRot_{ 0.0f,0.0f,0.0f };

    bool initializedParts_ = false;

    // ジャンプポーズ（ラジアン）
    Vector3 bodyJumpRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 headJumpRot_{ -15.0f * 3.14159265f / 180.0f, 0.0f, 0.0f };
    Vector3 rightArmJumpRot_{ 0.0f, 0.0f, 10.0f * 3.14159265f / 180.0f };
    Vector3 leftArmJumpRot_{ 0.0f, 0.0f, -10.0f * 3.14159265f / 180.0f };
    Vector3 rightFootJumpRot_{ 30.0f * 3.14159265f / 180.0f, 0.0f, 0.0f };
    Vector3 leftFootJumpRot_{ 30.0f * 3.14159265f / 180.0f, 0.0f, 0.0f };

    // 頂点検出とブレンド管理
    bool apexReached_ = false;
    float blendTimer_ = 0.0f;
    float blendDuration_ = 0.35f; // 頂点から着地までのブレンド近似時間
};