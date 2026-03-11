#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h" // Vector3, Math

// 前方宣言（Object3d をここで宣言しておく）
class Object3d;

// --------------------------------------------------------
// 待機状態 (Idle)
// --------------------------------------------------------
class PlayerStateIdle : public IAnimationState {
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

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

    // --- 追加: 腕 (Arm) 管理 ---
    Object3d* leftArmObj_ = nullptr;
    Object3d* rightArmObj_ = nullptr;
    Vector3 leftArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    Vector3 rightArmDefaultRot_{ 0.0f, 0.0f, 0.0f };
    bool leftArmSaved_ = false;
    bool rightArmSaved_ = false;

    // --- 追加: 剣 (Sword) 管理 (位置のみアニメーション) ---
    Object3d* swordObj_ = nullptr;
    // 保存: ローカルのデフォルト座標（剣の Transform.translate）
    Vector3 swordDefaultLocalPos_{ 0.0f, 0.0f, 0.0f };
    // 保存: ワールドのデフォルト座標（GetWorldPosition）
    Vector3 swordDefaultWorldPos_{ 0.0f, 0.0f, 0.0f };
    bool swordSaved_ = false;
    // 注意: 剣は足/腕と同じ補間係数 `t` を使う（swordDuration_ を独立させない）。

    // アニメーション制御
    float footTimer_ = 0.0f;
    float footDuration_ = 0.25f; // 足・腕（および剣）の往復にかける時間(秒)
    int footStage_ = 0; // 0=to target, 1=to default
    float targetAngleRad_ = 3.0f * 3.14159265f / 180.0f; // 3度 をラジアンに変換
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