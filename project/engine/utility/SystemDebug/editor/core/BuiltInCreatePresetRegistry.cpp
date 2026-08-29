#include "BuiltInCreatePresetRegistry.h"

#include "Object3d.h"
#include "Object3dCommon.h"
#include "PresetManager.h"
#include "json.hpp"

#include <memory>
#include <string>

using json = nlohmann::json;

namespace {
bool RegisterPresetOnce(const std::string& presetName, std::unique_ptr<Object3d> object, const std::string& displayName) {
    if (!object || PresetManager::GetInstance()->HasPreset(presetName)) {
        return false;
    }

    object->SetName(displayName);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    json data = object->ExportToJson();
    data["name"] = displayName;
    data["builtinCreateTemplate"] = true;
    PresetManager::GetInstance()->GetPreset(presetName) = data;
    return true;
}

std::unique_ptr<Object3d> CreateSpriteCardPreset(Object3dCommon* common) {
    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    object->SetClassName("Model");
    object->SetModel("Primitives/plane");
    object->SetName("SpriteCard_2_5D");
    object->SetScale({ 1.8f, 1.8f, 1.0f });
    object->SetTexture("Resources/sprite/common/white.png");
    object->SetBlendMode(BlendMode::kNormal);
    object->SetMaterialType(0);
    object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    object->SetEnableLighting(false);
    object->SetEnableEnvMap(false);
    object->SetEmissive(1.35f);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);

    ColliderConfig colliderConfig = object->GetColliderConfig();
    colliderConfig.type = ColliderType::kNone;
    object->SetColliderConfig(colliderConfig);
    return object;
}
}

void BuiltInCreatePresetRegistry::EnsureRegistered(Object3dCommon* common) {
    if (!common) {
        return;
    }

    const bool changed = RegisterPresetOnce(
        "Builtin/Utility/SpriteCard2_5D",
        CreateSpriteCardPreset(common),
        "2.5Dスプライト板");
    if (changed) {
        PresetManager::GetInstance()->SaveAll();
    }
}
