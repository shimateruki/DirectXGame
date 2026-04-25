#pragma once
#include "BaseGimmick.h"

class GimmickMovingFloor : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

private:
    float startY_ = 0.0f;
    float time_ = 0.0f;
};
