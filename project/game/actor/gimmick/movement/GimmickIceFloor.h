#pragma once
#include "BaseGimmick.h"

/// <summary>接触したキャラクターに氷床の滑りを適用します。</summary>
class GimmickIceFloor : public BaseGimmick {
  public:
    virtual ~GimmickIceFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;
};
