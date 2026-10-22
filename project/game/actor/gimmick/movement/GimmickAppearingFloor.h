#pragma once
#include "BaseGimmick.h"

/// <summary>トリガー後に一定時間だけ出現し、消滅前に点滅する床です。</summary>
class GimmickAppearingFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "AppearingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickAppearingFloor() = default;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    void OnTrigger() override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // 表示だけでなく衝突判定も同時に切り替えます。
    void SetFloorActive(bool active);

    float visibleTimer_ = 0.0f;
    float appearDuration_ = 3.0f;
    float blinkWarningTime_ = 1.0f;
    float blinkInterval_ = 0.12f;
    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;
    bool initializedForPlay_ = false;
    bool isActive_ = true;
};
