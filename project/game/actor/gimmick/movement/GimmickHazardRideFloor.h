#pragma once

#include "BaseGimmick.h"

/// <summary>
/// プレイヤーの乗車で動き始め、道中の妨害を順番に起動して終点で落下する輸送床。
/// </summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision / OnTrigger
class GimmickHazardRideFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "HazardRideFloor";
    static constexpr const char* kModelPath = "Stages/star_garden_ride_platform";

    /// 乗車床の移動距離、速度、妨害配置を初期化する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    /// 乗車から終点落下までの状態を進める。
    void Update(float deltaTime) override;

    /// プレイヤーの乗車を検知する。
    bool OnCollision(engine::Object3D* other) override;

    /// 外部トリガーから床の移動を要求する。
    void OnTrigger() override;

    /// 配置用の輸送床複製を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    enum class State {
        Waiting,
        Starting,
        Moving,
        Warning,
        Falling,
        Hidden,
    };

    /// Editor で配置した初期位置と回転を記録する。
    void CaptureStartTransform();

    /// 乗車演出と移動を開始する。
    void BeginRide();

    /// 移動進捗に応じて道中の妨害を順番に起動する。
    void TriggerPendingHazards(float progress);

    /// 再試行時に開始位置と妨害の進行を戻す。
    void ResetForRetry();

    /// 次の床状態とその経過時間を切り替える。
    void ChangeState(State nextState);

    /// 乗車イベントの終了後に初期位置へ戻すべきか返す。
    bool ShouldReturnAfterRide() const;

    /// 配置回転から進行方向を求める。
    engine::Vector3 GetTravelDirection() const;

    float GetTravelSpeed() const;

    float GetTravelDistance() const;

    float GetStartDelay() const;

    float GetEndWarningDuration() const;

    float GetFallDuration() const;

    float GetFallGravity() const;

    int GetHazardCount() const;

  private:
    State state_ = State::Waiting;
    engine::Vector3 startPosition_{};
    engine::Vector3 startRotation_{};
    engine::Vector3 frameDelta_{};
    float stateTimer_ = 0.0f; // 現在の状態に使う経過秒数。
    float travelledDistance_ = 0.0f;
    float fallVelocityY_ = 0.0f;
    int nextHazardIndex_ = 0;
    bool hasCapturedStartTransform_ = false;
};
