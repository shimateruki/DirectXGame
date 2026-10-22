#pragma once
#include "BaseGimmick.h"
#include <cstdint>

/// <summary>トリガーやスイッチによって回転状態を切り替えるオブジェクトです。</summary>
class GimmickRotatingObject : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "RotatingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickRotatingObject() = default;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    void OnTrigger() override;

    void OnSwitchEvent(bool active) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    bool initializedForPlay_ = false;
    bool active_ = true;
    float pathRotationOffset_ = 0.0f;
    engine::Object3d* supportFloor_ = nullptr;
    engine::Vector3 supportLocalPosition_{};
    engine::Vector3 supportStartRotation_{};
    engine::Vector3 startPosition_{};
    engine::Vector3 startRotation_{};
    uint32_t initialCollisionAttribute_ = 0;
    bool initialVisible_ = true;
};
