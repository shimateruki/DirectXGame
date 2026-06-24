#include "ItemFactory.h"
#include "game/actor/item/HealItem.h"

ItemFactory* ItemFactory::GetInstance() {
    static ItemFactory instance;
    return &instance;
}

// アイテムタイプ名から専用アイテムを生成する。未登録なら仮アイテムを返す。
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

    // タイプ名を本体とパラメータの両方に保存して、エディタ/イベント側から参照できるようにする
    newItem->SetItemType(itemName);
    if (!newItem->param_.has_value()) {
        newItem->param_.emplace();
    }
    newItem->param_->itemType = itemName;

    return newItem;
}
