#pragma once
#include "BaseGimmick.h"

/// <summary>接触すると一定時間だけ有効になるスイッチです。</summary>
class GimmickTimedSwitch : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "TimedSwitch";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickTimedSwitch() = default;

    // 上面を踏める地面として登録し、待機時の色を設定する。
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // 接触が途切れたら短い保持時間後に待機色へ戻す。
    void Update(float deltaTime) override;

    // 上面から踏まれた間だけ指定 EventID へ通知する。
    bool OnCollision(engine::Object3d* other) override;

    // 押下タイマーは持ち込まず、配置と接続先の設定だけを複製する。
    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    float contactTimer_ = 0.0f;
};
