#include "engine/editor/assets/EditorObjectDefaults.h"
#include "Object3D.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {
std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ContainsGroundKeyword(const std::string& value) {
    return value.find("block") != std::string::npos ||
        value.find("floor") != std::string::npos ||
        value.find("ground") != std::string::npos ||
        value.find("platform") != std::string::npos ||
        value.find("terrain") != std::string::npos ||
        value.find("wall") != std::string::npos;
}

bool ShouldApplyGroundDefaults(engine::Object3D* object) {
    // 地形として扱えるモデルだけに初期Colliderを補い、敵・演出・子Meshには付けない。
    if (!object) {
        return false;
    }

    const std::string className = ToLowerAscii(object->GetClassName());
    if (className == "enemy" || className == "player" || className == "gimmick" ||
        className == "item" || className == "gpuparticle" || className == "cinematiccamera" ||
        className == "spritecard" || className == "meshroot" || className == "meshpart") {
        return false;
    }

    const std::string modelName = ToLowerAscii(object->GetModelName());

    if (modelName.empty()) {
        return false;
    }

    if (modelName.rfind("characters/", 0) == 0 || modelName.rfind("item/", 0) == 0) {
        return false;
    }

    if (modelName == "primitives/plane" || modelName.find("portal") != std::string::npos) {
        return false;
    }

    return modelName.rfind("stages/", 0) == 0 ||
        modelName == "primitives/cube" ||
        ContainsGroundKeyword(modelName);
}

}

void EditorObjectDefaults::ApplyGroundCollision(engine::Object3D* object) {
    // 自動生成した地形の未設定項目だけに地面用の衝突属性を与える。
    if (!ShouldApplyGroundDefaults(object)) {
        return;
    }

    auto colliderConfig = object->GetColliderConfig();

    if (colliderConfig.type == ColliderType::None) {
        colliderConfig.type = ColliderType::Aabb;
        object->SetColliderConfig(colliderConfig);
    }

    if (object->GetCollisionAttribute() == 0) {
        object->SetCollisionAttribute(CollisionCategories::Get("ground"));
    }

    if (object->GetCollisionMask() == 0) {
        object->SetCollisionMask(0xFFFFFFFFu);
    }
}
