#pragma once

#include "BaseGimmick.h"

class GimmickStageGate : public BaseGimmick {
public:
    ~GimmickStageGate() override = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;

    int GetStageIndex() const;
    int GetGateMode() const;
    const std::string& GetTargetSceneName() const;
    bool IsStageSelectNodeMode() const;
    void SetGateState(bool selected, bool unlocked, bool cleared, bool unlocking = false);
    void SetGateActivation(float activation);
    bool IsUnlocked() const { return isUnlocked_; }

private:
    void CaptureBaseScale();
    void UpdatePortalMaterial();
    bool CanTriggerTransition() const;
    void TriggerTransition();
    Vector4 GetTargetColor() const;

    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    float pulseTimer_ = 0.0f;
    float activation_ = 1.0f;
    float targetActivation_ = 1.0f;
    float activationBurst_ = 0.0f;
    float lastAppliedPulse_ = 1.0f;
    bool hasBaseScale_ = false;
    bool hasTriggeredTransition_ = false;
    bool isSelected_ = false;
    bool isUnlocked_ = true;
    bool isCleared_ = false;
    bool isUnlocking_ = false;
};
