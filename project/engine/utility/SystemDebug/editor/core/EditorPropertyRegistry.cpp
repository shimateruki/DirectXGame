#include "EditorPropertyRegistry.h"

#include "GhostRecorder.h"
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

bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

const json* FindSerializedPath(const json& source, const std::string& path) {
    const json* current = &source;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t separator = path.find('.', begin);
        const std::string key = path.substr(
            begin,
            separator == std::string::npos ? std::string::npos : separator - begin);
        if (!current->is_object() || !current->contains(key)) {
            return nullptr;
        }
        current = &current->at(key);
        if (separator == std::string::npos) {
            return current;
        }
        begin = separator + 1;
    }
    return current;
}

bool TryReadSerializedPresenceMarker(
    const json& source,
    const std::string& componentTypeId,
    bool& present) {
    const json* marker = FindSerializedPath(
        source,
        "components." + componentTypeId + "._editorPresent");
    if (!marker || !marker->is_boolean()) {
        return false;
    }
    present = marker->get<bool>();
    return true;
}

bool HasNonEmptySerializedString(const json& source, const char* path) {
    const json* value = FindSerializedPath(source, path);
    return value && value->is_string() && !value->get_ref<const std::string&>().empty();
}

bool HasNonZeroSerializedInteger(const json& source, const char* path) {
    const json* value = FindSerializedPath(source, path);
    if (!value || (!value->is_number_integer() && !value->is_number_unsigned())) {
        return false;
    }
    return value->get<std::int64_t>() != 0;
}

bool ResolveSerializedComponentPresence(const json& source, const std::string& componentTypeId) {
    bool markerPresent = false;
    if (TryReadSerializedPresenceMarker(source, componentTypeId, markerPresent)) {
        return markerPresent;
    }

    if (componentTypeId == "Collider") {
        return HasNonZeroSerializedInteger(source, "collider.type") ||
            HasNonZeroSerializedInteger(source, "collisionAttribute") ||
            HasNonZeroSerializedInteger(source, "collisionMask");
    }
    if (componentTypeId == "ParticleEmitter") {
        return HasNonEmptySerializedString(source, "particleName") ||
            HasNonEmptySerializedString(source, "gpuParticleName");
    }
    if (componentTypeId == "LOD") {
        const json* enabled = FindSerializedPath(source, "lod.enabled");
        const json* levels = FindSerializedPath(source, "lod.levels");
        return (enabled && enabled->is_boolean() && enabled->get<bool>()) ||
            (levels && levels->is_array() && !levels->empty());
    }
    if (componentTypeId == "MeshEffect") {
        return HasNonEmptySerializedString(source, "meshEffect1") ||
            HasNonEmptySerializedString(source, "meshEffect2");
    }
    if (componentTypeId == "Animator") {
        return HasNonEmptySerializedString(source, "animation.animName") ||
            HasNonEmptySerializedString(source, "animation.animatorController");
    }
    if (componentTypeId == "PathMover") {
        return HasNonEmptySerializedString(source, "recorder.recordPathName");
    }
    if (componentTypeId == "GameplayLink") {
        const json* eventId = FindSerializedPath(source, "myEventID");
        const json* targetId = FindSerializedPath(source, "targetID");
        const bool hasEvent = eventId && eventId->is_number_integer() && eventId->get<int>() >= 0;
        const bool hasTarget = targetId && targetId->is_number_integer() && targetId->get<int>() >= 0;
        return hasEvent || hasTarget;
    }
    return false;
}

