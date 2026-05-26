#pragma once
#include "BaseGimmick.h"

class GimmickLiquidLevel : public BaseGimmick {
public:
    virtual ~GimmickLiquidLevel() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void ApplyTarget(bool active);
    void ApplyLiquidVisual();

    Vector3 basePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 targetPosition_ = { 0.0f, 0.0f, 0.0f };
    bool initializedForPlay_ = false;
    bool active_ = false;
};
