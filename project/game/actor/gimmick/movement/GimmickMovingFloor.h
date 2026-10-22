#pragma once
#include "BaseGimmick.h"
class RidingPlatformController;

/// <summary>移動床の軌道と、乗っているキャラクターへの移動量を管理します。</summary>
class GimmickMovingFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "MovingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    GimmickMovingFloor();

    ~GimmickMovingFloor() override;

    bool IsRidePlatform() const;

    std::unique_ptr<engine::Object3d> Clone() const override;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    bool OnCollision(engine::Object3d* other) override;

    // 逃走ギミックで床を再利用するとき、移動周期を初期位置から再開します。
    void ResetEscapeCycle();

  private:
    std::unique_ptr<RidingPlatformController> ride_;
    engine::Vector3 startPosition_{};
    engine::Vector3 startRotation_{};
    engine::Vector3 frameDelta_{};
    float time_ = 0.0f;
    float phaseOffset_ = 0.0f;
    bool hasCapturedStartTransform_ = false;
};
