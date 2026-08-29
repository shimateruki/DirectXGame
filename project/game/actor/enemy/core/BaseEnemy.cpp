#include "BaseEnemy.h"

#include "Object3dCommon.h"

void BaseEnemy::Initialize(Object3dCommon* common, const std::string& modelName) {
    Character::Initialize(common);
    SetClassName("Enemy");
    SetSaveCategory("Enemy");
    if (!modelName.empty()) {
        SetModel(modelName);
    }
}

void BaseEnemy::Update(float deltaTime) {
    Character::Update(deltaTime);
}

std::unique_ptr<Object3d> BaseEnemy::Clone() const {
    auto clone = std::make_unique<BaseEnemy>();
    clone->Initialize(common_);
    clone->ImportFromJson(const_cast<BaseEnemy*>(this)->ExportToJson());
    return clone;
}
