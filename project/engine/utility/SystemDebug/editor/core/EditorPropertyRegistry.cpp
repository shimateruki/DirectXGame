#include "EditorPropertyRegistry.h"

#include "Object3d.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

using json = nlohmann::json;

bool JsonValuesEqual(const json& lhs, const json& rhs) {
    if (lhs.is_number() && rhs.is_number()) {
        return std::fabs(lhs.get<double>() - rhs.get<double>()) <= 0.00001;
    }
    if (lhs.is_array() && rhs.is_array()) {
        if (lhs.size() != rhs.size()) {
            return false;
        }
        for (std::size_t index = 0; index < lhs.size(); ++index) {
            if (!JsonValuesEqual(lhs[index], rhs[index])) {
                return false;
            }
        }
        return true;
    }
    return lhs == rhs;
}

EditorPropertyFlags CommonFlags(bool animatable = false) {
    EditorPropertyFlags flags = EditorPropertyFlags::MultiEdit | EditorPropertyFlags::PrefabOverride;
    if (animatable) {
        flags = flags | EditorPropertyFlags::Animatable;
    }
    return flags;
}

}

EditorPropertyRegistry* EditorPropertyRegistry::GetInstance() {
    static EditorPropertyRegistry instance;
    return &instance;
}

bool EditorPropertyRegistry::Register(EditorPropertyDescriptor descriptor) {
    if (descriptor.path.empty() || !descriptor.getter || !descriptor.setter ||
        propertyIndices_.find(descriptor.path) != propertyIndices_.end()) {
        return false;
    }

    propertyIndices_[descriptor.path] = properties_.size();
    properties_.push_back(std::move(descriptor));
    return true;
}

bool EditorPropertyRegistry::IsApplicable(const Object3d* object, const std::string& path) const {
    const EditorPropertyDescriptor* property = Find(path);
    if (!object || !property) {
        return false;
    }
    return !property->applicable || property->applicable(*object);
}

const EditorPropertyDescriptor* EditorPropertyRegistry::Find(const std::string& path) const {
    auto it = propertyIndices_.find(path);
    if (it == propertyIndices_.end()) {
        return nullptr;
    }
    return &properties_[it->second];
}

json EditorPropertyRegistry::GetValue(const Object3d* object, const std::string& path) const {
    const EditorPropertyDescriptor* property = Find(path);
    if (!object || !property || !property->getter || !IsApplicable(object, path)) {
        return json();
    }
    return property->getter(*object);
}

bool EditorPropertyRegistry::SetValue(Object3d* object, const std::string& path, const json& value) const {
    const EditorPropertyDescriptor* property = Find(path);
    if (!object || !property || !property->setter || !IsApplicable(object, path) ||
        HasEditorPropertyFlag(property->flags, EditorPropertyFlags::ReadOnly)) {
        return false;
    }
    return property->setter(*object, value);
}

bool EditorPropertyRegistry::HasMixedValue(const std::vector<Object3d*>& objects, const std::string& path) const {
    const EditorPropertyDescriptor* property = Find(path);
    if (!property || !property->getter || objects.size() < 2) {
        return false;
    }

    const Object3d* first = nullptr;
    for (Object3d* object : objects) {
        if (object && IsApplicable(object, path)) {
            first = object;
            break;
        }
    }
    if (!first) {
        return false;
    }

    const json reference = property->getter(*first);
    for (Object3d* object : objects) {
        if (object && IsApplicable(object, path) && !JsonValuesEqual(reference, property->getter(*object))) {
            return true;
        }
    }
    return false;
}

