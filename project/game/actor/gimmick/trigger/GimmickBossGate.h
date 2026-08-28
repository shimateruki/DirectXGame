#pragma once

#include "BaseGimmick.h"

// ボス戦開始時に上から閉じる、実体モデル付きの格子ゲートです。
class GimmickBossGate final : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;
    std::unique_ptr<Object3d> Clone() const override;

    // 開門中の退避座標ではなく、シーンで編集した閉鎖位置を返します。
    Vector3 GetClosedPosition();

private:
    void CaptureAuthoredState(bool force = false);
    void ResetForEditor();
    void ApplyRuntimeState();
    void SetCollisionEnabled(bool enabled);
    float GetTransitionDuration() const;
    float GetTravelDistance() const;

    Vector3 authoredPosition_{};
    Vector3 authoredScale_{ 1.0f, 1.0f, 1.0f };
    uint32_t authoredCollisionAttribute_ = 0;
    uint32_t authoredCollisionMask_ = 0;
    float closure_ = 0.0f;
    float targetClosure_ = 0.0f;
    bool authoredStateCaptured_ = false;
    bool initializedForPlay_ = false;
    bool activationRequested_ = false;
};
