#pragma once
#include "BaseGimmick.h"

class GimmickHookPullBlock : public BaseGimmick {
public:
    virtual ~GimmickHookPullBlock() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;

    void StartHookPull(const Vector3& hookOwnerPos);

private:
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 pullTarget_ = { 0.0f, 0.0f, 0.0f };
    float pullTimer_ = 0.0f;
};