void SetSerializedComponentPresence(
    json& source,
    const std::string& componentTypeId,
    bool present) {
    source["components"][componentTypeId]["_editorPresent"] = present;
    if (present) {
        return;
    }

    // Componentを削除したVariantでも、基底Prefabの実行時設定が残らない値へ戻します。
    if (componentTypeId == "Collider") {
        source["collider"]["type"] = static_cast<int>(ColliderType::kNone);
        source["collisionAttribute"] = 0;
        source["collisionMask"] = 0;
        source["isStatic"] = false;
    }
    else if (componentTypeId == "ParticleEmitter") {
        source["components"][componentTypeId]["cpuParticle"] = "";
        source["components"][componentTypeId]["gpuParticle"] = "";
        source["particleName"] = "";
        source["gpuParticleName"] = "";
    }
    else if (componentTypeId == "LOD") {
        source["lod"]["enabled"] = false;
        source["lod"]["levels"] = json::array();
    }
    else if (componentTypeId == "MeshEffect") {
        source["components"][componentTypeId]["primary"] = "";
        source["components"][componentTypeId]["secondary"] = "";
        source["meshEffect1"] = "";
        source["meshEffect2"] = "";
    }
    else if (componentTypeId == "Animator") {
        source["animation"]["animName"] = "";
        source["animation"]["isAnimLoop"] = false;
        source["animation"]["animatorController"] = "";
    }
    else if (componentTypeId == "PathMover") {
        source["components"][componentTypeId]["path"] = "";
        source["components"][componentTypeId]["loop"] = false;
        source["components"][componentTypeId]["relative"] = false;
        source["recorder"]["recordPathName"] = "";
        source["recorder"]["isRecordLoop"] = false;
        source["recorder"]["isRecordRelative"] = false;
    }
    else if (componentTypeId == "GameplayLink") {
        source["components"][componentTypeId]["eventId"] = -1;
        source["components"][componentTypeId]["targetId"] = -1;
        source["myEventID"] = -1;
        source["targetID"] = -1;
    }
}

std::string ResolveComponentTypeId(const std::string& path) {
    if (StartsWith(path, "identity.")) return "SceneObject";
    if (StartsWith(path, "transform.")) return "Transform";
    if (StartsWith(path, "rendering.")) return "MeshRenderer";
    if (StartsWith(path, "editor.")) return "EditorState";
    if (StartsWith(path, "collision.") || StartsWith(path, "component.collision.")) return "Collider";
    if (StartsWith(path, "component.particle.")) return "ParticleEmitter";
    if (StartsWith(path, "component.lod.")) return "LOD";
    if (StartsWith(path, "component.meshEffect.")) return "MeshEffect";
    if (StartsWith(path, "component.animation.")) return "Animator";
    if (StartsWith(path, "component.path.")) return "PathMover";
    if (StartsWith(path, "component.link.")) return "GameplayLink";
    if (StartsWith(path, "camera.")) return "Camera";
    if (StartsWith(path, "gameplay.")) return "Gameplay";
    return "SceneObject";
}

