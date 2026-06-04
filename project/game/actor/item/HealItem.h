#pragma once
#include "BaseItem.h"

// プレイヤーのHPを回復するアイテム
class HealItem : public BaseItem {
public:
    virtual ~HealItem() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    std::unique_ptr<Object3d> Clone() const override;

protected:
    void Collect(Player* player) override;
};
