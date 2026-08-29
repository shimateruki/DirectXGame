#include "GimmickFactory.h"

#include <algorithm>

GimmickFactory* GimmickFactory::GetInstance() {
    static GimmickFactory instance;
    return &instance;
}

bool GimmickFactory::Register(const std::string& typeName, Creator creator) {
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

bool GimmickFactory::Unregister(const std::string& typeName) {
    if (creators_.erase(typeName) == 0) {
        return false;
    }
    registrationOrder_.erase(
        std::remove(registrationOrder_.begin(), registrationOrder_.end(), typeName),
        registrationOrder_.end());
    return true;
}

void GimmickFactory::Clear() {
    creators_.clear();
    registrationOrder_.clear();
}

bool GimmickFactory::IsRegistered(const std::string& typeName) const {
    return creators_.find(typeName) != creators_.end();
}

std::vector<std::string> GimmickFactory::GetRegisteredTypes() const {
    return registrationOrder_;
}

std::unique_ptr<BaseGimmick> GimmickFactory::CreateGimmick(
    const std::string& typeName,
    Object3dCommon* common) const {
    const auto found = creators_.find(typeName);
    if (found == creators_.end() || !found->second) {
        return nullptr;
    }

    std::unique_ptr<BaseGimmick> gimmick = found->second(common);
    if (gimmick) {
        gimmick->SetClassName("Gimmick");
        gimmick->SetSaveCategory("Object");
        gimmick->SetGimmickType(typeName);
    }
    return gimmick;
}
