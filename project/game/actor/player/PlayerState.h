#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h"
class Object3d; // 前方宣言
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
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    float timer_ = 0.0f;
    const float duration_ = 0.58f;
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

// --------------------------------------------------------
// フック移動状態 (Hook)
// --------------------------------------------------------
class PlayerStateHook : public IAnimationState
{
public:
    PlayerStateHook(const Vector3& targetPos) : targetPos_(targetPos) {}
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
private:
    Vector3 targetPos_;
    float speed_ = 60.0f; // フックの移動速度
    float oldGravity_ = 0.0f;
    float wobbleTimer_ = 0.0f;
    float oldFovY_ = 0.45f;   // Enter時に元のFOVを退避
    float spawnTimer_ = 0.0f; // パーティクル生成タイマー
    enum class Phase {
        kShootHook,  // 腕（フック）を目標に向けて飛ばしている状態
        kPullPlayer  // フックが刺さり、プレイヤー本体が引っ張られている状態
    };
    Phase phase_ = Phase::kShootHook;
    Vector3 hookTipPos_;            // フック先端の現在位置
    float hookShootSpeed_ = 250.0f; // フックが飛んでいく速度（超高速！）
};


// --------------------------------------------------------
// 敵引き寄せ状態 (Pull Enemy)
// --------------------------------------------------------
class PlayerStatePullEnemy : public IAnimationState
{
public:
    PlayerStatePullEnemy(Object3d* targetEnemy, const Vector3& targetPos)
        : targetEnemy_(targetEnemy), targetPos_(targetPos) {
    }
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
private:
    Object3d* targetEnemy_ = nullptr;
    Vector3 targetPos_;
    Vector3 hookTipPos_;

    enum class Phase {
        kShootHook,  // 腕が敵に向かって飛んでいる
        kPullEnemy   // 敵が自分に向かって引っ張られている
    };
    Phase phase_ = Phase::kShootHook;
    
    Vector3 enemyStartPos_; // 引っ張り開始時の敵の座標
    float pullTimer_ = 0.0f; // 引っ張りにかかる時間のタイマー
};

// --------------------------------------------------------
// 敵持ち運び状態 (Carry)
// --------------------------------------------------------
class PlayerStateCarry : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;
private:
    float struggleTimer_ = 0.0f;
};
