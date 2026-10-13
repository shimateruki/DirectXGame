#include "EditorPropertyRegistry.h"

#include "Object3d.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

using json = nlohmann::json;

bool JsonValuesEqual(const json& lhs, const json& rhs) {
    // 複数選択の混在判定では、編集時の浮動小数点誤差を差分として扱わないようにします。
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

bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

const json* FindSerializedPath(const json& source, const std::string& path) {
    // Editor の Property Path ではなく、保存 JSON 内のドット区切り Path を順にたどります。
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
    // 明示的な削除印を最優先し、印のない旧 JSON は従来フィールドから存在を推測します。
    bool markerPresent = false;
    if (TryReadSerializedPresenceMarker(source, componentTypeId, markerPresent)) {
        return markerPresent;
    }

    // 旧形式の各フィールドが空かどうかを種類ごとに判定します。
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
    // Variant の削除印だけでは基底 Prefab の値が残るため、旧形式のフィールドも初期化します。
    source["components"][componentTypeId]["_editorPresent"] = present;
    if (present) {
        return;
    }

    // Component ごとに実行時に参照される値を解除します。
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
    // UI 上の Property Path の接頭辞を、Inspector の Component 区分へ変換します。
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
    if (StartsWith(path, "component.nav.")) return "NavAgent";
    if (StartsWith(path, "camera.")) return "Camera";
    if (StartsWith(path, "gameplay.")) return "Gameplay";
    return "SceneObject";
}

std::string ResolveSerializedPath(const std::string& path) {
    // 編集用 Path と保存 JSON のキー名は一致しないため、互換フィールドへ写像します。
    // transform/描画と、旧形式の component フィールドを同じ窓口から扱います。
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
    // NavAgent 以降は任意プロパティ名を保持したまま、保存先の親キーだけ変換します。
    if (StartsWith(path, "component.nav.")) {
        const std::string property = path.substr(std::string("component.nav.").size());
        return "components.NavAgent." + property;
    }
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

    // 角度と FOV は表示単位が違うため、通常の数値入力より専用ヒントを優先します。
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
    // Component と Preset の識別子重複を防いでから、Editor の一覧へ追加します。
    std::vector<std::string> presetIds;
    presetIds.reserve(descriptor.presets.size());
    for (const EditorComponentPresetDescriptor& preset : descriptor.presets) {
        if (preset.id.empty() || preset.displayName.empty() || !preset.apply ||
            std::find(presetIds.begin(), presetIds.end(), preset.id) != presetIds.end()) {
            return false;
        }
        presetIds.push_back(preset.id);
    }
    // 削除可能Componentが旧JSONにも対応できるよう、保存状態の判定・更新を補う。
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

    // 定義ごとの重複設定を減らしつつ、明示指定があればそれを優先する。
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

const EditorComponentPresetDescriptor* EditorPropertyRegistry::FindComponentPreset(
    const std::string& typeId,
    const std::string& presetId) const {
    const EditorComponentDescriptor* component = FindComponent(typeId);
    if (!component) {
        return nullptr;
    }
    const auto it = std::find_if(
        component->presets.begin(),
        component->presets.end(),
        [&presetId](const EditorComponentPresetDescriptor& preset) {
            return preset.id == presetId;
        });
    return it == component->presets.end() ? nullptr : &*it;
}

std::vector<const EditorComponentDescriptor*> EditorPropertyRegistry::GetApplicableComponentsForObject(
    const Object3d* object) const {
    std::vector<const EditorComponentDescriptor*> result;
    if (!object) {
        return result;
    }

    for (const EditorComponentDescriptor& component : components_) {
        if (!component.applicable || component.applicable(*object)) {
            result.push_back(&component);
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
        return left->displayOrder < right->displayOrder;
    });
    return result;
}

std::vector<const EditorComponentDescriptor*> EditorPropertyRegistry::GetComponentsForObject(
    const Object3d* object) const {
    std::vector<const EditorComponentDescriptor*> result;
    for (const EditorComponentDescriptor* component : GetApplicableComponentsForObject(object)) {
        if (component && (!component->present || component->present(*object))) {
            result.push_back(component);
        }
    }
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

bool EditorPropertyRegistry::ApplyComponentPreset(
    Object3d* object,
    const std::string& typeId,
    const std::string& presetId) const {
    const EditorComponentPresetDescriptor* preset = FindComponentPreset(typeId, presetId);
    if (!object || !preset || !IsComponentApplicable(object, typeId)) {
        return false;
    }
    return preset->apply(*object);
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
    // Inspector以外からの呼び出しでもReadOnlyと適用条件を守る。
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

    // 適用できない Object は比較から除き、最初の適用可能値を基準にします。
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

    // Property 登録時に所属 Component を照合するため、Component を先に登録します。
    RegisterBuiltInComponents();
    RegisterBuiltInBaseProperties();
    RegisterBuiltInRenderAndComponentProperties();
    RegisterBuiltInCameraAndGameplayProperties();
}
