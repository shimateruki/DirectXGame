#pragma once

#include "BaseGimmick.h"

/// <summary>
/// プレイヤーの乗車で動き始め、道中の妨害を順番に起動して終点で落下する輸送床。
/// </summary>
class GimmickHazardRideFloor : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    enum class State {
        Waiting,
        Starting,
        Moving,
        Warning,
        Falling,
        Hidden,
    };

    void CaptureStartTransform();
    void BeginRide();
    void TriggerPendingHazards(float progress);
    void ResetForRetry();
    void ChangeState(State nextState);
    bool ShouldReturnAfterRide() const;
    Vector3 GetTravelDirection() const;

    float GetTravelSpeed() const;
    float GetTravelDistance() const;
    float GetStartDelay() const;
    float GetEndWarningDuration() const;
    float GetFallDuration() const;
    float GetFallGravity() const;
    int GetHazardCount() const;

private:
    State state_ = State::Waiting;
    Vector3 startPosition_{};
    Vector3 startRotation_{};
    Vector3 frameDelta_{};
    float stateTimer_ = 0.0f;
    float travelledDistance_ = 0.0f;
    float fallVelocityY_ = 0.0f;
    int nextHazardIndex_ = 0;
    bool hasCapturedStartTransform_ = false;
};
