#pragma once
#include "BaseGimmick.h"

// 接触または時間条件によって Target ID のイベントを切り替えるスイッチです。
class GimmickSwitch : public BaseGimmick {
  public:
    virtual ~GimmickSwitch() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

    // 脱出ギミックを再実行できるよう、出力と接触履歴を初期化します。
    void ResetEscapeCycle();
    bool IsOutputActive() const { return isOutputActive_; }

  private:
    void SendActive(bool active);
    int GetSwitchMode() const;

    float contactTimer_ = 0.0f;
    float timedTimer_ = 0.0f;
    bool wasPressed_ = false;
    bool isOutputActive_ = false;
};
