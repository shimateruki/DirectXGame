#pragma once
#include "BaseEnemy.h"
#include <functional>
#include <memory>

class EnemyBomber : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;

    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = callback;
    }

private:
    enum class ThrowState {
        Idle,
        Windup
    };

    bool IsTargetInRange(float* outDistance = nullptr, Vector3* outDirection = nullptr) const;
    void UpdateFacing(const Vector3& direction);
    void UpdateFootwork(float deltaTime, const Vector3& direction, float distance, bool inRange);
    void BeginThrow();
    void ThrowBomb();

    float throwTimer_ = 0.0f;
    float throwInterval_ = 2.8f;
    float windupTimer_ = 0.0f;
    float throwWindup_ = 0.7f;
    float initialThrowDelay_ = 0.6f;
    float footworkTimer_ = 0.0f;
    float footworkDirection_ = 1.0f;
    ThrowState throwState_ = ThrowState::Idle;
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_ = nullptr;
    Object3dCommon* common_ = nullptr;
};
