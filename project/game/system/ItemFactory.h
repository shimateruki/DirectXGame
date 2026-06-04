#pragma once
#include "game/actor/item/BaseItem.h"
#include "Object3dCommon.h"
#include <memory>
#include <string>

class ItemFactory {
public:
    static ItemFactory* GetInstance();

    std::unique_ptr<BaseItem> CreateItem(const std::string& itemName, Object3dCommon* common);

private:
    ItemFactory() = default;
    ~ItemFactory() = default;
    ItemFactory(const ItemFactory&) = delete;
    const ItemFactory& operator=(const ItemFactory&) = delete;
};
