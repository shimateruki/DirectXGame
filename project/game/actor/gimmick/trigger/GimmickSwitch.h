#pragma once
#include "BaseGimmick.h"

class GimmickSwitch : public BaseGimmick {
public:
    virtual ~GimmickSwitch() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void SendActive(bool active);
    int GetSwitchMode() const;

    float contactTimer_ = 0.0f;
    float timedTimer_ = 0.0f;
    bool wasPressed_ = false;
    bool isOutputActive_ = false;
};
