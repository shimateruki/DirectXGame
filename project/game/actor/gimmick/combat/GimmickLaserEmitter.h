#pragma once
#include "BaseGimmick.h"

// スイッチ状態に応じてレーザーの向きと当たり判定を切り替える発射器です。
class GimmickLaserEmitter : public BaseGimmick {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

  private:
    bool FindTargetPosition(Vector3& outTarget) const;
    void ApplyBeamTransform(const Vector3& source, const Vector3& target);
    // 表示中の線と衝突判定の有効状態を同期します。
    void ApplyCollisionState(bool enabled);
    float GetDamage() const;
    float GetDamageInterval() const;
    float GetThickness() const;

    Vector3 basePosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f};
    Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    bool initializedForPlay_ = false;
    bool active_ = true;
    float damageCooldownTimer_ = 0.0f;
    float pulseTimer_ = 0.0f;
};
