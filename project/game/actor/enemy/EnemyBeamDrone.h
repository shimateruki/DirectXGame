#pragma once
#include "BaseEnemy.h"
#include <memory>

class EnemyBeamDrone : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    std::unique_ptr<Object3d> Clone() const override;

private:
    enum class BeamState {
        Idle,
        Charge,
        Beam
    };

    void CaptureHomePosition();
    void UpdateHover(float deltaTime);
    void UpdateFacing(const Vector3& direction);
    void StartCharge();
    void FireBeam();
    void UpdateBeamVisual();
    void UpdateBeamDamage();
    float CalcDistancePointToSegment(const Vector3& point, const Vector3& start, const Vector3& end) const;
    Quaternion MakeYAxisToDirectionQuaternion(const Vector3& direction) const;

    std::unique_ptr<Object3d> beamVisual_;
    BeamState state_ = BeamState::Idle;
    Vector3 homePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 beamStart_ = { 0.0f, 0.0f, 0.0f };
    Vector3 beamEnd_ = { 0.0f, 0.0f, 0.0f };
    float hoverTimer_ = 0.0f;
    float cooldownTimer_ = 1.0f;
    float chargeTimer_ = 0.0f;
    float beamTimer_ = 0.0f;
    bool hasHomePosition_ = false;
    bool beamDamageDone_ = false;
};
