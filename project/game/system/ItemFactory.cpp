#include "ItemFactory.h"

#include <algorithm>

ItemFactory* ItemFactory::GetInstance() {
    static ItemFactory instance;
    return &instance;
}

bool ItemFactory::Register(const std::string& typeName, Creator creator) {
    if (typeName.empty() || !creator) {
        return false;
    }
    const bool isNew = creators_.find(typeName) == creators_.end();
    creators_[typeName] = std::move(creator);
    if (isNew) {
        registrationOrder_.push_back(typeName);
    }
    return true;
}

bool ItemFactory::Unregister(const std::string& typeName) {
    if (creators_.erase(typeName) == 0) {
        return false;
    }
    registrationOrder_.erase(
        std::remove(registrationOrder_.begin(), registrationOrder_.end(), typeName),
        registrationOrder_.end());
    return true;
}

void ItemFactory::Clear() {
    creators_.clear();
    registrationOrder_.clear();
}

bool ItemFactory::IsRegistered(const std::string& typeName) const {
    return creators_.find(typeName) != creators_.end();
}

std::vector<std::string> ItemFactory::GetRegisteredTypes() const {
    return registrationOrder_;
}

std::unique_ptr<BaseItem> ItemFactory::CreateItem(
    const std::string& typeName,
    Object3dCommon* common) const {
    const auto found = creators_.find(typeName);
    if (found == creators_.end() || !found->second) {
        return nullptr;
    }

    std::unique_ptr<BaseItem> item = found->second(common);
    if (item) {
        item->SetClassName("Item");
        item->SetSaveCategory("Object");
        item->SetItemType(typeName);
    }
    return item;
}
