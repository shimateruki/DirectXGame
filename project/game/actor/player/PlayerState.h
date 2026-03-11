#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h" 

// 前方宣言
class Object3d;

// --------------------------------------------------------
// 待機状態 (Idle)
// --------------------------------------------------------
class PlayerStateIdle : public IAnimationState {
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

    // デフォルト回転が既に保存済みかどうか
    bool leftFootSaved_ = false;
    bool rightFootSaved_ = false;

    // --- 腕の管理 ---
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Vector3 leftArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
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
    bool headSaved_ = false;

    // アニメーションの共通時間管理
    float animTimer_ = 0.0f;
    float animDuration_ = 1.0f;

	// 足・腕のアニメーション段階管理
    int footStage_ = 0; 
    float targetAngleRad_ = 3.0f * 3.14159265f / 180.0f; // 3度をラジアンに変換

    // 頭の滑らか係数
    float headSmoothSpeed_ = 8.0f;
};

// --------------------------------------------------------
// 走り状態 (Run)
// --------------------------------------------------------
class PlayerStateRun : public IAnimationState {
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
};