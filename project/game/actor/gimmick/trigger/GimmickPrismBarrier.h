#pragma once

#include "BaseGimmick.h"

// 中ボス戦中だけ立ち上がる、当たり判定付きのプリズム障壁です。
class GimmickPrismBarrier : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void CaptureAuthoredState(bool force = false);
    void ResetForEditor();
    void ApplyRuntimeVisual();
    void SetCollisionEnabled(bool enabled);
    float GetTransitionDuration() const;

    Vector3 authoredPosition_{};
    Vector3 authoredScale_{ 1.0f, 1.0f, 1.0f };
    Vector4 authoredColor_{ 0.30f, 0.86f, 1.0f, 0.84f };
    uint32_t authoredCollisionAttribute_ = 0;
    uint32_t authoredCollisionMask_ = 0;
    float activation_ = 0.0f;
    float targetActivation_ = 0.0f;
    float visualTime_ = 0.0f;
    bool authoredStateCaptured_ = false;
    bool initializedForPlay_ = false;
    bool activationRequestReceived_ = false;
};