std::string ResolveSerializedPath(const std::string& path) {
    if (path == "identity.name") return "name";
    if (path == "identity.guid") return "guid";
    if (path == "identity.tag") return "tag";
    if (path == "identity.layer") return "layer";
    if (path == "identity.saveCategory") return "saveCategory";
    if (path == "transform.position") return "translate";
    if (path == "transform.rotation") return "rotate";
    if (path == "transform.scale") return "scale";
    if (path == "rendering.visible") return "isVisible";
    if (path == "rendering.castShadow") return "castShadow";
    if (path == "rendering.color") return "color";
    if (path == "rendering.metallic") return "metallic";
    if (path == "rendering.roughness") return "roughness";
    if (path == "rendering.emissive") return "emissive";
    if (path == "rendering.model") return "modelName";
    if (path == "rendering.blendMode") return "blendMode";
    if (path == "rendering.materialType") return "materialType";
    if (path == "rendering.texture") return "texturePath";
    if (path == "rendering.textureTiling") return "textureTiling";
    if (path == "rendering.autoTextureTiling") return "autoTextureTiling";
    if (path == "rendering.lighting") return "enableLighting";
    if (path == "rendering.environmentMap") return "enableEnvMap";
    if (path == "rendering.environmentIntensity") return "envIntensity";
    if (path == "rendering.normalMapEnabled") return "enableNormalMap";
    if (path == "rendering.normalMap") return "normalMapPath";
    if (path == "rendering.ormMap") return "ormMapPath";
    if (path == "editor.locked") return "isLocked";
    if (path == "collision.attribute") return "collisionAttribute";
    if (path == "collision.mask") return "collisionMask";
    if (path == "component.collision.type") return "collider.type";
    if (path == "component.collision.center") return "collider.center";
    if (path == "component.collision.size") return "collider.size";
    if (path == "component.collision.rotation") return "collider.rotation";
    if (path == "component.collision.static") return "isStatic";
    if (path == "component.particle.cpu") return "particleName";
    if (path == "component.particle.gpu") return "gpuParticleName";
    if (path == "component.lod.enabled") return "lod.enabled";
    if (path == "component.lod.levels") return "lod.levels";
    if (path == "component.meshEffect.primary") return "meshEffect1";
    if (path == "component.meshEffect.secondary") return "meshEffect2";
    if (path == "component.animation.name") return "animation.animName";
    if (path == "component.animation.loop") return "animation.isAnimLoop";
    if (path == "component.animation.controller") return "animation.animatorController";
    if (path == "component.path.name") return "recorder.recordPathName";
    if (path == "component.path.loop") return "recorder.isRecordLoop";
    if (path == "component.path.relative") return "recorder.isRecordRelative";
    if (path == "component.link.eventId") return "myEventID";
    if (path == "component.link.targetId") return "targetID";
    if (StartsWith(path, "gameplay.")) return "param." + path.substr(9);
    if (StartsWith(path, "camera.")) {
        const std::string property = path.substr(7);
        if (property == "fov") return "camera.fovY";
        if (property == "eyeObject") return "camera.eyeObjectName";
        if (property == "targetObject") return "camera.targetObjectName";
        if (property == "blendIn") return "camera.blendInDuration";
        if (property == "blendOut") return "camera.blendOutDuration";
        return "camera." + property;
    }
    return {};
}

json ResolveDefaultValue(const std::string& path) {
    if (path == "component.collision.static" ||
        path == "component.lod.enabled" ||
        path == "component.path.loop" ||
        path == "component.path.relative") {
        return false;
    }
    if (path == "component.lod.levels") return json::array();
    if (path == "component.particle.cpu" || path == "component.particle.gpu" ||
        path == "component.meshEffect.primary" || path == "component.meshEffect.secondary" ||
        path == "component.animation.name" || path == "component.animation.controller" ||
        path == "component.path.name") {
        return "";
    }
    return json();
}

EditorPropertyUiHints ResolveUiHints(const std::string& path, EditorPropertyType type) {
    EditorPropertyUiHints hints;
    hints.configured = true;

    switch (type) {
    case EditorPropertyType::Number:
        hints.speed = 0.05f;
        hints.format = "%.3f";
        break;
    case EditorPropertyType::Vector2:
    case EditorPropertyType::Vector3:
    case EditorPropertyType::Vector4:
        hints.speed = 0.05f;
        hints.format = "%.3f";
        break;
    default:
        break;
    }

    if (path == "transform.rotation" || path == "component.collision.rotation") {
        hints.displayAsDegrees = true;
        hints.speed = 0.25f;
    }
    else if (path == "camera.fov") {
        hints.useSlider = true;
        hints.displayAsDegrees = true;
        hints.minValue = 3.0f;
        hints.maxValue = 170.0f;
        hints.format = "%.1f deg";
    }
    else if (path == "rendering.metallic" || path == "rendering.roughness") {
        hints.useSlider = true;
        hints.minValue = 0.0f;
        hints.maxValue = 1.0f;
    }
    else if (path == "rendering.emissive") {
        hints.speed = 0.02f;
        hints.minValue = 0.0f;
        hints.maxValue = 100.0f;
    }
    else if (path == "rendering.color") {
        hints.useColorPicker = true;
    }
    return hints;
}

}

EditorPropertyRegistry* EditorPropertyRegistry::GetInstance() {
    static EditorPropertyRegistry instance;
    return &instance;
}

