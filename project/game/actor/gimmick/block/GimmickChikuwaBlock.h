#pragma once
#include "BaseGimmick.h"

// 乗ると揺れ、落下・非表示を経て再出現する足場です。時間は param_ があればその設定を使います。
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickChikuwaBlock : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "ChikuwaBlock";
    static constexpr const char* kModelPath = "Stages/block";

    enum class State {
        Idle,    // 待機中（足場として存在）
        Shaking, // プレイヤーが乗って震えている
        Falling, // 落下中
        Hidden   // 消滅中（リスポーン待ち）
    };

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    // 待機中のみプレイヤーの搭乗を判定
    bool OnCollision(engine::Object3D* other) override;

    // 同じモデルで新しい実体を初期化してから、配置と設定値を複製する。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    void ChangeState(State newState);

    State state_ = State::Idle;
    float timer_ = 0.0f;
    engine::Vector3 startPos_;
    float velocityY_ = 0.0f;

    // 秒単位の各段階の長さと、ワールド単位/秒² の落下加速度です。
    float GetShakeDuration() const { return GetParameters().has_value() ? GetParameters()->shakeDuration : 1.0f; }

    float GetFallDuration() const { return GetParameters().has_value() ? GetParameters()->fallDuration : 2.0f; }

    float GetRespawnDuration() const { return GetParameters().has_value() ? GetParameters()->interval : 3.0f; }

    float GetGravity() const { return GetParameters().has_value() ? GetParameters()->gravity : 30.0f; }
};
