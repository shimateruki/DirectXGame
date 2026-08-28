#pragma once
#include "BaseGimmick.h"

class GimmickEventReceiver : public BaseGimmick {
public:
    virtual ~GimmickEventReceiver() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void ApplyActiveState(bool active);
    void SetCollisionEnabled(bool enabled);

    Vector3 basePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 targetPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;
    Vector4 originalColor_ = { 0.65f, 1.0f, 0.65f, 1.0f };
    float activationTimer_ = 0.0f;
    bool initializedForPlay_ = false;
    bool active_ = false;
    bool hasPendingActive_ = false;
    bool pendingActive_ = false;
};
