#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

namespace engine { class Object3d; }

/// <summary>フックで対象物を引き寄せる状態とロープ表示を管理します。</summary>
class PlayerStatePullObject : public IAnimationState {
  public:
    PlayerStatePullObject(engine::Object3d* targetObject, const engine::Vector3& targetPos)
        : targetObject_(targetObject), targetPos_(targetPos) {}
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

  private:
    void UpdateRopeMarker(Player* player, const engine::Vector3& endPos, float thickness);

    // 対象の生存期間は呼び出し元が管理します。
    engine::Object3d* targetObject_ = nullptr;
    engine::Vector3 targetPos_;
    engine::Vector3 hookTipPos_;
    float timer_ = 0.0f;
    bool pullStarted_ = false;
};
