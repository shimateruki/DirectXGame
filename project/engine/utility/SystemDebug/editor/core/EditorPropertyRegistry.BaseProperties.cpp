#include "EditorPropertyRegistry.h"
#include "EditorPropertyRegistry.BuiltIn.h"
#include "Object3d.h"

#include <algorithm>

using nlohmann::json;
using EditorPropertyBuiltIn::CommonFlags;

void EditorPropertyRegistry::RegisterBuiltInBaseProperties() {
    // 全オブジェクトに共通する基本プロパティを先に登録します。
    Register({
        "identity.name", "名前", "Identity", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetName(value.get<std::string>());
            return true;
        },
    });
    Register({
        "identity.guid", "Object GUID", "Identity", EditorPropertyType::String,
        EditorPropertyFlags::ReadOnly,
        [](const Object3d& object) { return json(object.GetPersistentGuid()); },
        [](Object3d&, const json&) { return false; },
    });
    Register({
        "identity.tag", "Tag", "Identity", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetTag()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetTag(value.get<std::string>());
            return true;
        },
    });
    Register({
        "identity.layer", "Layer", "Identity", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetLayer()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetLayer(value.get<std::string>());
            return true;
        },
    });
    Register({
        "identity.saveCategory", "保存先カテゴリ", "Identity", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSaveCategory()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetSaveCategory(value.get<std::string>());
            return true;
        },
    });
    Register({
        "transform.position", "座標", "Transform", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetTranslate();
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.SetTranslate({ value[0].get<float>(), value[1].get<float>(), value[2].get<float>() });
            object.UpdateLocalMatrix();
            object.UpdateWorldMatrix();
            return true;
        },
    });
    Register({
        "transform.rotation", "回転", "Transform", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetRotation();
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.SetRotation({ value[0].get<float>(), value[1].get<float>(), value[2].get<float>() });
            object.UpdateLocalMatrix();
            object.UpdateWorldMatrix();
            return true;
        },
    });
    Register({
        "transform.scale", "スケール", "Transform", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetScale();
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.SetScale({ value[0].get<float>(), value[1].get<float>(), value[2].get<float>() });
            object.UpdateLocalMatrix();
            object.UpdateWorldMatrix();
            return true;
        },
    });
    Register({
        "rendering.visible", "表示", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetIsVisible()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetIsVisible(value.get<bool>());
            return true;
        },
    });
    Register({
        "editor.locked", "編集ロック", "Editor", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetIsLocked()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetIsLocked(value.get<bool>());
            return true;
        },
    });
    Register({
        "rendering.castShadow", "影を落とす", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetCastShadow()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetCastShadow(value.get<bool>());
            return true;
        },
    });
    Register({
        "rendering.color", "色", "Rendering", EditorPropertyType::Vector4, CommonFlags(true),
        [](const Object3d& object) {
            const Vector4 value = object.GetColor();
            return json::array({ value.x, value.y, value.z, value.w });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 4) return false;
            object.SetColor({ value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() });
            return true;
        },
    });
    Register({
        "rendering.metallic", "金属度", "Rendering", EditorPropertyType::Number, CommonFlags(true),
        [](const Object3d& object) { return json(object.GetMetallic()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.SetMetallic(value.get<float>());
            return true;
        },
    });
    Register({
        "rendering.roughness", "粗さ", "Rendering", EditorPropertyType::Number, CommonFlags(true),
        [](const Object3d& object) { return json(object.GetRoughness()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.SetRoughness(value.get<float>());
            return true;
        },
    });
    Register({
        "rendering.emissive", "発光強度", "Rendering", EditorPropertyType::Number, CommonFlags(true),
        [](const Object3d& object) { return json(object.GetEmissive()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.SetEmissive(value.get<float>());
            return true;
        },
    });
    Register({
        "collision.attribute", "衝突属性", "Collision", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetCollisionAttribute()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
            object.SetCollisionAttribute(value.get<std::uint32_t>());
            return true;
        },
    });
    Register({
        "collision.mask", "衝突対象", "Collision", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetCollisionMask()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
            object.SetCollisionMask(value.get<std::uint32_t>());
            return true;
        },
    });
}