void EditorPropertyRegistry::InitializeBuiltInProperties() {
    if (initialized_) {
        return;
    }
    initialized_ = true;

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

    const auto cameraOnly = [](const Object3d& object) {
        return object.IsCameraObject();
    };
    const auto renderableOnly = [](const Object3d& object) {
        return !object.IsCameraObject() && object.GetMeshRenderer() != nullptr;
    };
    const auto gameplayParameterOnly = [](const Object3d& object) {
        const std::string className = object.GetClassName();
        return object.param_.has_value() && className != "Player" && className != "Enemy";
    };

    Register({
        "rendering.model", "モデル", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetModelName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetModel(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.blendMode", "合成方式", "Rendering", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetBlendMode())); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            const int mode = std::clamp(value.get<int>(), 0, 5);
            object.SetBlendMode(static_cast<BlendMode>(mode));
            return true;
        },
        renderableOnly,
        { "なし", "通常", "加算", "減算", "乗算", "スクリーン" },
    });
    Register({
        "rendering.materialType", "マテリアルType", "Rendering", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetMaterialType()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.SetMaterialType(value.get<int>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.texture", "基本Texture", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetTexturePath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetTexture(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.textureTiling", "Texture Tiling", "Rendering", EditorPropertyType::Vector2, CommonFlags(true),
        [](const Object3d& object) {
            const Vector2 value = object.GetTextureTiling();
            return json::array({ value.x, value.y });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 2) return false;
            object.SetTextureTiling({ value[0].get<float>(), value[1].get<float>() });
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.autoTextureTiling", "自動Tiling", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetAutoTextureTiling()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetAutoTextureTiling(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.lighting", "Lighting", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEnableLighting()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetEnableLighting(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.environmentMap", "環境Map", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEnableEnvMap()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetEnableEnvMap(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.environmentIntensity", "環境反射強度", "Rendering", EditorPropertyType::Number, CommonFlags(true),
        [](const Object3d& object) { return json(object.GetEnvIntensity()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.SetEnvIntensity((std::max)(value.get<float>(), 0.0f));
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.normalMapEnabled", "Normal Map", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEnableNormalMap()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetEnableNormalMap(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.normalMap", "Normal Texture", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNormalMapPath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetNormalMap(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.ormMap", "ORM Texture", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetOrmMapPath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetOrmMap(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });

    Register({
        "component.collision.type", "Collider Type", "Component", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetColliderConfig().type)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.type = static_cast<ColliderType>(std::clamp(value.get<int>(), 0, 6));
            object.SetColliderConfig(config);
            return true;
        },
        {},
        { "なし", "Sphere", "AABB", "OBB", "Cylinder", "Ring", "Terrain" },
    });
    Register({
        "component.collision.center", "Collider中心", "Component", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetColliderConfig().center;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.center = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            object.SetColliderConfig(config);
            return true;
        },
    });
    Register({
        "component.collision.size", "Colliderサイズ", "Component", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetColliderConfig().size;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.size = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            object.SetColliderConfig(config);
            return true;
        },
    });
    Register({
        "component.collision.rotation", "Collider回転", "Component", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetColliderConfig().rotation;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.rotation = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            object.SetColliderConfig(config);
            return true;
        },
    });
    Register({
        "component.collision.static", "Static Collision", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsStatic()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetStatic(value.get<bool>());
            return true;
        },
    });
    Register({
        "component.particle.cpu", "CPU Particle", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetParticleName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetParticleName(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.particle.gpu", "GPU Particle", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetGPUParticleName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetGPUParticleName(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.lod.enabled", "LOD有効", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsLodEnabled()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetLodEnabled(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "component.lod.levels", "LOD Levels", "Component", EditorPropertyType::String,
        EditorPropertyFlags::PrefabOverride,
        [](const Object3d& object) {
            json levels = json::array();
            for (const auto& level : object.GetLodLevels()) {
                levels.push_back({
                    { "level", level.level },
                    { "modelName", level.modelName },
                    { "distance", level.distance },
                });
            }
            return levels;
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array()) return false;
            std::vector<Object3d::LodLevel> levels;
            for (const auto& source : value) {
                if (!source.is_object()) continue;
                Object3d::LodLevel level;
                level.level = source.value("level", 0);
                level.modelName = source.value("modelName", "");
                level.distance = source.value("distance", 0.0f);
                if (level.level > 0 && !level.modelName.empty()) {
                    levels.push_back(std::move(level));
                }
            }
            object.SetLodLevels(levels);
            return true;
        },
        renderableOnly,
    });
    Register({
        "component.meshEffect.primary", "Mesh Effect 1", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetMeshEffect1Name()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetMeshEffect1Name(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.meshEffect.secondary", "Mesh Effect 2", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetMeshEffect2Name()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetMeshEffect2Name(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.animation.name", "Animation", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.animName_); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.animName_ = value.get<std::string>();
            return true;
        },
    });
    Register({
        "component.animation.loop", "Animation Loop", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.isAnimLoop_); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.isAnimLoop_ = value.get<bool>();
            return true;
        },
    });
    Register({
        "component.path.name", "Ghost Path", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.recordPathName_); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.recordPathName_ = value.get<std::string>();
            return true;
        },
    });
    Register({
        "component.path.loop", "Path Loop", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.isRecordLoop_); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.isRecordLoop_ = value.get<bool>();
            return true;
        },
    });
    Register({
        "component.path.relative", "Path相対座標", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.isRecordRelative_); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.isRecordRelative_ = value.get<bool>();
            return true;
        },
    });
    Register({
        "component.link.eventId", "Event ID", "Component", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEventID()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.SetEventID(value.get<int>());
            return true;
        },
    });
    Register({
        "component.link.targetId", "Target ID", "Component", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetTargetID()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.SetTargetID(value.get<int>());
            return true;
        },
    });

    Register({
        "camera.enabled", "Camera有効", "Camera", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().enabled); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.GetSceneCameraSettings().enabled = value.get<bool>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.role", "Camera役割", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().role)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().role = static_cast<SceneCameraRole>(std::clamp(value.get<int>(), 0, 1));
            return true;
        },
        cameraOnly,
        { "Main", "Cinematic" },
    });
    Register({
        "camera.fov", "FOV", "Camera", EditorPropertyType::Number, CommonFlags(true),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().fovY); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().fovY = std::clamp(value.get<float>(), 0.05f, 3.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.nearClip", "Near Clip", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().nearClip); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            auto& settings = object.GetSceneCameraSettings();
            settings.nearClip = (std::max)(value.get<float>(), 0.001f);
            settings.farClip = (std::max)(settings.farClip, settings.nearClip + 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.farClip", "Far Clip", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().farClip); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            auto& settings = object.GetSceneCameraSettings();
            settings.farClip = (std::max)(value.get<float>(), settings.nearClip + 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.eyeSource", "Eye取得元", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().eyeSource)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().eyeSource = static_cast<SceneCameraEyeSource>(std::clamp(value.get<int>(), 0, 1));
            return true;
        },
        cameraOnly,
        { "Camera Transform", "Scene Object" },
    });
    Register({
        "camera.eyeObject", "Eye追従Object", "Camera", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().eyeObjectName); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.GetSceneCameraSettings().eyeObjectName = value.get<std::string>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.eyeOffset", "Eye Offset", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetSceneCameraSettings().eyeOffset;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.GetSceneCameraSettings().eyeOffset = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.eyeFollowMode", "Eye追従方式", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().eyeFollowMode)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().eyeFollowMode =
                static_cast<SceneCameraFollowMode>(std::clamp(value.get<int>(), 0, 1));
            return true;
        },
        cameraOnly,
        { "完全追従", "Smooth" },
    });
    Register({
        "camera.eyeFollowResponse", "Eye追従レスポンス", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().eyeFollowResponse); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().eyeFollowResponse = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.targetMode", "注視方法", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().targetMode)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().targetMode = static_cast<SceneCameraTargetMode>(std::clamp(value.get<int>(), 0, 2));
            return true;
        },
        cameraOnly,
        { "固定座標", "Scene Object", "Camera前方" },
    });
    Register({
        "camera.targetObject", "注視Object", "Camera", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().targetObjectName); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.GetSceneCameraSettings().targetObjectName = value.get<std::string>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.targetOffset", "Target Offset", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetSceneCameraSettings().targetOffset;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.GetSceneCameraSettings().targetOffset = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.fixedTarget", "固定注視点", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetSceneCameraSettings().fixedTarget;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.GetSceneCameraSettings().fixedTarget = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.forwardDistance", "前方距離", "Camera", EditorPropertyType::Number, CommonFlags(true),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().forwardDistance); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().forwardDistance = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.targetFollowMode", "Target追従方式", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().targetFollowMode)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().targetFollowMode =
                static_cast<SceneCameraFollowMode>(std::clamp(value.get<int>(), 0, 1));
            return true;
        },
        cameraOnly,
        { "完全追従", "Smooth" },
    });
    Register({
        "camera.targetFollowResponse", "Target追従レスポンス", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().targetFollowResponse); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().targetFollowResponse = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.blendIn", "Blend In", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().blendInDuration); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().blendInDuration = (std::max)(value.get<float>(), 0.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.blendOut", "Blend Out", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetSceneCameraSettings().blendOutDuration); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().blendOutDuration = (std::max)(value.get<float>(), 0.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.easing", "切替Easing", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().easing)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().easing = static_cast<SceneCameraEasing>(std::clamp(value.get<int>(), 0, 4));
            return true;
        },
        cameraOnly,
        { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" },
    });

    auto registerParamNumber = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        float Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Number, CommonFlags(true),
            [member](const Object3d& object) { return json(object.param_.value().*member); },
            [member](Object3d& object, const json& value) {
                if (!value.is_number() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<float>();
                return true;
            },
            gameplayParameterOnly,
        });
    };
    auto registerParamInteger = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        int Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Integer, CommonFlags(),
            [member](const Object3d& object) { return json(object.param_.value().*member); },
            [member](Object3d& object, const json& value) {
                if (!value.is_number_integer() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<int>();
                return true;
            },
            gameplayParameterOnly,
        });
    };
    auto registerParamBool = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        bool Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Bool, CommonFlags(),
            [member](const Object3d& object) { return json(object.param_.value().*member); },
            [member](Object3d& object, const json& value) {
                if (!value.is_boolean() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<bool>();
                return true;
            },
            gameplayParameterOnly,
        });
    };
    auto registerParamString = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        std::string Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::String, CommonFlags(),
            [member](const Object3d& object) { return json(object.param_.value().*member); },
            [member](Object3d& object, const json& value) {
                if (!value.is_string() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<std::string>();
                return true;
            },
            gameplayParameterOnly,
        });
    };

    registerParamNumber("gameplay.healAmount", "回復量", &Object3d::EntityParameter::healAmount);
    registerParamNumber("gameplay.interval", "間隔", &Object3d::EntityParameter::interval);
    registerParamInteger("gameplay.maxCount", "最大数", &Object3d::EntityParameter::maxCount);
    registerParamNumber("gameplay.detectionRange", "感知範囲", &Object3d::EntityParameter::detectionRange);
    registerParamNumber("gameplay.shakeDuration", "揺れ時間", &Object3d::EntityParameter::shakeDuration);
    registerParamNumber("gameplay.fallDuration", "落下時間", &Object3d::EntityParameter::fallDuration);
    registerParamInteger("gameplay.colorType", "色Type", &Object3d::EntityParameter::colorType);
    registerParamInteger("gameplay.switchMode", "Switch Mode", &Object3d::EntityParameter::switchMode);
    registerParamInteger("gameplay.actionMode", "Action Mode", &Object3d::EntityParameter::actionMode);
    registerParamString("gameplay.targetScene", "移動先Scene", &Object3d::EntityParameter::targetScene);
    registerParamNumber("gameplay.moveAmount", "移動量", &Object3d::EntityParameter::moveAmount);
    registerParamNumber("gameplay.moveSpeed", "移動速度", &Object3d::EntityParameter::moveSpeed);
    registerParamBool("gameplay.startActive", "開始時有効", &Object3d::EntityParameter::startActive);
    registerParamBool("gameplay.returnOnOff", "OFFで戻る", &Object3d::EntityParameter::returnOnOff);
}
