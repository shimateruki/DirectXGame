#pragma once

#include "AttackTelegraph.h"
#include "BaseGimmick.h"

/// <summary>
/// イベントで警告を開始し、上空から落下してプレイヤーを妨害する棘。
/// </summary>
class GimmickFallingSpike : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    enum class State {
        Dormant,
        Warning,
        Falling,
        Embedded,
        Hidden,
    };

    void CaptureStartTransform();
    void ResetRuntimeState();
    bool ShouldReturnAfterUse() const;
    void ApplyPlayerDamage(Object3d* playerObject);

    float GetWarningDuration() const;
    float GetDropDistance() const;
    float GetGravity() const;
    float GetDamage() const;

private:
    AttackTelegraph warningTelegraph_;
    State state_ = State::Dormant;
    Vector3 startPosition_{};
    Vector3 startRotation_{};
    float timer_ = 0.0f;
    float fallVelocityY_ = 0.0f;
    float damageCooldown_ = 0.0f;
    bool hasCapturedStartTransform_ = false;
};
