#pragma once

#include "game/actor/item/BaseItem.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Object3dCommon;

/// Itemタイプ名と生成処理を登録するFactoryです。初期状態は空です。
/// Creatorは受け取ったObject3dCommonで初期化済みのインスタンスを返してください。
class ItemFactory {
public:
    using Creator = std::function<std::unique_ptr<BaseItem>(Object3dCommon*)>;

    static ItemFactory* GetInstance();

    /// 同名登録はCreatorを置き換えますが、エディター表示順は最初の登録位置を維持します。
    bool Register(const std::string& typeName, Creator creator);
    bool Unregister(const std::string& typeName);
    void Clear();
    bool IsRegistered(const std::string& typeName) const;
    std::vector<std::string> GetRegisteredTypes() const;
    std::unique_ptr<BaseItem> CreateItem(
        const std::string& typeName,
        Object3dCommon* common) const;

private:
    ItemFactory() = default;
    std::unordered_map<std::string, Creator> creators_;
    std::vector<std::string> registrationOrder_;
};
