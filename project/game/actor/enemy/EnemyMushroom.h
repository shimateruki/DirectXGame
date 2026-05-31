#pragma once
#include "BaseEnemy.h"

class EnemyMushroom : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;

private:
    void UpdateFacing(const Vector3& direction);
    void DispatchSporeDamage(const Vector3& direction, float distance);
    void FireSporeProjectile(const Vector3& direction, float distance);
    void ApplySquashAnimation(float deltaTime);

    float attackCooldown_ = 0.8f;
    float attackTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    bool hasBaseScale_ = false;
};
