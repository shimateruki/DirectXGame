#pragma once
#include "BaseGimmick.h"
class RidingPlatformController;

class GimmickMovingFloor : public BaseGimmick {
public:
    GimmickMovingFloor();
    ~GimmickMovingFloor() override;
    bool IsRidePlatform() const;
    std::unique_ptr<Object3d> Clone() const override;
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    void ResetEscapeCycle();

private:
    std::unique_ptr<RidingPlatformController> ride_;
    Vector3 startPosition_{};
    Vector3 startRotation_{};
    Vector3 frameDelta_{};
    float time_ = 0.0f;
    float phaseOffset_ = 0.0f;
    bool hasCapturedStartTransform_ = false;
};
