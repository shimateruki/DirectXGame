#pragma once
#include "BaseGimmick.h"

class GimmickMovingFloor : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

private:
    Vector3 startPosition_{};
    Vector3 startRotation_{};
    Vector3 frameDelta_{};
    float time_ = 0.0f;
    float phaseOffset_ = 0.0f;
    bool hasCapturedStartTransform_ = false;
};
