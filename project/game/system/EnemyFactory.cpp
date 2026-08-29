#include "EnemyFactory.h"

#include <algorithm>

EnemyFactory* EnemyFactory::GetInstance() {
    static EnemyFactory instance;
    return &instance;
}

bool EnemyFactory::Register(const std::string& typeName, Creator creator) {
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

bool EnemyFactory::Unregister(const std::string& typeName) {
    if (creators_.erase(typeName) == 0) {
        return false;
    }
    registrationOrder_.erase(
        std::remove(registrationOrder_.begin(), registrationOrder_.end(), typeName),
        registrationOrder_.end());
    return true;
}

void EnemyFactory::Clear() {
    creators_.clear();
    registrationOrder_.clear();
}

bool EnemyFactory::IsRegistered(const std::string& typeName) const {
    return creators_.find(typeName) != creators_.end();
}

std::vector<std::string> EnemyFactory::GetRegisteredTypes() const {
    return registrationOrder_;
}

std::unique_ptr<BaseEnemy> EnemyFactory::CreateEnemy(
    const std::string& typeName,
    Object3dCommon* common) const {
    const auto found = creators_.find(typeName);
    if (found == creators_.end() || !found->second) {
        return nullptr;
    }

    std::unique_ptr<BaseEnemy> enemy = found->second(common);
    if (enemy) {
        enemy->SetClassName("Enemy");
        enemy->SetSaveCategory("Enemy");
        enemy->SetEnemyType(typeName);
    }
    return enemy;
}
