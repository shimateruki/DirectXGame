#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

/// <summary>フックを射出し、命中した地点までプレイヤーを引き寄せます。</summary>
class PlayerStateHook : public IAnimationState {
  public:
    explicit PlayerStateHook(const Vector3& targetPos) : targetPos_(targetPos) {}
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

  private:
    enum class Phase { kShootHook, kPullPlayer };

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

/// <summary>アンカーにロープを張り、振り子移動と解除時の速度を管理します。</summary>
class PlayerStateSwingHook : public IAnimationState {
  public:
    explicit PlayerStateSwingHook(const Vector3& anchorPos) : anchorPos_(anchorPos) {}
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

  private:
    // プレイヤーとフック先端の間に表示するロープを更新します。
    void UpdateRopeMarker(Player* player, const Vector3& endPos, float thickness);
    // 振り子移動を終了し、保持していた移動条件を復元します。
    void Release(Player* player);

    enum class Phase { kShootHook, kSwing };

    Vector3 anchorPos_;
    Vector3 hookTipPos_;
    Vector3 swingVelocity_ = {0.0f, 0.0f, 0.0f};
    Vector3 releaseVelocity_ = {0.0f, 0.0f, 0.0f};
    Phase phase_ = Phase::kShootHook;
    float oldGravity_ = 0.0f;
    float oldFovY_ = 0.45f;
    float ropeLength_ = 0.0f;
    float timer_ = 0.0f;
    bool released_ = false;
};
