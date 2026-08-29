#include "BaseItem.h"

#include "Object3dCommon.h"

void BaseItem::Initialize(Object3dCommon* common, const std::string& modelName) {
    Object3d::Initialize(common);
    SetClassName("Item");
    SetSaveCategory("Object");
    if (!modelName.empty()) {
        SetModel(modelName);
    }
}

std::unique_ptr<Object3d> BaseItem::Clone() const {
    auto clone = std::make_unique<BaseItem>();
    clone->Initialize(common_);
    clone->ImportFromJson(const_cast<BaseItem*>(this)->ExportToJson());
    return clone;
}
