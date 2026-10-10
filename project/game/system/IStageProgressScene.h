#pragma once

class Object3d;
struct Vector3;

// Goal と StarCoin の接触イベントを扱うシーンだけが実装する能力。
class IStageProgressScene {
  public:
    virtual ~IStageProgressScene() = default;
    virtual bool TryStartGoalFromContact(Object3d* actor, Object3d* goal) = 0;
    virtual void CollectStarCoinFromContact(int coinIndex, const Vector3& worldPosition) = 0;
};
