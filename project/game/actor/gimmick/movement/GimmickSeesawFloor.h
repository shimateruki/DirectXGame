#pragma once
#include "BaseGimmick.h"

/// <summary>接触位置に応じて傾き、離れると元の角度に戻る床です。</summary>
class GimmickSeesawFloor : public BaseGimmick {
  public:
    virtual ~GimmickSeesawFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    engine::Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f};
    float contactTimer_ = 0.0f;
    float targetTilt_ = 0.0f;
    float halfLength_ = 5.0f;
    float maxTilt_ = 0.35f;
    float tiltSpeed_ = 7.0f;
    bool initializedBase_ = false;
};
