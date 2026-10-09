#pragma once

#include "BaseGimmick.h"

/// <summary>マグマ上の継続ダメージと表面の発光を管理します。</summary>
class GimmickMagmaHazard : public BaseGimmick {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

  private:
    // エディター設定から接触ダメージと再適用間隔を解決します。
    float GetDamage() const;
    float GetDamageInterval() const;

    float damageCooldownTimer_ = 0.0f;
    float visualTimer_ = 0.0f;
    float baseEmissive_ = 1.8f;
    bool initializedForPlay_ = false;
};
