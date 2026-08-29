#include "BaseGimmick.h"

#include "Object3dCommon.h"

void BaseGimmick::Initialize(Object3dCommon* common, const std::string& modelName) {
    Object3d::Initialize(common);
    SetClassName("Gimmick");
    SetSaveCategory("Object");
    if (!modelName.empty()) {
        SetModel(modelName);
    }
}

std::unique_ptr<Object3d> BaseGimmick::Clone() const {
    auto clone = std::make_unique<BaseGimmick>();
    clone->Initialize(common_);
    clone->ImportFromJson(const_cast<BaseGimmick*>(this)->ExportToJson());
    return clone;
}
