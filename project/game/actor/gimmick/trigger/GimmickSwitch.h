#pragma once
#include "BaseGimmick.h"

// 接触または時間条件によって Target ID のイベントを切り替えるスイッチです。
class GimmickSwitch : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "Switch";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickSwitch() = default;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // 短い接触間隔では押下状態を保ち、踏んだ直後の瞬間的な判定抜けを吸収する。
    void Update(float deltaTime) override;

    // プレイヤーが上から踏んだ接触だけ押下に数え、側面衝突を除外する。
    bool OnCollision(engine::Object3d* other) override;

    // スイッチ固有の設定も含めて配置物を複製する。
    std::unique_ptr<engine::Object3d> Clone() const override;

    // 脱出ギミックを再実行できるよう、出力と接触履歴を初期化します。
    void ResetEscapeCycle();

    bool IsOutputActive() const { return isOutputActive_; }

  private:
    // 状態変更を一度だけイベントとして配信し、連続接触での重複発火を防ぎます。
    void SendActive(bool active);

    int GetSwitchMode() const;

    float contactTimer_ = 0.0f;
    float timedTimer_ = 0.0f;
    bool wasPressed_ = false;
    bool isOutputActive_ = false;
};
