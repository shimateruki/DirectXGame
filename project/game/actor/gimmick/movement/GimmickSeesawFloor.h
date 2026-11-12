#pragma once
#include "BaseGimmick.h"

/// <summary>接触位置に応じて傾き、離れると元の角度に戻る床です。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickSeesawFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "SeesawFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickSeesawFloor() = default;

    // シーソーの天面を地面として扱い、乗員を支えられるようにする。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 編集時の姿勢を基準角として保持し、Play中の傾きから復帰できるようにする。
    void Update(float deltaTime) override;

    // 上面に乗ったPlayerの位置から左右どちらへ傾けるかを決める。
    bool OnCollision(engine::Object3D* other) override;

    // 揺れの途中状態は持ち越さず、配置と設定から新しい床を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    engine::Vector3 baseRotation_ = engine::Vector3::ZERO;
    float contactTimer_ = 0.0f; // 床やスイッチの接触保持までの残り秒数。
    float targetTilt_ = 0.0f;
    float halfLength_ = 5.0f;
    float maxTilt_ = 0.35f;
    float tiltSpeed_ = 7.0f;
    bool initializedBase_ = false;
};
