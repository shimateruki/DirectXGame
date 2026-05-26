#pragma once
#include "BaseGimmick.h"

class GimmickRotatingObject : public BaseGimmick {
public:
    virtual ~GimmickRotatingObject() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    bool initializedForPlay_ = false;
    bool active_ = true;
};
