#pragma once

namespace engine { class Object3d; }
namespace engine { struct Vector3; }

// Goal と StarCoin の接触イベントを扱うシーンだけが実装する能力。
class IStageProgressScene {
  public:
    virtual ~IStageProgressScene() = default;
    virtual bool TryStartGoalFromContact(engine::Object3d* actor, engine::Object3d* goal) = 0;
    virtual void CollectStarCoinFromContact(int coinIndex, const engine::Vector3& worldPosition) = 0;
};
