#pragma once
#include "BaseGimmick.h"

class GimmickPhaseFlipFloor : public BaseGimmick {
public:
    virtual ~GimmickPhaseFlipFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void ApplyState(bool isOwnPhase, float phaseProgress);
    Vector4 GetPhaseColor(float alpha) const;
    int GetPhaseIndex() const;
    int GetPhaseCount() const;
    float GetPhaseDuration() const;
    float SmoothStep(float t) const;

    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    float cycleTimer_ = 0.0f;
    bool initializedForPlay_ = false;
    bool wasOwnPhase_ = false;
    int flipCount_ = 0;
};
