#include "ItemFactory.h"
#include "game/actor/item/HealItem.h"

ItemFactory* ItemFactory::GetInstance() {
    static ItemFactory instance;
    return &instance;
}

std::unique_ptr<BaseItem> ItemFactory::CreateItem(const std::string& itemName, Object3dCommon* common) {
    std::unique_ptr<BaseItem> newItem = nullptr;

    if (itemName == "Heal") {
        auto heal = std::make_unique<HealItem>();
        heal->Initialize(common, "Item/heart.gltf");
        newItem = std::move(heal);
    }

    if (!newItem) {
        newItem = std::make_unique<BaseItem>();
        newItem->Initialize(common, "Primitives/sphere");
    }

    newItem->SetItemType(itemName);
    if (!newItem->param_.has_value()) {
        newItem->param_.emplace();
    }
    newItem->param_->itemType = itemName;

    return newItem;
}
