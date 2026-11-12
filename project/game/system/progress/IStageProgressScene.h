#pragma once

namespace engine { class Object3D; }
namespace engine { struct Vector3; }

// Goal と StarCoin の接触イベントを扱うシーンだけが実装する能力。
// 関連：GameRuleがゴール・スターコイン接触を通知する。実際の進行は各シーンが実装する。
// 主な入口：TryStartGoalFromContact / CollectStarCoinFromContact
class IStageProgressScene {
  public:
    virtual ~IStageProgressScene() = default;

    // 接触を受けたシーン側で開始条件を判定し、ゴール処理を受け付けたかを返す。
    virtual bool TryStartGoalFromContact(engine::Object3D* actor, engine::Object3D* goal) = 0;

    // coinIndexはステージ内の収集番号、worldPositionは取得演出の発生地点として渡す。
    virtual void CollectStarCoinFromContact(int coinIndex, const engine::Vector3& worldPosition) = 0;
};
