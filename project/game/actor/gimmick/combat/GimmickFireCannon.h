#pragma once
#include "BaseGimmick.h"
#include "game/actor/enemy/combat/AttackTelegraph.h"

class GimmickFireCannon : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void UpdateMortar(float deltaTime);
    AttackTelegraph landingWarning_;
    Vector3 lockedTarget_ = {};
    bool targetLocked_ = false;
    float warningTimer_ = 0.0f;
    float flightTimer_ = 0.0f;
    float recoilTimer_ = 0.0f;
    bool IsPlaying() const;
    bool FindFireDirection(Vector3& outDirection) const;
    Vector3 GetForwardDirection() const;
    Vector3 GetMuzzlePosition(const Vector3& direction) const;
    void RotateToward(const Vector3& direction, float deltaTime);
    void FireProjectile(const Vector3& direction);
    float GetProjectileSpeed() const;
    float GetFireInterval() const;
    float GetProjectileRadius() const;
    float GetTurnSpeedRadians() const;

    Vector3 basePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    bool initializedForPlay_ = false;
    bool active_ = true;
    float fireTimer_ = 0.0f;
};
