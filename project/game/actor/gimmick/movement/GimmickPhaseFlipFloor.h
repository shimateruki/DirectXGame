#pragma once
#include "BaseGimmick.h"

// フェーズ周期で足場の可視性と衝突属性を交互に切り替えます。
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update
class GimmickPhaseFlipFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "PhaseFlipFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickPhaseFlipFloor() = default;

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 編集時は元の状態へ戻し、再生時だけ位相のタイマーを進めます。
    void Update(float deltaTime) override;

    // 複製先の基本資源を作ってから配置と周期設定を移します。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    // 現在のフェーズに属する時だけ、足場として踏める状態にします。
    void ApplyState(bool isOwnPhase, float phaseProgress);

    engine::Vector4 GetPhaseColor(float alpha) const;

    int GetPhaseIndex() const;

    int GetPhaseCount() const;

    float GetPhaseDuration() const;

    // 回転の開始と終了で角速度が急変しないようにします。
    float SmoothStep(float t) const;

    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;
    engine::Vector3 baseRotation_ = engine::Vector3::ZERO;
    float cycleTimer_ = 0.0f;
    bool initializedForPlay_ = false;
    bool wasOwnPhase_ = false;
    int flipCount_ = 0;
};
