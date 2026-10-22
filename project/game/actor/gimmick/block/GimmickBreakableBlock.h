#pragma once
#include "BaseGimmick.h"

class Player;

// 爆発のみで破壊可能なブロックギミック
class GimmickBreakableBlock : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "BreakableBlock";
    static constexpr const char* kModelPath = "Stages/bomb_break_block";

    virtual ~GimmickBreakableBlock() = default;

    // 初期化
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // 更新処理
    void Update(float deltaTime) override;

    // 衝突判定
    bool OnCollision(engine::Object3d* other) override;

    // actionMode=6は汎用衝撃、7はピンクのバウンド落下だけで破壊します。
    bool TryBreakByPlayerImpact(const Player* player);

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    void Break(); // 破壊処理

  private:
    bool isBroken_ = false;
};
