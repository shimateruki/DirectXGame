#pragma once
#include "BaseGimmick.h"

class GimmickSeesawFloor : public BaseGimmick {
public:
    virtual ~GimmickSeesawFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    float contactTimer_ = 0.0f;
    float targetTilt_ = 0.0f;
    float halfLength_ = 5.0f;
    float maxTilt_ = 0.35f;
    float tiltSpeed_ = 7.0f;
    bool initializedBase_ = false;
};
