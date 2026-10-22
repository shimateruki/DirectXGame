#pragma once
#include "BaseGimmick.h"

// フェーズ周期で足場の可視性と衝突属性を交互に切り替えます。
class GimmickPhaseFlipFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "PhaseFlipFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickPhaseFlipFloor() = default;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // 現在のフェーズに属する時だけ、足場として踏める状態にします。
    void ApplyState(bool isOwnPhase, float phaseProgress);

    engine::Vector4 GetPhaseColor(float alpha) const;

    int GetPhaseIndex() const;

    int GetPhaseCount() const;

    float GetPhaseDuration() const;

    float SmoothStep(float t) const;

    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;
    engine::Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f};
    float cycleTimer_ = 0.0f;
    bool initializedForPlay_ = false;
    bool wasOwnPhase_ = false;
    int flipCount_ = 0;
};