bool EditorPropertyRegistry::RegisterComponent(EditorComponentDescriptor descriptor) {
    if (descriptor.typeId.empty() || descriptor.displayName.empty() ||
        componentIndices_.find(descriptor.typeId) != componentIndices_.end()) {
        return false;
    }
    if (descriptor.removable && !descriptor.serializedPresent) {
        const std::string typeId = descriptor.typeId;
        descriptor.serializedPresent = [typeId](const json& source) {
            return ResolveSerializedComponentPresence(source, typeId);
        };
    }
    if (descriptor.removable && !descriptor.setSerializedPresent) {
        const std::string typeId = descriptor.typeId;
        descriptor.setSerializedPresent = [typeId](json& source, bool present) {
            SetSerializedComponentPresence(source, typeId, present);
        };
    }
    componentIndices_[descriptor.typeId] = components_.size();
    components_.push_back(std::move(descriptor));
    return true;
}

bool EditorPropertyRegistry::Register(EditorPropertyDescriptor descriptor) {
    if (descriptor.path.empty() || !descriptor.getter || !descriptor.setter ||
        propertyIndices_.find(descriptor.path) != propertyIndices_.end()) {
        return false;
    }

    if (descriptor.componentTypeId.empty()) {
        descriptor.componentTypeId = ResolveComponentTypeId(descriptor.path);
    }
    if (descriptor.serializedPath.empty()) {
        descriptor.serializedPath = ResolveSerializedPath(descriptor.path);
    }
    if (descriptor.defaultValue.is_null()) {
        descriptor.defaultValue = ResolveDefaultValue(descriptor.path);
    }
    if (!descriptor.ui.configured) {
        descriptor.ui = ResolveUiHints(descriptor.path, descriptor.type);
    }
    if (!FindComponent(descriptor.componentTypeId)) {
        return false;
    }

    propertyIndices_[descriptor.path] = properties_.size();
    properties_.push_back(std::move(descriptor));
    return true;
}

const EditorComponentDescriptor* EditorPropertyRegistry::FindComponent(const std::string& typeId) const {
    const auto it = componentIndices_.find(typeId);
    return it == componentIndices_.end() ? nullptr : &components_[it->second];
}

std::vector<const EditorComponentDescriptor*> EditorPropertyRegistry::GetComponentsForObject(
    const Object3d* object) const {
    std::vector<const EditorComponentDescriptor*> result;
    if (!object) return result;

    for (const EditorComponentDescriptor& component : components_) {
        if (component.applicable && !component.applicable(*object)) continue;
        if (component.present && !component.present(*object)) continue;
        result.push_back(&component);
    }
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
        return left->displayOrder < right->displayOrder;
    });
    return result;
}

std::vector<const EditorPropertyDescriptor*> EditorPropertyRegistry::GetPropertiesForComponent(
    const std::string& typeId) const {
    std::vector<const EditorPropertyDescriptor*> result;
    for (const EditorPropertyDescriptor& property : properties_) {
        if (property.componentTypeId == typeId) result.push_back(&property);
    }
    return result;
}

bool EditorPropertyRegistry::IsComponentApplicable(
    const Object3d* object,
    const std::string& typeId) const {
    const EditorComponentDescriptor* component = FindComponent(typeId);
    return object && component && (!component->applicable || component->applicable(*object));
}

bool EditorPropertyRegistry::IsComponentPresent(
    const Object3d* object,
    const std::string& typeId) const {
    const EditorComponentDescriptor* component = FindComponent(typeId);
    if (!IsComponentApplicable(object, typeId) || !component) {
        return false;
    }
    return !component->present || component->present(*object);
}

bool EditorPropertyRegistry::IsComponentPresent(
    const json& serializedObject,
    const std::string& typeId) const {
    const EditorComponentDescriptor* component = FindComponent(typeId);
    return component && component->serializedPresent && component->serializedPresent(serializedObject);
}

