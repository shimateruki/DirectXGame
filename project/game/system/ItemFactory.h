#pragma once
#include "game/actor/item/BaseItem.h"
#include "Object3dCommon.h"
#include <memory>
#include <string>

// 文字列のアイテムタイプ名から、対応するアイテムを生成するファクトリ
class ItemFactory {
public:
    static ItemFactory* GetInstance();

    // itemName に対応するアイテムを生成し、基本初期化まで行う
    std::unique_ptr<BaseItem> CreateItem(const std::string& itemName, Object3dCommon* common);

private:
    ItemFactory() = default;
    ~ItemFactory() = default;
    ItemFactory(const ItemFactory&) = delete;
    const ItemFactory& operator=(const ItemFactory&) = delete;
};
