#pragma once

#include "BaseGimmick.h"

// 離れた区画へプレイヤーを放物線状に運ぶスターランチャーです。
class GimmickLaunchStar : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;

private:
    Vector3 CalculateDestination(const Vector3& start) const;

    float pulseTimer_ = 0.0f;
    float retriggerCooldown_ = 0.0f;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    bool baseScaleCaptured_ = false;
};