bool EditorPropertyRegistry::SetComponentPresent(
    json& serializedObject,
    const std::string& typeId,
    bool present) const {
    const EditorComponentDescriptor* component = FindComponent(typeId);
    if (!component || !component->setSerializedPresent) {
        return false;
    }
    component->setSerializedPresent(serializedObject, present);
    return true;
}

bool EditorPropertyRegistry::AddComponent(Object3d* object, const std::string& typeId) const {
    const EditorComponentDescriptor* component = FindComponent(typeId);
    if (!object || !component || !component->add ||
        !IsComponentApplicable(object, typeId) || IsComponentPresent(object, typeId)) {
        return false;
    }
    return component->add(*object);
}

bool EditorPropertyRegistry::RemoveComponent(Object3d* object, const std::string& typeId) const {
    const EditorComponentDescriptor* component = FindComponent(typeId);
    if (!object || !component || !component->removable || !component->remove ||
        !IsComponentPresent(object, typeId)) {
        return false;
    }
    return component->remove(*object);
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

    const auto always = [](const Object3d&) { return true; };
    const auto never = [](const Object3d&) { return false; };
    RegisterComponent({ "SceneObject", "Scene Object", "名前、GUID、Tag、LayerなどObject自体の情報です。", 0, true, false,
        always, always, EditorComponentInspectorMode::Custom });
    RegisterComponent({ "Transform", "Transform", "位置・回転・スケールと親子Transformを管理します。", 10, true, false, always,
        [](const Object3d& object) { return object.HasBuiltInComponent(Object3d::kTransformComponentType); },
        EditorComponentInspectorMode::Custom });
    RegisterComponent({ "MeshRenderer", "Mesh Renderer", "モデル、マテリアル、影、LOD描画を管理します。", 20, false, false,
        [](const Object3d& object) { return !object.IsCameraObject(); },
        [](const Object3d& object) { return object.HasBuiltInComponent(Object3d::kMeshRendererComponentType); },
        EditorComponentInspectorMode::Custom });
    RegisterComponent({ "Collider", "Collider", "衝突形状、属性、衝突Maskを管理します。", 30, false, true,
        [](const Object3d& object) { return !object.IsCameraObject(); },
        [](const Object3d& object) {
            return object.HasComponentPresenceMarker("Collider") ||
                object.GetColliderType() != ColliderType::kNone ||
                object.GetCollisionAttribute() != 0 || object.GetCollisionMask() != 0;
        },
        EditorComponentInspectorMode::Custom,
        [](Object3d& object) {
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.type = ColliderType::kAABB;
            if (std::fabs(config.size.x) < 0.001f || std::fabs(config.size.y) < 0.001f ||
                std::fabs(config.size.z) < 0.001f) {
                config.size = { 1.0f, 1.0f, 1.0f };
            }
            object.SetColliderConfig(config);
            object.SetCollisionAttribute(CollisionAttribute::kGround);
            object.SetCollisionMask(0xFFFFFFFFu);
            object.SetStatic(true);
            object.SetComponentPresenceMarker("Collider", true);
            return true;
        },
        [](Object3d& object) {
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.type = ColliderType::kNone;
            object.SetColliderConfig(config);
            object.SetCollisionAttribute(0);
            object.SetCollisionMask(0);
            object.SetStatic(false);
            object.SetComponentPresenceMarker("Collider", false);
            return true;
        } });
    RegisterComponent({ "ParticleEmitter", "Particle Emitter", "CPU/GPU Particle AssetをObjectへ接続します。", 40, false, true, always,
        [](const Object3d& object) {
            return object.HasParticleEmitterComponent();
        },
        EditorComponentInspectorMode::Custom,
        [](Object3d& object) {
            object.EnsureParticleEmitterComponent();
            return true;
        },
        [](Object3d& object) {
            return object.RemoveParticleEmitterComponent();
        } });
    RegisterComponent({ "LOD", "LOD", "Camera距離に応じた軽量Model切替を管理します。", 50, false, true,
        [](const Object3d& object) { return !object.IsCameraObject(); },
        [](const Object3d& object) {
            return object.HasComponentPresenceMarker("LOD") ||
                object.IsLodEnabled() || object.HasLodLevels();
        },
        EditorComponentInspectorMode::Custom,
        [](Object3d& object) {
            object.SetComponentPresenceMarker("LOD", true);
            object.SetLodEnabled(true);
            if (!object.HasLodLevels()) {
                object.ReloadLodManifest();
            }
            return true;
        },
        [](Object3d& object) {
            object.SetLodEnabled(false);
            object.ClearLodLevels();
            object.SetComponentPresenceMarker("LOD", false);
            return true;
        } });
    RegisterComponent({ "MeshEffect", "Mesh Effect", "Objectに追従するMesh Effectを管理します。", 60, false, true, always,
        [](const Object3d& object) {
            return object.HasMeshEffectComponent();
        },
        EditorComponentInspectorMode::Custom,
        [](Object3d& object) {
            object.EnsureMeshEffectComponent();
            return true;
        },
        [](Object3d& object) {
            return object.RemoveMeshEffectComponent();
        } });
    RegisterComponent({ "Animator", "Animator", "AnimationとAnimator Controllerを管理します。", 70, false, true, always,
        [](const Object3d& object) {
            return object.HasComponentPresenceMarker("Animator") ||
                !object.animName_.empty() || !object.GetAnimatorControllerPath().empty();
        },
        EditorComponentInspectorMode::Custom,
        [](Object3d& object) {
            object.SetComponentPresenceMarker("Animator", true);
            return true;
        },
        [](Object3d& object) {
            object.animName_.clear();
            object.isAnimLoop_ = false;
            object.ClearAnimatorController();
            object.SetComponentPresenceMarker("Animator", false);
            return true;
        } });
    RegisterComponent({ "PathMover", "Path Mover", "Ghost Recorderで記録したPath移動を管理します。", 80, false, true, always,
        [](const Object3d& object) {
            return object.HasPathMoverComponent();
        },
        EditorComponentInspectorMode::Custom,
        [](Object3d& object) {
            object.EnsurePathMoverComponent();
            return true;
        },
        [](Object3d& object) {
            return object.RemovePathMoverComponent();
        } });
    RegisterComponent({ "GameplayLink", "Gameplay Link", "Event IDとTarget IDの接続を管理します。", 90, false, true, always,
        [](const Object3d& object) {
            return object.HasGameplayLinkComponent();
        },
        EditorComponentInspectorMode::Automatic,
        [](Object3d& object) {
            object.EnsureGameplayLinkComponent();
            return true;
        },
        [](Object3d& object) {
            return object.RemoveGameplayLinkComponent();
        } });
    RegisterComponent({ "Camera", "Camera", "Scene CameraのLens、追従、Blendを管理します。", 100, false, false,
        [](const Object3d& object) { return object.IsCameraObject(); },
        [](const Object3d& object) { return object.IsCameraObject(); },
        EditorComponentInspectorMode::Custom });
    RegisterComponent({ "Gameplay", "Gameplay", "敵、ギミック、Item固有の配置Parameterです。", 110, false, false, always,
        [](const Object3d& object) { return object.param_.has_value(); },
        EditorComponentInspectorMode::Custom });
    RegisterComponent({ "EditorState", "Editor State", "Editorだけが使用する表示・Lock状態です。", 1000, true, false,
        never, never, EditorComponentInspectorMode::Custom });

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
        "component.animation.controller", "Animator Controller", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetAnimatorControllerPath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            return object.SetAnimatorController(value.get<std::string>());
        },
    });
    Register({
        "component.path.name", "Ghost Path", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetRecordPathName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetRecordPathName(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.path.loop", "Path Loop", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsRecordLoop()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetRecordLoop(value.get<bool>());
            return true;
        },
    });
    Register({
        "component.path.relative", "Path相対座標", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsRecordRelative()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetRecordRelative(value.get<bool>());
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
