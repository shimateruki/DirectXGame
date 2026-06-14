#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

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
    float timer_ = 0.0f;
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

private:
    float timer_ = 0.0f;
    bool sceneChangeRequested_ = false;
    bool lifePresentationStarted_ = false;
    Vector2 irisCenter_ = { 0.5f, 0.5f };
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
        LifeLost,
        Waiting,
        IrisOut,
        IrisIn
    };

    Phase phase_ = Phase::Waiting;
    float waitTimer_ = 0.0f;
    Vector2 irisCenter_ = { 0.5f, 0.5f };
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
    enum class Phase {
        kShootHook,
        kPullPlayer
    };

    Vector3 targetPos_;
    float speed_ = 60.0f;
    float oldGravity_ = 0.0f;
    float wobbleTimer_ = 0.0f;
    float oldFovY_ = 0.45f;
    float spawnTimer_ = 0.0f;
    Phase phase_ = Phase::kShootHook;
    Vector3 hookTipPos_;
    float hookShootSpeed_ = 250.0f;
};

// --------------------------------------------------------
// ターザン用スイングフック状態 (Swing Hook)
// --------------------------------------------------------
class PlayerStateSwingHook : public IAnimationState
{
public:
    PlayerStateSwingHook(const Vector3& anchorPos) : anchorPos_(anchorPos) {}
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    void UpdateRopeMarker(Player* player, const Vector3& endPos, float thickness);
    void Release(Player* player);

    enum class Phase {
        kShootHook,
        kSwing
    };

    Vector3 anchorPos_;
    Vector3 hookTipPos_;
    Vector3 swingVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 releaseVelocity_ = { 0.0f, 0.0f, 0.0f };
    Phase phase_ = Phase::kShootHook;
    float oldGravity_ = 0.0f;
    float oldFovY_ = 0.45f;
    float ropeLength_ = 0.0f;
    float timer_ = 0.0f;
    bool released_ = false;
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
    enum class Phase {
        kShootHook,
        kPullEnemy
    };

    Object3d* targetEnemy_ = nullptr;
    Vector3 targetPos_;
    Vector3 hookTipPos_;
    Phase phase_ = Phase::kShootHook;
    Vector3 enemyStartPos_;
    float pullTimer_ = 0.0f;
    bool isHeavyPullTarget_ = false;
};

// --------------------------------------------------------
// オブジェクト引き寄せ状態 (Pull Object)
// --------------------------------------------------------
class PlayerStatePullObject : public IAnimationState
{
public:
    PlayerStatePullObject(Object3d* targetObject, const Vector3& targetPos)
        : targetObject_(targetObject), targetPos_(targetPos) {
    }
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    void UpdateRopeMarker(Player* player, const Vector3& endPos, float thickness);

    Object3d* targetObject_ = nullptr;
    Vector3 targetPos_;
    Vector3 hookTipPos_;
    float timer_ = 0.0f;
    bool pullStarted_ = false;
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