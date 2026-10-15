#pragma once
#include "BaseGimmick.h"

// プレイヤーが取得できるコインギミック
class GimmickCoin : public BaseGimmick {
  public:
    virtual ~GimmickCoin() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;
    std::unique_ptr<engine::Object3d> Clone() const override;
    // 敵からの一時ドロップに切り替えます。速度はワールド単位/秒、各時刻は秒、groundY はワールド Y 座標です。
    // 寿命は最低 0.2 秒に補正され、blinkStartTime は寿命を超えない値に丸められます。
    void ConfigureTemporaryDrop(const engine::Vector3& initialVelocity, float lifetime, float blinkStartTime, float groundY);

  protected:
    void CaptureReplayCustomState(json& state) const override;
    void RestoreReplayCustomState(const json& state) override;

  private:
    void Collect();
    void UpdateTemporaryDrop(float deltaTime);
    void RequestSelfRemove();

  private:
    bool isCollected_ = false;
    bool isTemporaryDrop_ = false;
    float rotationSpeed_ = 3.0f;
    float collectAnimationTimer_ = 0.0f;
    float dropAge_ = 0.0f;
    float dropLifetime_ = 0.0f;
    float dropBlinkStartTime_ = 0.0f;
    float dropGroundY_ = 0.0f;
    float dropGroundOffset_ = 0.0f;
    float dropSettleTimer_ = 0.0f;
    engine::Vector3 dropVelocity_{0.0f, 0.0f, 0.0f};
    engine::Vector3 collectStartPosition_{};
    engine::Vector3 collectStartScale_{0.6f, 0.6f, 0.15f};
};
