#pragma once
#include "BaseGimmick.h"

class GimmickOneWayFloor : public BaseGimmick {
public:
    virtual ~GimmickOneWayFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;
};
