#pragma once
#include "BaseGimmick.h"

/// <summary>接触時に加速を与え、発光の反応を再生するパネルです。</summary>
class GimmickDashPanel : public BaseGimmick {
  public:
    virtual ~GimmickDashPanel() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    bool visualBaseCaptured_ = false;
    float visualTime_ = 0.0f;
    float activationPulse_ = 0.0f;
    float activationCooldown_ = 0.0f;
    float baseEmissive_ = 1.2f;
    engine::Vector4 baseColor_ = {1.0f, 0.38f, 0.04f, 1.0f};
};
