#pragma once
#include "BaseGimmick.h"

class Player;

// 爆発のみで破壊可能なブロックギミック
class GimmickBreakableBlock : public BaseGimmick {
public:
    virtual ~GimmickBreakableBlock() = default;

    // 初期化
    void Initialize(Object3dCommon* common, const std::string& modelName) override;

    // 更新処理
    void Update(float deltaTime) override;

    // 衝突判定
    bool OnCollision(Object3d* other) override;

    // actionMode=6は汎用衝撃、7はピンクのバウンド落下だけで破壊します。
    bool TryBreakByPlayerImpact(const Player* player);

    std::unique_ptr<Object3d> Clone() const override;

private:
    void Break(); // 破壊処理

private:
    bool isBroken_ = false;
};
