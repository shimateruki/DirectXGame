#pragma once
#include "BaseGimmick.h"

/// <summary>接触時に加速を与え、発光の反応を再生するパネルです。</summary>
class GimmickDashPanel : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "DashPanel";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickDashPanel() = default;

    // 地面として接触し、踏んだ時の光が読める材質を既定にする。
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // 編集済みの基準色を最初に保存し、短い加速パルスの後に戻す。
    void Update(float deltaTime) override;

    // 上面から踏んだプレイヤーだけを加速し、接触し続けても発光を連射しない。
    bool OnCollision(engine::Object3d* other) override;

    // 配置と調整値を複製し、実行時のパルス時計は新しい床に持ち込まない。
    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    bool visualBaseCaptured_ = false;
    float visualTime_ = 0.0f;
    float activationPulse_ = 0.0f;
    float activationCooldown_ = 0.0f;
    float baseEmissive_ = 1.2f;
    engine::Vector4 baseColor_ = {1.0f, 0.38f, 0.04f, 1.0f};
};
