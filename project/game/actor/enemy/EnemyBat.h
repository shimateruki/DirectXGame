#pragma once
#include "BaseEnemy.h"

class EnemyBat : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;

private:
    enum class BatState {
        Orbit,
        Telegraph,
        Dive,
        Recover
    };

    void CaptureHomePosition();
    void EnsureAnimation();
    void UpdateFacing(const Vector3& direction);
    Vector3 CalcOrbitPosition() const;
    void SetPlayerContactEnabled(bool enabled);

    Vector3 homePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 diveTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    BatState state_ = BatState::Orbit;
    float hoverTimer_ = 0.0f;
    float stateTimer_ = 0.0f;
    float diveCooldown_ = 2.0f;
    bool hasHomePosition_ = false;
};
