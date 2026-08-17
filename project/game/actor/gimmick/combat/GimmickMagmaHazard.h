#pragma once

#include "BaseGimmick.h"

class GimmickMagmaHazard : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    float GetDamage() const;
    float GetDamageInterval() const;

    float damageCooldownTimer_ = 0.0f;
    float visualTimer_ = 0.0f;
    float baseEmissive_ = 1.8f;
    bool initializedForPlay_ = false;
};
