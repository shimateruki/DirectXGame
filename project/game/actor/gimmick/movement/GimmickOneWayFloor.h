#pragma once
#include "BaseGimmick.h"

/// <summary>通過方向によって衝突の有無が変わる床です。</summary>
class GimmickOneWayFloor : public BaseGimmick {
  public:
    virtual ~GimmickOneWayFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;
};
