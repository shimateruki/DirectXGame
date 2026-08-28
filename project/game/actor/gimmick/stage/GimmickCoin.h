#pragma once
#include "BaseGimmick.h"

// プレイヤーが取得できるコインギミック
class GimmickCoin : public BaseGimmick {
public:
    virtual ~GimmickCoin() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ConfigureTemporaryDrop(const Vector3& initialVelocity, float lifetime, float blinkStartTime, float groundY);

protected:
    void CaptureReplayCustomState(json& state) const override;
    void RestoreReplayCustomState(const json& state) override;

private:
    void Collect();
    void UpdateTemporaryDrop(float deltaTime);
    void RequestSelfRemove();

private:
    bool isCollected_ = false;
    bool isTemporaryDrop_ = false;
    float rotationSpeed_ = 3.0f;
    float collectAnimationTimer_ = 0.0f;
    float dropAge_ = 0.0f;
    float dropLifetime_ = 0.0f;
    float dropBlinkStartTime_ = 0.0f;
    float dropGroundY_ = 0.0f;
    float dropSettleTimer_ = 0.0f;
    Vector3 dropVelocity_{ 0.0f, 0.0f, 0.0f };
    Vector3 collectStartPosition_{};
    Vector3 collectStartScale_{ 0.6f, 0.6f, 0.15f };
};
