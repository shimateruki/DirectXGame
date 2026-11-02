#pragma once
#include "BaseGimmick.h"

// スイッチからの有効/無効イベントを、配置物の位置・表示・衝突状態へ反映します。
class GimmickEventReceiver : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "EventReceiver";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickEventReceiver() = default;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // 編集時の配置を再生基準にし、ボス報酬はイベント発火まで隠します。
    void Update(float deltaTime) override;

    // 単発トリガーはスイッチのオンイベントとして扱います。
    void OnTrigger() override;

    // 再生開始前に届いたイベントも、基準姿勢の取得後に適用できるよう保留します。
    void OnSwitchEvent(bool active) override;

    // 演出による移設では、移動先だけでなく再生の基準位置も更新する。
    void RelocateRuntimeBase(const engine::Vector3& position);

    // 複製先を初期化してから、イベント設定と配置をコピーします。
    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // モード別に表示・衝突・移動先を切り替え、報酬出現時は着地まで衝突を遅らせます。
    void ApplyActiveState(bool active);

    void SetCollisionEnabled(bool enabled);

    engine::Vector3 basePosition_ = engine::Vector3::ZERO;
    engine::Vector3 targetPosition_ = engine::Vector3::ZERO;
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 baseRotation_ = engine::Vector3::ZERO;

    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;

    engine::Vector4 originalColor_ = {0.65f, 1.0f, 0.65f, 1.0f};

    float activationTimer_ = 0.0f;

    bool initializedForPlay_ = false;
    bool active_ = false;

    bool hasPendingActive_ = false;
    bool pendingActive_ = false;
};
