#pragma once
#include "BaseGimmick.h"

/// <summary>上面を踏んだとき、種類に応じてパネル加速を開始または解除し、発光します。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickDashPanel : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "DashPanel";
    static constexpr const char* kStopTypeId = "DashPanelStop";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickDashPanel() = default;

    // 地面として接触し、踏んだ時の光が読める材質を既定にする。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 編集済みの基準色を最初に保存し、短い発光パルスの後に戻す。
    void Update(float deltaTime) override;

    // 上面を踏んだプレイヤーのパネル加速を更新し、発光の連続発火を抑えます。
    bool OnCollision(engine::Object3D* other) override;

    // 配置と調整値を複製し、実行時のパルス時計は新しい床に持ち込まない。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    bool visualBaseCaptured_ = false;
    float visualTime_ = 0.0f; // 見た目の周期に使う経過秒数。
    float activationPulse_ = 0.0f;
    float activationCooldown_ = 0.0f;
    float baseEmissive_ = 1.2f;
    engine::Vector4 baseColor_ = {1.0f, 0.38f, 0.04f, 1.0f};
};
