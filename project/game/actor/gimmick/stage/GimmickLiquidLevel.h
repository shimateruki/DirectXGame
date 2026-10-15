#pragma once
#include "BaseGimmick.h"

// スイッチ操作で液面を移動し、脱出中のシーンオブジェクトも制御します。
class GimmickLiquidLevel : public BaseGimmick {
  public:
    virtual ~GimmickLiquidLevel() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // 脱出演出の開始・終了に合わせて関連オブジェクトを切り替えます。
    void UpdateEscape(float deltaTime);
    void ResetEscapeObjects(bool active);
    bool escapeStarted_ = false;
    bool escapeFinished_ = false;
    float escapeTime_ = 0.0f;
    void ApplyTarget(bool active);
    void ApplyLiquidVisual();

    engine::Vector3 basePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 targetPosition_ = {0.0f, 0.0f, 0.0f};
    bool initializedForPlay_ = false;
    bool active_ = false;
};
