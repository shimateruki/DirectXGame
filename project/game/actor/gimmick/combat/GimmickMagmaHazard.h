#pragma once

#include "BaseGimmick.h"

/// <summary>マグマ上の継続ダメージと表面の発光を管理します。</summary>
class GimmickMagmaHazard : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "MagmaHazard";
    static constexpr const char* kModelPath = "Stages/block";

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    bool OnCollision(engine::Object3d* other) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // エディター設定から接触ダメージと再適用間隔を解決します。
    float GetDamage() const;

    float GetDamageInterval() const;

    float damageCooldownTimer_ = 0.0f;
    float visualTimer_ = 0.0f;
    float baseEmissive_ = 1.8f;
    bool initializedForPlay_ = false;
};
