#pragma once

#include "Character.h"

class Player;

/// 新しい敵実装を追加するための最小基底クラスです。
/// target_はScene側が所有するため、派生クラスは対象削除時に参照を更新してください。
class BaseEnemy : public Character {
public:
    BaseEnemy() = default;
    ~BaseEnemy() override = default;

    virtual void Initialize(Object3dCommon* common, const std::string& modelName = {});
    void Update(float deltaTime) override;

    void SetTarget(Object3d* target) { target_ = target; }
    Object3d* GetTarget() const { return target_; }

    /// 派生型は動的型を維持するため、自身のCloneを必ずoverrideしてください。
    std::unique_ptr<Object3d> Clone() const override;

private:
    Object3d* target_ = nullptr; // 所有権を持たない追跡対象。
};
