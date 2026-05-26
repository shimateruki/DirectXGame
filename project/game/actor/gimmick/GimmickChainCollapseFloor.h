#pragma once
#include "BaseGimmick.h"

class GimmickChainCollapseFloor : public BaseGimmick {
public:
    virtual ~GimmickChainCollapseFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    enum class State {
        Idle,
        Pending,
        Shaking,
        Falling,
        Hidden
    };

    void StartCollapse();
    void QueueCollapse();
    void ChangeState(State state);
    void TriggerNextFloor();
    float GetShakeDuration() const;
    float GetFallDuration() const;
    float GetChainDelay() const;
    float GetGravity() const;

    State state_ = State::Idle;
    Vector3 startPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 startRotation_ = { 0.0f, 0.0f, 0.0f };
    float timer_ = 0.0f;
    float pendingTimer_ = 0.0f;
    float velocityY_ = 0.0f;
    bool initializedStart_ = false;
    bool triggeredNext_ = false;
};
