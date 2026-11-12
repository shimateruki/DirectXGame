#pragma once
#include "BaseGimmick.h"

// スイッチ操作で液面を移動し、脱出中のシーンオブジェクトも制御します。
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnTrigger / OnSwitchEvent
class GimmickLiquidLevel : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "LiquidLevel";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickLiquidLevel() = default;

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 脱出シーケンスだけは通常のスイッチ昇降と別の時間軸で進めます。
    void Update(float deltaTime) override;

    // 脱出用液面はエリア進行で動くため、通常のトリガー入力を無視します。
    void OnTrigger() override;

    // オフで戻さない設定なら、解除イベント後も上昇位置を維持します。
    void OnSwitchEvent(bool active) override;

    // 複製先の描画資源を作った後で、液面の設定を引き継ぎます。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    // 脱出演出の開始・終了に合わせて関連オブジェクトを切り替えます。
    void UpdateEscape(float deltaTime);

    void ResetEscapeObjects(bool active);

    bool escapeStarted_ = false;
    bool escapeFinished_ = false;
    float escapeTime_ = 0.0f;

    // 配置時の高さを基準に、設定した距離だけ上下させます。
    void ApplyTarget(bool active);

    // 色種別に合わせて水とマグマの材質・透過色を切り替えます。
    void ApplyLiquidVisual();

    engine::Vector3 basePosition_ = engine::Vector3::ZERO;
    engine::Vector3 targetPosition_ = engine::Vector3::ZERO;
    bool initializedForPlay_ = false;
    bool active_ = false;
};
