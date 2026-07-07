#pragma once
#include "BaseGimmick.h"

class GimmickLaserNode : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    Vector3 GetLaserAnchorPosition() const;

    std::unique_ptr<Object3d> Clone() const override;

private:
    bool FindTargetPosition(Vector3& outTarget) const;
    void ApplyBeamTransform(const Vector3& source, const Vector3& target);
    void UpdateBeamDamage(const Vector3& source, const Vector3& target);
    float CalcDistancePointToSegment(const Vector3& point, const Vector3& start, const Vector3& end) const;
    Quaternion MakeYAxisToDirectionQuaternion(const Vector3& direction) const;
    float GetDamage() const;
    float GetDamageInterval() const;
    float GetThickness() const;

    std::unique_ptr<Object3d> beamVisual_;
    Vector3 basePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    bool initializedForPlay_ = false;
    bool active_ = true;
    float damageCooldownTimer_ = 0.0f;
    float pulseTimer_ = 0.0f;
};
