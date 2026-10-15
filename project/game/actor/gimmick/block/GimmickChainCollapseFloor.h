#pragma once
#include "BaseGimmick.h"

// 踏まれた床が揺れて落下し、次の床へ連鎖する足場です。
class GimmickChainCollapseFloor : public BaseGimmick {
  public:
    virtual ~GimmickChainCollapseFloor() = default;

    /// 配置床の初期姿勢と連鎖先を準備する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 連鎖待ち、揺れ、落下、再試行の状態を進める。
    void Update(float deltaTime) override;
    /// プレイヤーが乗ったとき落下を開始する。
    bool OnCollision(engine::Object3d* other) override;
    /// 前の床からの連鎖要求を受け取る。
    void OnTrigger() override;

    /// 配置用の崩落床複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // 待機中の連鎖遅延と、実際の揺れ・落下を区別します。
    enum class State { Idle, Pending, Shaking, Falling, Hidden };

    /// 揺れから落下する過程を開始する。
    void StartCollapse();
    /// 前の床からの連鎖を遅延予約する。
    void QueueCollapse();
    /// 再試行時に初期姿勢と連鎖状態を戻す。
    void ResetForRetry();
    /// 床の段階とタイマーを切り替える。
    void ChangeState(State state);
    /// 連鎖先の床へ起動イベントを送る。
    void TriggerNextFloor();
    /// 落下後に床を初期位置へ戻すべきか返す。
    bool ShouldReturnAfterCollapse() const;
    float GetShakeDuration() const;
    float GetFallDuration() const;
    float GetChainDelay() const;
    float GetGravity() const;

    State state_ = State::Idle;
    engine::Vector3 startPosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 startRotation_ = {0.0f, 0.0f, 0.0f};
    float timer_ = 0.0f;
    float pendingTimer_ = 0.0f;
    float velocityY_ = 0.0f;
    float returnTimer_ = 0.0f;
    bool initializedStart_ = false;
    bool triggeredNext_ = false;
};
