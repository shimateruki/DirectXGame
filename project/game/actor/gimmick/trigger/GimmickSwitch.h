#pragma once
#include "BaseGimmick.h"

// 接触または時間条件によって Target ID のイベントを切り替えるスイッチです。
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickSwitch : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "Switch";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickSwitch() = default;

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 短い接触間隔では押下状態を保ち、踏んだ直後の瞬間的な判定抜けを吸収する。
    void Update(float deltaTime) override;

    // プレイヤーが上から踏んだ接触だけ押下に数え、側面衝突を除外する。
    bool OnCollision(engine::Object3D* other) override;

    // スイッチ固有の設定も含めて配置物を複製する。
    std::unique_ptr<engine::Object3D> Clone() const override;

    // 脱出ギミックを再実行できるよう、出力と接触履歴を初期化します。
    void ResetEscapeCycle();

    bool IsOutputActive() const { return isOutputActive_; }

  private:
    // 状態変更を一度だけイベントとして配信し、連続接触での重複発火を防ぎます。
    void SendActive(bool active);

    int GetSwitchMode() const;

    float contactTimer_ = 0.0f; // 床やスイッチの接触保持までの残り秒数。
    float timedTimer_ = 0.0f; // 時間制限スイッチの出力終了までの残り秒数。
    bool wasPressed_ = false;
    bool isOutputActive_ = false;
};
