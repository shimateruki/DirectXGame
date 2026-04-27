#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

// --------------------------------------------------------
// 待機状態 (Idle)
// --------------------------------------------------------
class PlayerStateIdle : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
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
};

// --------------------------------------------------------
// 回避ダッシュ状態 (Dash)
// --------------------------------------------------------
class PlayerStateDash : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
private:
    float timer_ = 0.0f; // 状態を終了させるための簡易タイマー
};

// --------------------------------------------------------
// 死亡状態 (Dead)
// --------------------------------------------------------
class PlayerStateDead : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
};

// --------------------------------------------------------
// 被弾・ノックバック状態 (Damage)
// --------------------------------------------------------
class PlayerStateDamage : public IAnimationState
{
public:
    PlayerStateDamage(const Vector3& knockbackDir) : knockbackDir_(knockbackDir) {}
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
private:
    Vector3 knockbackDir_;
    float timer_ = 0.0f;
    const float duration_ = 0.5f; // ノックバック時間
};

// --------------------------------------------------------
// 落下演出状態 (FallingOut)
// --------------------------------------------------------
class PlayerStateFallingOut : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
private:
    enum class Phase {
        Waiting,  // 落下を見せる待機時間
        IrisOut,  // 画面を閉じる
        IrisIn    // 画面を開く
    };
    Phase phase_ = Phase::Waiting;
    float waitTimer_ = 0.0f;
};