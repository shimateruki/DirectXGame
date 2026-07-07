#pragma once
#include "BaseGimmick.h"

class GimmickAppearingFloor : public BaseGimmick {
public:
    virtual ~GimmickAppearingFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void OnTrigger() override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void SetFloorActive(bool active);

    float visibleTimer_ = 0.0f;
    float appearDuration_ = 3.0f;
    float blinkWarningTime_ = 1.0f;
    float blinkInterval_ = 0.12f;
    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;
    bool initializedForPlay_ = false;
    bool isActive_ = true;
};
