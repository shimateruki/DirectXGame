#pragma once

#include "BaseGimmick.h"

// プレイヤーの接触開始を既存の被弾処理へ渡す障害物。モデルと判定は配置側で変更できる。
// 主な入口：Initialize / OnTriggerEnter。接触の継続と再接触の判定はCollisionManagerが担当する。
class GimmickContactHazard final : public BaseGimmick {
  public:
    static constexpr const char* kTypeId = "ContactHazard";
    static constexpr const char* kModelPath = "Gimmicks/volcanic_spike_bar";

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;
    void OnTriggerEnter(const CollisionEvent& event) override;
    std::unique_ptr<engine::Object3D> Clone() const override;
};
