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
    void Enter(Player* player) override;

    void Update(Player* player) override;

    void Exit(Player* player) override;

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
    void Enter(Player* player) override;
    void Update(Player* player) override;
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
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    float animTimer_ = 0.0f;
    float animDuration_ = 0.3f; // アニメーションにかける時間

    // 各パーツ
    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;

    // 退避用（元のポーズ）
    Vector3 bodyDefaultPos_, bodyDefaultRot_;
    Vector3 headDefaultPos_, headDefaultRot_;
    Vector3 headStartRot_;
    Vector3 rightArmDefaultPos_, rightArmDefaultRot_;
    Vector3 leftArmDefaultPos_, leftArmDefaultRot_;
    Vector3 rightFootDefaultPos_, rightFootDefaultRot_;
    Vector3 leftFootDefaultPos_, leftFootDefaultRot_;

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
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    float animTimer_ = 0.0f;
    float animDuration_ = 0.5f;

    // 各パーツ
    Object3d* bodyObj_ = nullptr;
    Object3d* headObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightFootObj_ = nullptr;
    Object3d* leftFootObj_ = nullptr;

    // 退避用（元のポーズ）
    Vector3 bodyDefaultPos_, bodyDefaultRot_;
    Vector3 headDefaultPos_, headDefaultRot_;
    Vector3 headStartRot_;
    Vector3 rightArmDefaultPos_, rightArmDefaultRot_;
    Vector3 leftArmDefaultPos_, leftArmDefaultRot_;
    Vector3 rightFootDefaultPos_, rightFootDefaultRot_;
    Vector3 leftFootDefaultPos_, leftFootDefaultRot_;

    bool initializedParts_ = false;

    // アニメ開始・終了ポーズ用ヘルパ
    void ApplyPose(float t);
};