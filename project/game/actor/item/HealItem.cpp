#include "HealItem.h"
#include "DebugConsole.h"
#include "Player.h"
#include <algorithm>
#include <cassert>

void HealItem::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseItem::Initialize(common, modelName);

    SetName("Item_Heal");
    SetItemType("Heal");
    SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
    SetEmissive(1.8f);
    SetScale({ 0.8f, 0.8f, 0.8f });
    SetCollisionRadius(1.2f);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->itemType = "Heal";
    param_->healAmount = 1.0f;
}

void HealItem::Collect(Player* player) {
    if (!player) {
        MarkCollected();
        return;
    }

    if (!player->param_.has_value()) {
        player->param_.emplace();
    }

    float healAmount = 1.0f;
    if (param_.has_value()) {
        healAmount = (std::max)(0.0f, param_->healAmount);
    }

    float beforeHp = player->param_->hp;
    float maxHp = (std::max)(1.0f, player->param_->maxHp);
    player->param_->hp = (std::min)(maxHp, beforeHp + healAmount);

    DebugConsole::GetInstance()->AddLog(
        "Heal Item: +" + std::to_string(healAmount) +
        " HP (" + std::to_string(beforeHp) + " -> " + std::to_string(player->param_->hp) + ")"
    );

    MarkCollected();
}

std::unique_ptr<Object3d> HealItem::Clone() const {
    auto newObj = std::make_unique<HealItem>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
