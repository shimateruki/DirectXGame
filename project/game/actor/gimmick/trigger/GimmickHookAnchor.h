#pragma once
#include "BaseGimmick.h"

/// <summary>フックで掴めるアンカーの衝突と待機時の脈動を管理します。</summary>
class GimmickHookAnchor : public BaseGimmick {
  public:
    virtual ~GimmickHookAnchor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

  private:
    float pulseTimer_ = 0.0f;
};
