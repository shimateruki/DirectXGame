#include "game/system/scene/ItemFactory.h"

ItemFactory* ItemFactory::GetInstance() {
    // 種類ごとの生成表をプロセス内で共有する。
    static ItemFactory instance;
    return &instance;
}

// 廃止済み種別は生成せず、それ以外の旧アイテムデータは共通型で読み込みます。
std::unique_ptr<BaseItem> ItemFactory::CreateItem(const std::string& itemName, engine::Object3dCommon* common) {
    // 廃止済みの回復アイテムや種別未設定のデータは、別アイテムへ置き換えず生成しません。
    if (itemName.empty() || itemName == "Heal") {
        return nullptr;
    }

    auto newItem = std::make_unique<BaseItem>();
    newItem->Initialize(common, "Primitives/sphere");

    // タイプ名を本体とパラメータの両方に保存して、エディタ/イベント側から参照できるようにする
    newItem->SetItemType(itemName);

    if (!newItem->GetParameters().has_value()) {
        newItem->ResetParameters();
    }

    newItem->SetParameter(&engine::EntityParameter::itemType, itemName);

    return newItem;
}
