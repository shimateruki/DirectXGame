#pragma once
#include "BaseGimmick.h"

/// <summary>接触すると一定時間だけ有効になるスイッチです。</summary>
class GimmickTimedSwitch : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "TimedSwitch";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickTimedSwitch() = default;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    bool OnCollision(engine::Object3d* other) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    float contactTimer_ = 0.0f;
};
