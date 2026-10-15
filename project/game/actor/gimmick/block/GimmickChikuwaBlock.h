#pragma once
#include "BaseGimmick.h"

// 乗ると揺れ、落下・非表示を経て再出現する足場です。時間は param_ があればその設定を使います。
class GimmickChikuwaBlock : public BaseGimmick {
  public:
    enum class State {
        Idle,    // 待機中（足場として存在）
        Shaking, // プレイヤーが乗って震えている
        Falling, // 落下中
        Hidden   // 消滅中（リスポーン待ち）
    };

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    void ChangeState(State newState);

    State state_ = State::Idle;
    float timer_ = 0.0f;
    engine::Vector3 startPos_;
    float velocityY_ = 0.0f;

    // 秒単位の各段階の長さと、ワールド単位/秒² の落下加速度です。
    float GetShakeDuration() const { return param_.has_value() ? param_->shakeDuration : 1.0f; }
    float GetFallDuration() const { return param_.has_value() ? param_->fallDuration : 2.0f; }
    float GetRespawnDuration() const { return param_.has_value() ? param_->interval : 3.0f; }
    float GetGravity() const { return param_.has_value() ? param_->gravity : 30.0f; }
};
