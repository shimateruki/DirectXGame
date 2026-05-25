#pragma once
#include "BaseGimmick.h"

class GimmickSinkingFloor : public BaseGimmick {
public:
    virtual ~GimmickSinkingFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    Vector3 startPos_ = { 0.0f, 0.0f, 0.0f };
    float contactTimer_ = 0.0f;
    float sinkDepth_ = 2.0f;
    float sinkSpeed_ = 5.5f;
    bool initializedStart_ = false;
};
