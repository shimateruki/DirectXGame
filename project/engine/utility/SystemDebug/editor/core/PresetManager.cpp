#include "PresetManager.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EditorPropertyRegistry.h"
#include "EditorTransactionManager.h"
#include "EnemyFactory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::string ReadString(const json& source, const char* key, const std::string& fallback = "") {
    if (source.is_object() && source.contains(key) && source.at(key).is_string()) {
        return source.at(key).get<std::string>();
    }
    return fallback;
}

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

bool ShouldApplyGroundDefaults(Object3d* object) {
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
        modelName == "primitives/cube" || ContainsGroundKeyword(modelName);
}

void ApplyGroundDefaultsIfNeeded(Object3d* object) {
    if (!ShouldApplyGroundDefaults(object)) {
        return;
    }

    auto colliderConfig = object->GetColliderConfig();
    if (colliderConfig.type == ColliderType::kNone) {
        colliderConfig.type = ColliderType::kAABB;
        object->SetColliderConfig(colliderConfig);
    }
    if (object->GetCollisionAttribute() == 0) {
        object->SetCollisionAttribute(kGround);
    }
    if (object->GetCollisionMask() == 0) {
        object->SetCollisionMask(0xFFFFFFFFu);
    }
}

std::string ResolveEnemyType(const json& node) {
    std::string enemyType = ReadString(node, "enemyType");
    if (enemyType.empty() && node.contains("param") && node["param"].is_object()) {
        enemyType = ReadString(node["param"], "enemyType");
    }
    return enemyType;
}

std::uint64_t HashText(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char value : text) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string MakeHexId(const std::string& prefix, std::uint64_t value) {
    std::ostringstream stream;
    stream << prefix << '-' << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string MakeStableId(const std::string& prefix, const std::string& seed) {
    return MakeHexId(prefix, HashText(seed));
}

std::string MakeUniqueId(const std::string& prefix) {
    static std::atomic<std::uint64_t> counter = 0;
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return MakeHexId(prefix, ticks ^ (++counter * 0x9E3779B97F4A7C15ull));
}

json* FindSourceNodeRecursive(json& node, const std::string& sourceObjectId);
const json* FindSourceNodeRecursive(const json& node, const std::string& sourceObjectId);

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

void RemoveSceneObjectGuids(json& node) {
    if (!node.is_object()) return;
    node.erase("guid");
    if (!node.contains("children") || !node["children"].is_array()) return;
    for (json& child : node["children"]) {
        RemoveSceneObjectGuids(child);
    }
}

json BuildPresetNode(Object3d* object, std::unordered_set<const Object3d*>& visited) {
    if (!object || visited.count(object) != 0) {
        return json::object();
    }

    visited.insert(object);
    json node = object->ExportToJson();
    node.erase("guid");
    node.erase("prefabInstance");
    node.erase("prefabObjectId");
    json children = json::array();

    for (Object3d* child : object->GetChildren()) {
        json childNode = BuildPresetNode(child, visited);
        if (!childNode.empty()) {
            children.push_back(std::move(childNode));
        }
    }

    if (!children.empty()) {
        node["children"] = std::move(children);
    }
    return node;
}

json BuildPrefabNode(
    Object3d* object,
    const std::string& assetId,
    std::unordered_set<const Object3d*>& visited) {
    if (!object || visited.count(object) != 0) {
        return json::object();
    }

    visited.insert(object);
    json node = object->ExportToJson();
    node.erase("guid");
    node.erase("prefabInstance");

    const auto& instance = object->GetPrefabInstanceInfo();
    if (instance.assetId == assetId && !instance.sourceObjectId.empty()) {
        node["prefabObjectId"] = instance.sourceObjectId;
    } else {
        node["prefabObjectId"] = MakeUniqueId("object");
    }

    json children = json::array();
    for (Object3d* child : object->GetChildren()) {
        json childNode = BuildPrefabNode(child, assetId, visited);
        if (!childNode.empty()) {
            children.push_back(std::move(childNode));
        }
    }
    if (!children.empty()) {
        node["children"] = std::move(children);
    }
    return node;
}

void EnsurePrefabObjectIds(json& node, const std::string& assetId, const std::string& hierarchyPath) {
    if (!node.is_object()) {
        return;
    }
    if (!node.contains("prefabObjectId") || !node["prefabObjectId"].is_string() ||
        node["prefabObjectId"].get<std::string>().empty()) {
        node["prefabObjectId"] = MakeStableId("object", assetId + ':' + hierarchyPath);
    }
    node.erase("prefabInstance");
    // Prefab AssetはScene上の実体GUIDを所有しません。
    node.erase("guid");

    if (!node.contains("children") || !node["children"].is_array()) {
        return;
    }
    for (std::size_t index = 0; index < node["children"].size(); ++index) {
        EnsurePrefabObjectIds(node["children"][index], assetId,
            hierarchyPath + '/' + std::to_string(index));
    }
}

void EnsurePrefabVariantFields(json& asset) {
    if (!asset.contains("removedObjectIds") || !asset["removedObjectIds"].is_array()) {
        asset["removedObjectIds"] = json::array();
    }
    if (!asset.contains("addedChildren") || !asset["addedChildren"].is_array()) {
        asset["addedChildren"] = json::array();
    }
    if (!asset.contains("reparentedObjects") || !asset["reparentedObjects"].is_array()) {
        asset["reparentedObjects"] = json::array();
    }
    if (!asset.contains("nodeOverrides") || !asset["nodeOverrides"].is_object()) {
        asset["nodeOverrides"] = json::object();
    }
    if (!asset.contains("componentOverrides") || !asset["componentOverrides"].is_object()) {
        asset["componentOverrides"] = json::object();
    }
}

struct PrefabNodeLocation {
    const json* node = nullptr;
    std::string parentObjectId;
    std::size_t order = 0;
};

void CollectPrefabNodeLocations(
    const json& node,
    const std::string& parentObjectId,
    std::size_t order,
    std::unordered_map<std::string, PrefabNodeLocation>& locations) {
    if (!node.is_object()) {
        return;
    }

    const std::string objectId = node.value("prefabObjectId", "");
    if (!objectId.empty()) {
        locations[objectId] = { &node, parentObjectId, order };
    }

    if (!node.contains("children") || !node["children"].is_array()) {
        return;
    }
    for (std::size_t childIndex = 0; childIndex < node["children"].size(); ++childIndex) {
        CollectPrefabNodeLocations(node["children"][childIndex], objectId, childIndex, locations);
    }
}

int CountPrefabNodes(const json& node) {
    if (!node.is_object()) {
        return 0;
    }
    int count = 1;
    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& child : node["children"]) {
            count += CountPrefabNodes(child);
        }
    }
    return count;
}

void RemovePrefabChildrenWithKnownIds(json& node, const std::unordered_set<std::string>& knownIds) {
    if (!node.is_object() || !node.contains("children") || !node["children"].is_array()) {
        return;
    }

    auto& children = node["children"];
    for (auto it = children.begin(); it != children.end();) {
        const std::string childId = it->is_object() ? it->value("prefabObjectId", "") : "";
        if (!childId.empty() && knownIds.find(childId) != knownIds.end()) {
            it = children.erase(it);
        } else {
            RemovePrefabChildrenWithKnownIds(*it, knownIds);
            ++it;
        }
    }
    if (children.empty()) {
        node.erase("children");
    }
}

bool RemovePrefabNodeRecursive(json& root, const std::string& sourceObjectId) {
    if (!root.is_object() || !root.contains("children") || !root["children"].is_array()) {
        return false;
    }

    auto& children = root["children"];
    for (auto childIt = children.begin(); childIt != children.end(); ++childIt) {
        if (childIt->is_object() && childIt->value("prefabObjectId", "") == sourceObjectId) {
            children.erase(childIt);
            return true;
        }
        if (RemovePrefabNodeRecursive(*childIt, sourceObjectId)) {
            return true;
        }
    }
    return false;
}

bool TakePrefabNodeRecursive(json& root, const std::string& sourceObjectId, json& removedNode) {
    if (!root.is_object() || !root.contains("children") || !root["children"].is_array()) {
        return false;
    }

    auto& children = root["children"];
    for (auto childIt = children.begin(); childIt != children.end(); ++childIt) {
        if (childIt->is_object() && childIt->value("prefabObjectId", "") == sourceObjectId) {
            removedNode = std::move(*childIt);
            children.erase(childIt);
            return true;
        }
        if (TakePrefabNodeRecursive(*childIt, sourceObjectId, removedNode)) {
            return true;
        }
    }
    return false;
}

bool InsertPrefabChild(json& root, const std::string& parentObjectId, std::size_t order, json child) {
    json* parent = FindSourceNodeRecursive(root, parentObjectId);
    if (!parent) {
        return false;
    }
    if (!parent->contains("children") || !(*parent)["children"].is_array()) {
        (*parent)["children"] = json::array();
    }

    auto& children = (*parent)["children"];
    const std::size_t insertOrder = (std::min)(order, children.size());
    children.insert(children.begin() + static_cast<json::difference_type>(insertOrder), std::move(child));
    return true;
}

void ApplyJsonMergePatch(json& target, const json& patch) {
    if (!patch.is_object()) {
        target = patch;
        return;
    }
    if (!target.is_object()) {
        target = json::object();
    }

    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.value().is_null()) {
            target.erase(it.key());
        } else {
            ApplyJsonMergePatch(target[it.key()], it.value());
        }
    }
}

json BuildJsonMergePatch(const json& base, const json& edited, bool& changed) {
    if (JsonValuesEqual(base, edited)) {
        changed = false;
        return json::object();
    }
    if (!base.is_object() || !edited.is_object()) {
        changed = true;
        return edited;
    }

    json patch = json::object();
    for (auto baseIt = base.begin(); baseIt != base.end(); ++baseIt) {
        if (!edited.contains(baseIt.key())) {
            patch[baseIt.key()] = nullptr;
        }
    }
    for (auto editedIt = edited.begin(); editedIt != edited.end(); ++editedIt) {
        if (!base.contains(editedIt.key())) {
            patch[editedIt.key()] = editedIt.value();
            continue;
        }
        bool childChanged = false;
        json childPatch = BuildJsonMergePatch(base.at(editedIt.key()), editedIt.value(), childChanged);
        if (childChanged) {
            patch[editedIt.key()] = std::move(childPatch);
        }
    }
    changed = !patch.empty();
    return patch;
}

bool EraseJsonPath(json& source, const std::string& path) {
    const std::size_t separator = path.find('.');
    if (separator == std::string::npos) {
        return source.is_object() && source.erase(path) > 0;
    }
    const std::string head = path.substr(0, separator);
    if (!source.is_object() || !source.contains(head)) {
        return false;
    }
    const bool erased = EraseJsonPath(source[head], path.substr(separator + 1));
    if (erased && source.contains(head) && source[head].is_object() && source[head].empty()) {
        source.erase(head);
    }
    return erased;
}

void MigrateLegacyComponentOverrides(json& asset) {
    EnsurePrefabVariantFields(asset);
    json& nodeOverrides = asset["nodeOverrides"];
    if (!nodeOverrides.is_object()) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (auto nodeIt = nodeOverrides.begin(); nodeIt != nodeOverrides.end();) {
        json& patch = nodeIt.value();
        if (!patch.is_object() || !patch.contains("components") || !patch["components"].is_object()) {
            ++nodeIt;
            continue;
        }

        json& serializedComponents = patch["components"];
        for (const EditorComponentDescriptor& component : registry->GetComponents()) {
            if (!component.removable || !component.serializedPresent || !component.setSerializedPresent ||
                !serializedComponents.contains(component.typeId) ||
                !serializedComponents[component.typeId].is_object()) {
                continue;
            }
            json& componentPatch = serializedComponents[component.typeId];
            if (componentPatch.contains("_editorPresent") &&
                componentPatch["_editorPresent"].is_boolean()) {
                asset["componentOverrides"][nodeIt.key()][component.typeId] =
                    componentPatch["_editorPresent"];
                componentPatch.erase("_editorPresent");
            }
            if (componentPatch.empty()) {
                serializedComponents.erase(component.typeId);
            }
        }
        if (serializedComponents.empty()) {
            patch.erase("components");
        }
        if (patch.empty()) {
            nodeIt = nodeOverrides.erase(nodeIt);
        } else {
            ++nodeIt;
        }
    }
}

json NormalizePrefabAsset(const std::string& prefabName, const json& value) {
    json asset;
    if (value.is_object() && value.contains("root") && value["root"].is_object()) {
        asset = value;
    } else {
        asset["root"] = value;
    }

    asset["version"] = 3;
    std::string assetId = asset.value("assetId", "");
    if (assetId.empty()) {
        assetId = MakeStableId("prefab", prefabName);
        asset["assetId"] = assetId;
    }
    if (!asset.contains("root") || !asset["root"].is_object()) {
        asset["root"] = json::object();
    }
    if (!asset.contains("baseAssetId") || !asset["baseAssetId"].is_string()) {
        asset["baseAssetId"] = "";
    }
    if (!asset.contains("propertyOverrides") || !asset["propertyOverrides"].is_object()) {
        asset["propertyOverrides"] = json::object();
    }
    EnsurePrefabVariantFields(asset);
    MigrateLegacyComponentOverrides(asset);
    EnsurePrefabObjectIds(asset["root"], assetId, "root");
    for (std::size_t index = 0; index < asset["addedChildren"].size(); ++index) {
        json& entry = asset["addedChildren"][index];
        if (entry.is_object() && entry.contains("node") && entry["node"].is_object()) {
            EnsurePrefabObjectIds(entry["node"], assetId, "added/" + std::to_string(index));
        }
    }
    return asset;
}

struct PrefabCreationContext {
    std::string prefabName;
    std::string assetId;
    std::string instanceId;
};

void CreateObjectFromPresetNode(
    const json& node,
    Object3dCommon* common,
    Object3d* parent,
    std::vector<std::unique_ptr<Object3d>>& outObjects,
    const PrefabCreationContext* prefabContext = nullptr) {
    if (!node.is_object() || !common) {
        return;
    }

    const std::string enemyType = ResolveEnemyType(node);
    std::unique_ptr<Object3d> object;
    if (!enemyType.empty()) {
        object = EnemyFactory::GetInstance()->CreateEnemy(enemyType, common);
    }
    if (!object) {
        object = std::make_unique<Object3d>();
        object->Initialize(common);
    }
    object->ImportFromJson(node);
    // Preset/Prefabの生成物は、Asset内データとは別のScene実体です。
    object->RegeneratePersistentGuid();
    if (!enemyType.empty()) {
        object->SetClassName("Enemy");
        object->SetEnemyType(enemyType);
        if (!object->param_.has_value()) {
            object->param_.emplace();
        }
        object->param_->enemyType = enemyType;
    }
    ApplyGroundDefaultsIfNeeded(object.get());
    if (node.contains("name") && node["name"].is_string()) {
        object->SetName(node["name"].get<std::string>());
    }

    if (prefabContext) {
        Object3d::PrefabInstanceInfo info;
        info.prefabName = prefabContext->prefabName;
        info.assetId = prefabContext->assetId;
        info.instanceId = prefabContext->instanceId;
        info.sourceObjectId = node.value("prefabObjectId", "");
        info.isRoot = parent == nullptr;
        object->SetPrefabInstanceInfo(info);
    }

    Object3d* raw = object.get();
    if (parent) {
        raw->SetParent(parent);
    }
    raw->UpdateLocalMatrix();
    raw->UpdateWorldMatrix();
    outObjects.push_back(std::move(object));

    if (!node.contains("children") || !node["children"].is_array()) {
        return;
    }
    for (const auto& childNode : node["children"]) {
        CreateObjectFromPresetNode(childNode, common, raw, outObjects, prefabContext);
    }
}

json* FindSourceNodeRecursive(json& node, const std::string& sourceObjectId) {
    if (!node.is_object()) {
        return nullptr;
    }
    if (node.value("prefabObjectId", "") == sourceObjectId) {
        return &node;
    }
    if (node.contains("children") && node["children"].is_array()) {
        for (auto& child : node["children"]) {
            if (json* result = FindSourceNodeRecursive(child, sourceObjectId)) {
                return result;
            }
        }
    }
    return nullptr;
}

const json* FindSourceNodeRecursive(const json& node, const std::string& sourceObjectId) {
    if (!node.is_object()) {
        return nullptr;
    }
    if (node.value("prefabObjectId", "") == sourceObjectId) {
        return &node;
    }
    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& child : node["children"]) {
            if (const json* result = FindSourceNodeRecursive(child, sourceObjectId)) {
                return result;
            }
        }
    }
    return nullptr;
}

const char* GetSerializedPropertyKey(const std::string& propertyPath) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    if (const EditorPropertyDescriptor* descriptor = registry->Find(propertyPath)) {
        return descriptor->serializedPath.empty() ? nullptr : descriptor->serializedPath.c_str();
    }

    // Registry導入前の外部Propertyに対する移行期間用Fallbackです。
    if (propertyPath == "identity.name") return "name";
    if (propertyPath == "identity.tag") return "tag";
    if (propertyPath == "identity.layer") return "layer";
    if (propertyPath == "identity.saveCategory") return "saveCategory";
    if (propertyPath == "transform.position") return "translate";
    if (propertyPath == "transform.rotation") return "rotate";
    if (propertyPath == "transform.scale") return "scale";
    if (propertyPath == "rendering.visible") return "isVisible";
    if (propertyPath == "rendering.castShadow") return "castShadow";
    if (propertyPath == "rendering.color") return "color";
    if (propertyPath == "rendering.metallic") return "metallic";
    if (propertyPath == "rendering.roughness") return "roughness";
    if (propertyPath == "rendering.emissive") return "emissive";
    if (propertyPath == "editor.locked") return "isLocked";
    if (propertyPath == "collision.attribute") return "collisionAttribute";
    if (propertyPath == "collision.mask") return "collisionMask";
    if (propertyPath == "rendering.model") return "modelName";
    if (propertyPath == "rendering.blendMode") return "blendMode";
    if (propertyPath == "rendering.materialType") return "materialType";
    if (propertyPath == "rendering.texture") return "texturePath";
    if (propertyPath == "rendering.textureTiling") return "textureTiling";
    if (propertyPath == "rendering.autoTextureTiling") return "autoTextureTiling";
    if (propertyPath == "rendering.lighting") return "enableLighting";
    if (propertyPath == "rendering.environmentMap") return "enableEnvMap";
    if (propertyPath == "rendering.environmentIntensity") return "envIntensity";
    if (propertyPath == "rendering.normalMapEnabled") return "enableNormalMap";
    if (propertyPath == "rendering.normalMap") return "normalMapPath";
    if (propertyPath == "rendering.ormMap") return "ormMapPath";
    if (propertyPath == "component.collision.type") return "collider.type";
    if (propertyPath == "component.collision.center") return "collider.center";
    if (propertyPath == "component.collision.size") return "collider.size";
    if (propertyPath == "component.collision.rotation") return "collider.rotation";
    if (propertyPath == "component.collision.static") return "isStatic";
    if (propertyPath == "component.particle.cpu") return "particleName";
    if (propertyPath == "component.particle.gpu") return "gpuParticleName";
    if (propertyPath == "component.lod.enabled") return "lod.enabled";
    if (propertyPath == "component.lod.levels") return "lod.levels";
    if (propertyPath == "component.meshEffect.primary") return "meshEffect1";
    if (propertyPath == "component.meshEffect.secondary") return "meshEffect2";
    if (propertyPath == "component.animation.name") return "animation.animName";
    if (propertyPath == "component.animation.loop") return "animation.isAnimLoop";
    if (propertyPath == "component.animation.controller") return "animation.animatorController";
    if (propertyPath == "component.path.name") return "recorder.recordPathName";
    if (propertyPath == "component.path.loop") return "recorder.isRecordLoop";
    if (propertyPath == "component.path.relative") return "recorder.isRecordRelative";
    if (propertyPath == "component.link.eventId") return "myEventID";
    if (propertyPath == "component.link.targetId") return "targetID";
    if (propertyPath == "camera.enabled") return "camera.enabled";
    if (propertyPath == "camera.role") return "camera.role";
    if (propertyPath == "camera.fov") return "camera.fovY";
    if (propertyPath == "camera.nearClip") return "camera.nearClip";
    if (propertyPath == "camera.farClip") return "camera.farClip";
    if (propertyPath == "camera.eyeSource") return "camera.eyeSource";
    if (propertyPath == "camera.eyeObject") return "camera.eyeObjectName";
    if (propertyPath == "camera.eyeOffset") return "camera.eyeOffset";
    if (propertyPath == "camera.eyeFollowMode") return "camera.eyeFollowMode";
    if (propertyPath == "camera.eyeFollowResponse") return "camera.eyeFollowResponse";
    if (propertyPath == "camera.targetMode") return "camera.targetMode";
    if (propertyPath == "camera.targetObject") return "camera.targetObjectName";
    if (propertyPath == "camera.targetOffset") return "camera.targetOffset";
    if (propertyPath == "camera.fixedTarget") return "camera.fixedTarget";
    if (propertyPath == "camera.forwardDistance") return "camera.forwardDistance";
    if (propertyPath == "camera.targetFollowMode") return "camera.targetFollowMode";
    if (propertyPath == "camera.targetFollowResponse") return "camera.targetFollowResponse";
    if (propertyPath == "camera.blendIn") return "camera.blendInDuration";
    if (propertyPath == "camera.blendOut") return "camera.blendOutDuration";
    if (propertyPath == "camera.easing") return "camera.easing";
    if (propertyPath == "gameplay.healAmount") return "param.healAmount";
    if (propertyPath == "gameplay.interval") return "param.interval";
    if (propertyPath == "gameplay.maxCount") return "param.maxCount";
    if (propertyPath == "gameplay.detectionRange") return "param.detectionRange";
    if (propertyPath == "gameplay.shakeDuration") return "param.shakeDuration";
    if (propertyPath == "gameplay.fallDuration") return "param.fallDuration";
    if (propertyPath == "gameplay.colorType") return "param.colorType";
    if (propertyPath == "gameplay.switchMode") return "param.switchMode";
    if (propertyPath == "gameplay.actionMode") return "param.actionMode";
    if (propertyPath == "gameplay.targetScene") return "param.targetScene";
    if (propertyPath == "gameplay.moveAmount") return "param.moveAmount";
    if (propertyPath == "gameplay.moveSpeed") return "param.moveSpeed";
    if (propertyPath == "gameplay.startActive") return "param.startActive";
    if (propertyPath == "gameplay.returnOnOff") return "param.returnOnOff";
    return nullptr;
}

const char* GetCanonicalComponentPropertyKey(const std::string& propertyPath) {
    if (propertyPath == "component.particle.cpu") return "components.ParticleEmitter.cpuParticle";
    if (propertyPath == "component.particle.gpu") return "components.ParticleEmitter.gpuParticle";
    if (propertyPath == "component.meshEffect.primary") return "components.MeshEffect.primary";
    if (propertyPath == "component.meshEffect.secondary") return "components.MeshEffect.secondary";
    if (propertyPath == "component.path.name") return "components.PathMover.path";
    if (propertyPath == "component.path.loop") return "components.PathMover.loop";
    if (propertyPath == "component.path.relative") return "components.PathMover.relative";
    if (propertyPath == "component.link.eventId") return "components.GameplayLink.eventId";
    if (propertyPath == "component.link.targetId") return "components.GameplayLink.targetId";
    return nullptr;
}

const json* FindJsonPath(const json& source, const std::string& path) {
    const json* current = &source;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t separator = path.find('.', begin);
        const std::string key = path.substr(begin, separator == std::string::npos ? std::string::npos : separator - begin);
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

json* EnsureJsonPath(json& source, const std::string& path) {
    json* current = &source;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t separator = path.find('.', begin);
        const std::string key = path.substr(begin, separator == std::string::npos ? std::string::npos : separator - begin);
        if (separator == std::string::npos) {
            return &(*current)[key];
        }
        json& next = (*current)[key];
        if (!next.is_object()) {
            next = json::object();
        }
        current = &next;
        begin = separator + 1;
    }
    return nullptr;
}

json GetMissingSourceDefault(const std::string& propertyPath) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    if (const EditorPropertyDescriptor* descriptor = registry->Find(propertyPath)) {
        return descriptor->defaultValue;
    }

    // Registry未登録の旧Propertyだけを従来値で扱います。
    if (propertyPath == "component.collision.static" ||
        propertyPath == "component.lod.enabled" ||
        propertyPath == "component.path.loop" ||
        propertyPath == "component.path.relative") {
        return false;
    }
    if (propertyPath == "component.lod.levels") {
        return json::array();
    }
    if (propertyPath == "component.particle.cpu" ||
        propertyPath == "component.particle.gpu" ||
        propertyPath == "component.meshEffect.primary" ||
        propertyPath == "component.meshEffect.secondary" ||
        propertyPath == "component.animation.name" ||
        propertyPath == "component.animation.controller" ||
        propertyPath == "component.path.name") {
        return "";
    }
    return json();
}

json ReadSourceProperty(const json& sourceNode, const std::string& propertyPath) {
    const char* key = GetSerializedPropertyKey(propertyPath);
    if (!key) {
        return json();
    }
    const json* value = FindJsonPath(sourceNode, key);
    return value ? *value : GetMissingSourceDefault(propertyPath);
}

bool WriteSourceProperty(json& sourceNode, const std::string& propertyPath, const json& value) {
    const char* key = GetSerializedPropertyKey(propertyPath);
    if (!key) {
        return false;
    }
    json* target = EnsureJsonPath(sourceNode, key);
    if (!target) {
        return false;
    }
    *target = value;
    if (const char* canonicalKey = GetCanonicalComponentPropertyKey(propertyPath)) {
        if (json* canonicalTarget = EnsureJsonPath(sourceNode, canonicalKey)) {
            *canonicalTarget = value;
        }
    }
    if (propertyPath == "transform.rotation") {
        // Euler角のOverrideを適用した時は、古いQuaternionが優先されないようにします。
        sourceNode.erase("quaternion");
    }
    return true;
}

bool IsPrefabComponentDescriptor(const EditorComponentDescriptor& component) {
    return component.removable && component.serializedPresent && component.setSerializedPresent;
}

void RemoveComponentPropertyOverrides(
    json& asset,
    const std::string& sourceObjectId,
    const std::string& componentTypeId) {
    if (!asset.contains("propertyOverrides") || !asset["propertyOverrides"].is_object() ||
        !asset["propertyOverrides"].contains(sourceObjectId) ||
        !asset["propertyOverrides"][sourceObjectId].is_object()) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    json& objectOverrides = asset["propertyOverrides"][sourceObjectId];
    for (const EditorPropertyDescriptor* property : registry->GetPropertiesForComponent(componentTypeId)) {
        if (property) {
            objectOverrides.erase(property->path);
        }
    }
    if (objectOverrides.empty()) {
        asset["propertyOverrides"].erase(sourceObjectId);
    }
}

bool WriteComponentStateFromObject(
    json& sourceNode,
    Object3d* object,
    const EditorComponentDescriptor& component,
    bool present) {
    if (!object || !component.setSerializedPresent) {
        return false;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    if (present) {
        for (const EditorPropertyDescriptor* property : registry->GetPropertiesForComponent(component.typeId)) {
            if (!property || !HasEditorPropertyFlag(property->flags, EditorPropertyFlags::PrefabOverride) ||
                property->serializedPath.empty()) {
                continue;
            }
            const json value = registry->GetValue(object, property->path);
            if (!value.is_null()) {
                WriteSourceProperty(sourceNode, property->path, value);
            }
        }
    }
    component.setSerializedPresent(sourceNode, present);
    return true;
}

bool ApplySerializedComponentState(
    Object3d* object,
    const json& sourceNode,
    const EditorComponentDescriptor& component) {
    if (!object || !component.serializedPresent) {
        return false;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    const bool desiredPresent = component.serializedPresent(sourceNode);
    const bool currentPresent = registry->IsComponentPresent(object, component.typeId);
    if (desiredPresent != currentPresent) {
        const bool changed = desiredPresent
            ? registry->AddComponent(object, component.typeId)
            : registry->RemoveComponent(object, component.typeId);
        if (!changed) {
            return false;
        }
    }

    if (desiredPresent) {
        for (const EditorPropertyDescriptor* property : registry->GetPropertiesForComponent(component.typeId)) {
            if (!property || property->serializedPath.empty()) {
                continue;
            }
            const json value = ReadSourceProperty(sourceNode, property->path);
            if (!value.is_null()) {
                registry->SetValue(object, property->path, value);
            }
        }
    }
    return true;
}

void RemoveKnownComponentPresenceMarkers(json& node) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    for (const EditorComponentDescriptor& component : registry->GetComponents()) {
        if (IsPrefabComponentDescriptor(component)) {
            EraseJsonPath(node, "components." + component.typeId + "._editorPresent");
            EraseJsonPath(node, "components." + component.typeId + ".version");
        }
    }
}

bool IsInstancePlacementProperty(const Object3d::PrefabInstanceInfo& info, const std::string& propertyPath) {
    return info.isRoot &&
        (propertyPath == "transform.position" || propertyPath == "transform.rotation");
}

void RegisterPrefabAssetTransaction(
    PresetManager* manager,
    const std::string& prefabName,
    const json& before,
    const json& after,
    const std::string& label) {
    if (!manager || before == after) {
        return;
    }

    EditorTransaction transaction;
    transaction.label = label;
    transaction.undo = [manager, prefabName, before]() {
        manager->GetPrefab(prefabName) = before;
        manager->RefreshPrefabInheritance();
        manager->SavePrefabs();
    };
    transaction.redo = [manager, prefabName, after]() {
        manager->GetPrefab(prefabName) = after;
        manager->RefreshPrefabInheritance();
        manager->SavePrefabs();
    };
    EditorTransactionManager::GetInstance()->Register(std::move(transaction));
}

}

PresetManager* PresetManager::GetInstance() {
    static PresetManager instance;
    return &instance;
}

void PresetManager::Initialize() {
    presets_.clear();
    prefabs_.clear();

    LoadPresets("Resources/json/preset/presets.json");
    LoadPrefabs();

    bool needsMigration = false;
    if (fs::exists("Resources/json/preset/EnemyPresets.json")) {
        LoadPresets("Resources/json/preset/EnemyPresets.json");
        needsMigration = true;
    }
    if (fs::exists("Resources/json/preset/GimmickPresets.json")) {
        LoadPresets("Resources/json/preset/GimmickPresets.json");
        needsMigration = true;
    }

    if (needsMigration) {
        SaveAll();
        std::error_code error;
        fs::remove("Resources/json/preset/EnemyPresets.json", error);
        fs::remove("Resources/json/preset/GimmickPresets.json", error);
    }
}

void PresetManager::LoadPresets(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }

    json root;
    try {
        file >> root;
        if (root.is_object()) {
            for (auto& element : root.items()) {
                json preset = element.value();
                RemoveSceneObjectGuids(preset);
                presets_[element.key()] = std::move(preset);
            }
        }
    } catch (...) {
        std::cerr << "Failed to parse preset file: " << filename << std::endl;
    }
}

void PresetManager::SavePresets(const std::string& filename) {
    json root = json::object();
    for (const auto& pair : presets_) {
        json preset = pair.second;
        RemoveSceneObjectGuids(preset);
        root[pair.first] = std::move(preset);
    }

    try {
        fs::create_directories(fs::path(filename).parent_path());
        std::ofstream file(filename);
        if (file.is_open()) {
            file << root.dump(4);
        }
    } catch (...) {
    }
}

void PresetManager::AddPresetFromObject(const std::string& presetName, Object3d* obj) {
    if (!obj || presetName.empty()) {
        return;
    }
    std::unordered_set<const Object3d*> visited;
    presets_[presetName] = BuildPresetNode(obj, visited);
    SaveAll();
}

void PresetManager::ApplyPresetToObject(const std::string& presetName, Object3d* obj) {
    auto it = presets_.find(presetName);
    if (it == presets_.end() || !obj) {
        return;
    }

    try {
        json preset = it->second;
        RemoveSceneObjectGuids(preset);
        obj->ImportFromJson(preset);
    } catch (...) {
    }
}

std::vector<std::unique_ptr<Object3d>> PresetManager::CreateObjectsFromPreset(
    const std::string& presetName,
    Object3dCommon* common) const {
    std::vector<std::unique_ptr<Object3d>> objects;
    auto it = presets_.find(presetName);
    if (it == presets_.end() || !common) {
        return objects;
    }

    try {
        CreateObjectFromPresetNode(it->second, common, nullptr, objects);
    } catch (...) {
        objects.clear();
    }
    return objects;
}

void PresetManager::RemovePreset(const std::string& presetName) {
    if (presets_.erase(presetName) > 0) {
        SaveAll();
    }
}

void PresetManager::RenamePreset(const std::string& oldName, const std::string& newName) {
    auto it = presets_.find(oldName);
    if (it == presets_.end() || newName.empty() || presets_.find(newName) != presets_.end()) {
        return;
    }
    presets_[newName] = it->second;
    presets_.erase(it);
    SaveAll();
}

void PresetManager::LoadPrefabs(const std::string& filename) {
    prefabs_.clear();
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }

    json data;
    try {
        file >> data;
    } catch (...) {
        return;
    }

    const json* source = &data;
    if (data.is_object() && data.contains("prefabs") && data["prefabs"].is_object()) {
        source = &data["prefabs"];
    }
    if (!source->is_object()) {
        return;
    }

    for (auto it = source->begin(); it != source->end(); ++it) {
        prefabs_[it.key()] = NormalizePrefabAsset(it.key(), it.value());
    }
    RefreshPrefabInheritance();
}

void PresetManager::SavePrefabs(const std::string& filename) {
    RefreshPrefabInheritance();
    const fs::path path(filename);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }

    json data;
    data["version"] = 3;
    data["prefabs"] = json::object();
    for (const auto& [name, prefab] : prefabs_) {
        data["prefabs"][name] = prefab;
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << data.dump(4);
    }
}

void PresetManager::RefreshPrefabInheritance() {
    if (prefabs_.empty()) {
        return;
    }

    // Variantの親子順がMap順と一致しなくても解決できるよう、最大Asset数だけ反復します。
    for (std::size_t pass = 0; pass < prefabs_.size(); ++pass) {
        bool changed = false;
        for (auto& [name, asset] : prefabs_) {
            (void)name;
            const std::string baseAssetId = asset.value("baseAssetId", "");
            if (baseAssetId.empty()) {
                continue;
            }

            auto baseIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&baseAssetId](const auto& pair) {
                return pair.second.value("assetId", "") == baseAssetId;
            });
            if (baseIt == prefabs_.end() || !baseIt->second.contains("root") ||
                !baseIt->second["root"].is_object()) {
                continue;
            }

            json resolvedRoot = baseIt->second["root"];

            const json& removedObjectIds = asset["removedObjectIds"];
            if (removedObjectIds.is_array()) {
                for (const auto& sourceObjectId : removedObjectIds) {
                    if (sourceObjectId.is_string()) {
                        RemovePrefabNodeRecursive(resolvedRoot, sourceObjectId.get<std::string>());
                    }
                }
            }

            const json& addedChildren = asset["addedChildren"];
            if (addedChildren.is_array()) {
                std::vector<json> sortedAddedChildren(addedChildren.begin(), addedChildren.end());
                std::stable_sort(sortedAddedChildren.begin(), sortedAddedChildren.end(), [](const json& lhs, const json& rhs) {
                    const std::string lhsParent = lhs.value("parentObjectId", "");
                    const std::string rhsParent = rhs.value("parentObjectId", "");
                    if (lhsParent != rhsParent) return lhsParent < rhsParent;
                    return lhs.value("order", static_cast<std::size_t>(0)) < rhs.value("order", static_cast<std::size_t>(0));
                });
                for (auto& entry : sortedAddedChildren) {
                    if (!entry.is_object() || !entry.contains("node") || !entry["node"].is_object()) continue;
                    InsertPrefabChild(
                        resolvedRoot,
                        entry.value("parentObjectId", ""),
                        entry.value("order", static_cast<std::size_t>(0)),
                        entry["node"]);
                }
            }

            // 追加Objectを先に構築しておくことで、既存Objectを追加Objectの子へ移す差分にも対応します。
            struct PendingReparent {
                std::string sourceObjectId;
                std::string parentObjectId;
                std::size_t order = 0;
                json node;
            };
            std::vector<PendingReparent> pendingReparents;
            const json& reparentedObjects = asset["reparentedObjects"];
            if (reparentedObjects.is_array()) {
                for (const auto& entry : reparentedObjects) {
                    if (!entry.is_object()) continue;
                    PendingReparent pending;
                    pending.sourceObjectId = entry.value("sourceObjectId", "");
                    pending.parentObjectId = entry.value("parentObjectId", "");
                    pending.order = entry.value("order", static_cast<std::size_t>(0));
                    if (!pending.sourceObjectId.empty() && !pending.parentObjectId.empty() &&
                        FindSourceNodeRecursive(resolvedRoot, pending.parentObjectId) &&
                        TakePrefabNodeRecursive(resolvedRoot, pending.sourceObjectId, pending.node)) {
                        pendingReparents.push_back(std::move(pending));
                    }
                }
            }
            std::stable_sort(pendingReparents.begin(), pendingReparents.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.parentObjectId != rhs.parentObjectId) return lhs.parentObjectId < rhs.parentObjectId;
                return lhs.order < rhs.order;
            });
            for (auto& pending : pendingReparents) {
                InsertPrefabChild(resolvedRoot, pending.parentObjectId, pending.order, std::move(pending.node));
            }

            const json& nodeOverrides = asset["nodeOverrides"];
            if (nodeOverrides.is_object()) {
                for (auto nodeIt = nodeOverrides.begin(); nodeIt != nodeOverrides.end(); ++nodeIt) {
                    json* node = FindSourceNodeRecursive(resolvedRoot, nodeIt.key());
                    if (node && nodeIt.value().is_object()) {
                        ApplyJsonMergePatch(*node, nodeIt.value());
                    }
                }
            }

            const json& overrides = asset["propertyOverrides"];
            if (overrides.is_object()) {
                for (auto objectIt = overrides.begin(); objectIt != overrides.end(); ++objectIt) {
                    json* node = FindSourceNodeRecursive(resolvedRoot, objectIt.key());
                    if (!node || !objectIt.value().is_object()) {
                        continue;
                    }
                    for (auto propertyIt = objectIt.value().begin(); propertyIt != objectIt.value().end(); ++propertyIt) {
                        WriteSourceProperty(*node, propertyIt.key(), propertyIt.value());
                    }
                }
            }

            // Componentの追加・削除はProperty値とは別の構造差分として最後に反映します。
            // 削除時は基底Prefabの実行時設定も無効化されるため、値だけが残ることはありません。
            const json& componentOverrides = asset["componentOverrides"];
            if (componentOverrides.is_object()) {
                EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
                registry->InitializeBuiltInProperties();
                for (auto objectIt = componentOverrides.begin(); objectIt != componentOverrides.end(); ++objectIt) {
                    json* node = FindSourceNodeRecursive(resolvedRoot, objectIt.key());
                    if (!node || !objectIt.value().is_object()) {
                        continue;
                    }
                    for (auto componentIt = objectIt.value().begin();
                        componentIt != objectIt.value().end(); ++componentIt) {
                        if (componentIt.value().is_boolean()) {
                            registry->SetComponentPresent(
                                *node,
                                componentIt.key(),
                                componentIt.value().get<bool>());
                        }
                    }
                }
            }

            if (!asset.contains("root") || asset["root"] != resolvedRoot) {
                asset["root"] = std::move(resolvedRoot);
                changed = true;
            }
            asset["version"] = 3;
        }
        if (!changed) {
            break;
        }
    }
}

void PresetManager::AddPrefabFromObject(const std::string& prefabName, Object3d* obj) {
    if (!obj || prefabName.empty()) {
        return;
    }

    auto current = prefabs_.find(prefabName);
    if (current != prefabs_.end()) {
        UpdatePrefabFromObject(prefabName, obj);
        return;
    }

    std::string assetId;
    if (assetId.empty()) {
        assetId = MakeUniqueId("prefab");
    }

    std::unordered_set<const Object3d*> visited;
    json asset;
    asset["version"] = 3;
    asset["assetId"] = assetId;
    asset["baseAssetId"] = "";
    asset["propertyOverrides"] = json::object();
    EnsurePrefabVariantFields(asset);
    asset["root"] = BuildPrefabNode(obj, assetId, visited);
    prefabs_[prefabName] = std::move(asset);
    SavePrefabs();
}

bool PresetManager::UpdatePrefabFromObject(const std::string& prefabName, Object3d* obj) {
    auto assetIt = prefabs_.find(prefabName);
    if (assetIt == prefabs_.end() || !obj) {
        return false;
    }

    const json beforeAsset = assetIt->second;
    const std::string assetId = assetIt->second.value("assetId", "");
    if (assetId.empty()) {
        return false;
    }

    std::unordered_set<const Object3d*> visited;
    json editedRoot = BuildPrefabNode(obj, assetId, visited);
    if (!editedRoot.is_object()) {
        return false;
    }
    EnsurePrefabObjectIds(editedRoot, assetId, "root");
    EnsurePrefabVariantFields(assetIt->second);

    const std::string baseAssetId = assetIt->second.value("baseAssetId", "");
    if (baseAssetId.empty()) {
        assetIt->second["root"] = std::move(editedRoot);
        assetIt->second["propertyOverrides"] = json::object();
        assetIt->second["removedObjectIds"] = json::array();
        assetIt->second["addedChildren"] = json::array();
        assetIt->second["reparentedObjects"] = json::array();
        assetIt->second["nodeOverrides"] = json::object();
        assetIt->second["componentOverrides"] = json::object();
    } else {
        auto baseIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&baseAssetId](const auto& pair) {
            return pair.second.value("assetId", "") == baseAssetId;
        });
        if (baseIt == prefabs_.end() || !baseIt->second.contains("root") ||
            !baseIt->second["root"].is_object()) {
            return false;
        }

        const json& baseRoot = baseIt->second["root"];
        std::unordered_map<std::string, PrefabNodeLocation> baseLocations;
        std::unordered_map<std::string, PrefabNodeLocation> editedLocations;
        CollectPrefabNodeLocations(baseRoot, "", 0, baseLocations);
        CollectPrefabNodeLocations(editedRoot, "", 0, editedLocations);

        json removedObjectIds = json::array();
        for (const auto& [sourceObjectId, location] : baseLocations) {
            (void)location;
            if (editedLocations.find(sourceObjectId) == editedLocations.end()) {
                removedObjectIds.push_back(sourceObjectId);
            }
        }

        json addedChildren = json::array();
        std::unordered_set<std::string> baseObjectIds;
        for (const auto& [sourceObjectId, location] : baseLocations) {
            (void)location;
            baseObjectIds.insert(sourceObjectId);
        }
        for (const auto& [sourceObjectId, location] : editedLocations) {
            if (baseLocations.find(sourceObjectId) != baseLocations.end() || !location.node) {
                continue;
            }
            // 追加Objectの子も追加Objectなら、最上位だけを保存して重複生成を防ぎます。
            if (editedLocations.find(location.parentObjectId) != editedLocations.end() &&
                baseLocations.find(location.parentObjectId) == baseLocations.end()) {
                continue;
            }
            if (location.parentObjectId.empty()) {
                continue;
            }
            json addedNode = *location.node;
            RemovePrefabChildrenWithKnownIds(addedNode, baseObjectIds);
            addedChildren.push_back({
                { "parentObjectId", location.parentObjectId },
                { "order", location.order },
                { "node", std::move(addedNode) },
            });
        }

        json reparentedObjects = json::array();
        const std::string rootId = baseRoot.value("prefabObjectId", "");
        for (const auto& [sourceObjectId, editedLocation] : editedLocations) {
            auto baseLocationIt = baseLocations.find(sourceObjectId);
            if (baseLocationIt == baseLocations.end() || sourceObjectId == rootId) {
                continue;
            }
            const PrefabNodeLocation& baseLocation = baseLocationIt->second;
            if (editedLocation.parentObjectId != baseLocation.parentObjectId ||
                editedLocation.order != baseLocation.order) {
                reparentedObjects.push_back({
                    { "sourceObjectId", sourceObjectId },
                    { "parentObjectId", editedLocation.parentObjectId },
                    { "order", editedLocation.order },
                });
            }
        }

        json propertyOverrides = json::object();
        json nodeOverrides = json::object();
        json componentOverrides = json::object();
        EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
        registry->InitializeBuiltInProperties();
        for (const auto& [sourceObjectId, editedLocation] : editedLocations) {
            auto baseLocationIt = baseLocations.find(sourceObjectId);
            if (baseLocationIt == baseLocations.end() || !editedLocation.node || !baseLocationIt->second.node) {
                continue;
            }

            const json& baseNode = *baseLocationIt->second.node;
            const json& editedNode = *editedLocation.node;
            for (const EditorComponentDescriptor& component : registry->GetComponents()) {
                if (!IsPrefabComponentDescriptor(component)) {
                    continue;
                }
                const bool basePresent = registry->IsComponentPresent(baseNode, component.typeId);
                const bool editedPresent = registry->IsComponentPresent(editedNode, component.typeId);
                if (basePresent != editedPresent) {
                    componentOverrides[sourceObjectId][component.typeId] = editedPresent;
                }
            }
            for (const EditorPropertyDescriptor& descriptor : registry->GetProperties()) {
                if (!HasEditorPropertyFlag(descriptor.flags, EditorPropertyFlags::PrefabOverride) ||
                    !GetSerializedPropertyKey(descriptor.path)) {
                    continue;
                }
                const json baseValue = ReadSourceProperty(baseNode, descriptor.path);
                const json editedValue = ReadSourceProperty(editedNode, descriptor.path);
                if (!editedValue.is_null() && !JsonValuesEqual(baseValue, editedValue)) {
                    propertyOverrides[sourceObjectId][descriptor.path] = editedValue;
                }
            }

            json baseRaw = baseNode;
            json editedRaw = editedNode;
            for (json* raw : { &baseRaw, &editedRaw }) {
                raw->erase("children");
                raw->erase("prefabObjectId");
                raw->erase("prefabInstance");
                raw->erase("quaternion");
                RemoveKnownComponentPresenceMarkers(*raw);
                for (const EditorPropertyDescriptor& descriptor : registry->GetProperties()) {
                    const char* serializedPath = GetSerializedPropertyKey(descriptor.path);
                    if (serializedPath) {
                        EraseJsonPath(*raw, serializedPath);
                    }
                    if (const char* canonicalPath = GetCanonicalComponentPropertyKey(descriptor.path)) {
                        EraseJsonPath(*raw, canonicalPath);
                    }
                }
            }

            bool rawChanged = false;
            json rawPatch = BuildJsonMergePatch(baseRaw, editedRaw, rawChanged);
            if (rawChanged) {
                nodeOverrides[sourceObjectId] = std::move(rawPatch);
            }
        }

        assetIt->second["removedObjectIds"] = std::move(removedObjectIds);
        assetIt->second["addedChildren"] = std::move(addedChildren);
        assetIt->second["reparentedObjects"] = std::move(reparentedObjects);
        assetIt->second["propertyOverrides"] = std::move(propertyOverrides);
        assetIt->second["nodeOverrides"] = std::move(nodeOverrides);
        assetIt->second["componentOverrides"] = std::move(componentOverrides);
        // RootはRefreshPrefabInheritanceで必ず基底＋差分から再構築します。
        assetIt->second["root"] = baseRoot;
    }

    RefreshPrefabInheritance();
    const json afterAsset = assetIt->second;
    SavePrefabs();
    RegisterPrefabAssetTransaction(this, prefabName, beforeAsset, afterAsset, "Save Prefab Mode");
    return true;
}

int PresetManager::SynchronizePrefabInstances(
    const std::map<std::string, json>& beforePrefabs,
    std::vector<std::unique_ptr<Object3d>>& sceneObjects,
    Object3dCommon* common) {
    if (!common || beforePrefabs.empty() || sceneObjects.empty()) {
        return 0;
    }

    auto findAssetById = [](const auto& assets, const std::string& assetId) -> const json* {
        for (const auto& [name, asset] : assets) {
            (void)name;
            if (asset.value("assetId", "") == assetId) {
                return &asset;
            }
        }
        return nullptr;
    };
    auto findAssetNameById = [](const auto& assets, const std::string& assetId) -> std::string {
        for (const auto& [name, asset] : assets) {
            if (asset.value("assetId", "") == assetId) {
                return name;
            }
        }
        return "";
    };

    std::unordered_map<std::string, std::vector<Object3d*>> instances;
    for (const auto& object : sceneObjects) {
        if (!object || !object->IsPrefabInstance()) continue;
        const auto& info = object->GetPrefabInstanceInfo();
        if (!info.instanceId.empty()) {
            instances[info.instanceId].push_back(object.get());
        }
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    int synchronizedObjects = 0;
    for (auto& [instanceId, objects] : instances) {
        if (objects.empty() || !objects.front()) continue;
        const auto rootInfo = objects.front()->GetPrefabInstanceInfo();
        const json* oldAsset = findAssetById(beforePrefabs, rootInfo.assetId);
        const json* newAsset = findAssetById(prefabs_, rootInfo.assetId);
        if (!oldAsset || !newAsset || !oldAsset->contains("root") || !newAsset->contains("root") ||
            !(*oldAsset)["root"].is_object() || !(*newAsset)["root"].is_object() ||
            (*oldAsset)["root"] == (*newAsset)["root"]) {
            continue;
        }

        const json& oldRoot = (*oldAsset)["root"];
        const json& newRoot = (*newAsset)["root"];
        std::unordered_map<std::string, PrefabNodeLocation> oldLocations;
        std::unordered_map<std::string, PrefabNodeLocation> newLocations;
        CollectPrefabNodeLocations(oldRoot, "", 0, oldLocations);
        CollectPrefabNodeLocations(newRoot, "", 0, newLocations);

        std::unordered_map<std::string, Object3d*> instanceObjects;
        for (Object3d* object : objects) {
            if (object) {
                instanceObjects[object->GetPrefabInstanceInfo().sourceObjectId] = object;
            }
        }

        // Source上で親が変わったObjectだけを同期します。Scene側で独自に親変更済みなら保持します。
        for (auto& [sourceObjectId, object] : instanceObjects) {
            auto oldIt = oldLocations.find(sourceObjectId);
            auto newIt = newLocations.find(sourceObjectId);
            if (!object || oldIt == oldLocations.end() || newIt == newLocations.end() ||
                oldIt->second.parentObjectId == newIt->second.parentObjectId) {
                continue;
            }

            std::string currentParentSourceId;
            if (Object3d* currentParent = object->GetParent(); currentParent && currentParent->IsPrefabInstance() &&
                currentParent->GetPrefabInstanceInfo().instanceId == instanceId) {
                currentParentSourceId = currentParent->GetPrefabInstanceInfo().sourceObjectId;
            }
            auto newParentIt = instanceObjects.find(newIt->second.parentObjectId);
            if (currentParentSourceId == oldIt->second.parentObjectId && newParentIt != instanceObjects.end()) {
                object->SetParent(newParentIt->second, false);
            }
        }

        // InstanceがSourceと同じ値を保っているPropertyだけ更新し、ローカルOverrideは残します。
        for (auto& [sourceObjectId, object] : instanceObjects) {
            auto oldIt = oldLocations.find(sourceObjectId);
            auto newIt = newLocations.find(sourceObjectId);
            if (!object || oldIt == oldLocations.end() || newIt == newLocations.end() ||
                !oldIt->second.node || !newIt->second.node) {
                continue;
            }

            for (const EditorPropertyDescriptor& descriptor : registry->GetProperties()) {
                if (!HasEditorPropertyFlag(descriptor.flags, EditorPropertyFlags::PrefabOverride) ||
                    descriptor.path == "identity.name" ||
                    IsInstancePlacementProperty(object->GetPrefabInstanceInfo(), descriptor.path)) {
                    continue;
                }
                const json oldValue = ReadSourceProperty(*oldIt->second.node, descriptor.path);
                const json newValue = ReadSourceProperty(*newIt->second.node, descriptor.path);
                const json instanceValue = registry->GetValue(object, descriptor.path);
                if (!oldValue.is_null() && !newValue.is_null() && !instanceValue.is_null() &&
                    JsonValuesEqual(instanceValue, oldValue) && !JsonValuesEqual(oldValue, newValue)) {
                    registry->SetValue(object, descriptor.path, newValue);
                }
            }
            ++synchronizedObjects;
        }

        // 新しく追加されたSource階層は、各Instanceにも同じInstance IDで生成します。
        std::vector<std::string> addedRootIds;
        for (const auto& [sourceObjectId, location] : newLocations) {
            if (oldLocations.find(sourceObjectId) != oldLocations.end() ||
                instanceObjects.find(sourceObjectId) != instanceObjects.end()) {
                continue;
            }
            if (newLocations.find(location.parentObjectId) != newLocations.end() &&
                oldLocations.find(location.parentObjectId) == oldLocations.end()) {
                continue;
            }
            addedRootIds.push_back(sourceObjectId);
        }
        for (const std::string& sourceObjectId : addedRootIds) {
            const auto locationIt = newLocations.find(sourceObjectId);
            if (locationIt == newLocations.end() || !locationIt->second.node) continue;
            auto parentIt = instanceObjects.find(locationIt->second.parentObjectId);
            if (parentIt == instanceObjects.end()) continue;

            PrefabCreationContext context;
            context.prefabName = findAssetNameById(prefabs_, rootInfo.assetId);
            context.assetId = rootInfo.assetId;
            context.instanceId = instanceId;
            std::vector<std::unique_ptr<Object3d>> created;
            CreateObjectFromPresetNode(*locationIt->second.node, common, parentIt->second, created, &context);
            for (auto& createdObject : created) {
                if (!createdObject) continue;
                instanceObjects[createdObject->GetPrefabInstanceInfo().sourceObjectId] = createdObject.get();
                CollisionManager::GetInstance()->AddObject(createdObject.get());
                sceneObjects.push_back(std::move(createdObject));
                ++synchronizedObjects;
            }
        }

        // 追加Objectの生成後にもう一度親同期を行い、追加Object配下への移動を反映します。
        for (auto& [sourceObjectId, object] : instanceObjects) {
            auto oldIt = oldLocations.find(sourceObjectId);
            auto newIt = newLocations.find(sourceObjectId);
            if (!object || oldIt == oldLocations.end() || newIt == newLocations.end() ||
                oldIt->second.parentObjectId == newIt->second.parentObjectId) {
                continue;
            }
            std::string currentParentSourceId;
            if (Object3d* currentParent = object->GetParent(); currentParent && currentParent->IsPrefabInstance() &&
                currentParent->GetPrefabInstanceInfo().instanceId == instanceId) {
                currentParentSourceId = currentParent->GetPrefabInstanceInfo().sourceObjectId;
            }
            auto newParentIt = instanceObjects.find(newIt->second.parentObjectId);
            if (currentParentSourceId == oldIt->second.parentObjectId && newParentIt != instanceObjects.end()) {
                object->SetParent(newParentIt->second, false);
            }
        }

        // Sourceから削除されたObjectは除去します。非Prefabの子はScene Rootへ退避します。
        std::unordered_set<Object3d*> removedObjects;
        for (const auto& [sourceObjectId, object] : instanceObjects) {
            if (object && oldLocations.find(sourceObjectId) != oldLocations.end() &&
                newLocations.find(sourceObjectId) == newLocations.end()) {
                removedObjects.insert(object);
            }
        }
        if (!removedObjects.empty()) {
            for (const auto& candidate : sceneObjects) {
                if (!candidate || removedObjects.find(candidate.get()) != removedObjects.end()) continue;
                if (candidate->GetParent() && removedObjects.find(candidate->GetParent()) != removedObjects.end()) {
                    candidate->SetParent(nullptr, true);
                }
            }
            for (Object3d* removed : removedObjects) {
                CollisionManager::GetInstance()->RemoveObject(removed);
            }
            sceneObjects.erase(std::remove_if(sceneObjects.begin(), sceneObjects.end(), [&removedObjects](const auto& object) {
                return object && removedObjects.find(object.get()) != removedObjects.end();
            }), sceneObjects.end());
            synchronizedObjects += static_cast<int>(removedObjects.size());
        }
    }

    return synchronizedObjects;
}

bool PresetManager::CreatePrefabFromPreset(
    const std::string& presetName,
    const std::string& prefabName) {
    auto presetIt = presets_.find(presetName);
    if (presetIt == presets_.end() || prefabName.empty() || prefabs_.find(prefabName) != prefabs_.end()) {
        return false;
    }

    const std::string assetId = MakeUniqueId("prefab");
    json asset;
    asset["version"] = 3;
    asset["assetId"] = assetId;
    asset["baseAssetId"] = "";
    asset["propertyOverrides"] = json::object();
    EnsurePrefabVariantFields(asset);
    asset["root"] = presetIt->second;
    EnsurePrefabObjectIds(asset["root"], assetId, "root");
    prefabs_[prefabName] = std::move(asset);
    SavePrefabs();
    return true;
}

bool PresetManager::CreatePrefabVariant(
    const std::string& basePrefabName,
    const std::string& variantName) {
    auto baseIt = prefabs_.find(basePrefabName);
    if (baseIt == prefabs_.end() || variantName.empty() || prefabs_.find(variantName) != prefabs_.end()) {
        return false;
    }

    const std::string baseAssetId = baseIt->second.value("assetId", "");
    if (baseAssetId.empty() || !baseIt->second.contains("root") || !baseIt->second["root"].is_object()) {
        return false;
    }

    // 自分自身へ戻る派生関係は作らず、既存の解決済みRootを初期表示へ使います。
    json asset;
    asset["version"] = 3;
    asset["assetId"] = MakeUniqueId("prefab");
    asset["baseAssetId"] = baseAssetId;
    asset["propertyOverrides"] = json::object();
    EnsurePrefabVariantFields(asset);
    asset["root"] = baseIt->second["root"];
    prefabs_[variantName] = std::move(asset);
    SavePrefabs();
    return true;
}

bool PresetManager::IsPrefabVariant(const std::string& prefabName) const {
    auto it = prefabs_.find(prefabName);
    return it != prefabs_.end() && !it->second.value("baseAssetId", "").empty();
}

std::string PresetManager::GetPrefabBaseName(const std::string& prefabName) const {
    auto it = prefabs_.find(prefabName);
    if (it == prefabs_.end()) {
        return "";
    }
    const std::string baseAssetId = it->second.value("baseAssetId", "");
    if (baseAssetId.empty()) {
        return "";
    }
    auto baseIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&baseAssetId](const auto& pair) {
        return pair.second.value("assetId", "") == baseAssetId;
    });
    return baseIt == prefabs_.end() ? "" : baseIt->first;
}

PresetManager::PrefabStructureOverrideSummary PresetManager::GetPrefabStructureOverrideSummary(
    const std::string& prefabName) const {
    PrefabStructureOverrideSummary summary;
    auto it = prefabs_.find(prefabName);
    if (it == prefabs_.end()) {
        return summary;
    }

    const json& asset = it->second;
    if (asset.contains("removedObjectIds") && asset["removedObjectIds"].is_array()) {
        summary.removedObjects = static_cast<int>(asset["removedObjectIds"].size());
    }
    if (asset.contains("reparentedObjects") && asset["reparentedObjects"].is_array()) {
        summary.reparentedObjects = static_cast<int>(asset["reparentedObjects"].size());
    }
    if (asset.contains("nodeOverrides") && asset["nodeOverrides"].is_object()) {
        summary.rawNodeOverrides = static_cast<int>(asset["nodeOverrides"].size());
    }
    if (asset.contains("componentOverrides") && asset["componentOverrides"].is_object()) {
        for (const auto& objectOverrides : asset["componentOverrides"]) {
            if (objectOverrides.is_object()) {
                summary.componentOverrides += static_cast<int>(objectOverrides.size());
            }
        }
    }
    if (asset.contains("addedChildren") && asset["addedChildren"].is_array()) {
        for (const auto& entry : asset["addedChildren"]) {
            if (entry.is_object() && entry.contains("node")) {
                summary.addedObjects += CountPrefabNodes(entry["node"]);
            }
        }
    }
    return summary;
}

std::vector<std::unique_ptr<Object3d>> PresetManager::CreateObjectsFromPrefab(
    const std::string& prefabName,
    Object3dCommon* common) const {
    std::vector<std::unique_ptr<Object3d>> result;
    auto it = prefabs_.find(prefabName);
    if (it == prefabs_.end() || !common) {
        return result;
    }

    const json& asset = it->second;
    if (!asset.contains("root") || !asset["root"].is_object()) {
        return result;
    }

    PrefabCreationContext context;
    context.prefabName = prefabName;
    context.assetId = asset.value("assetId", "");
    context.instanceId = MakeUniqueId("instance");
    CreateObjectFromPresetNode(asset["root"], common, nullptr, result, &context);
    return result;
}

json* PresetManager::FindPrefabSourceNode(const Object3d::PrefabInstanceInfo& info) {
    if (!info.IsLinked()) {
        return nullptr;
    }

    for (auto& [name, asset] : prefabs_) {
        if (asset.value("assetId", "") != info.assetId || !asset.contains("root")) {
            continue;
        }
        return FindSourceNodeRecursive(asset["root"], info.sourceObjectId);
    }
    return nullptr;
}

const json* PresetManager::FindPrefabSourceNode(const Object3d::PrefabInstanceInfo& info) const {
    if (!info.IsLinked()) {
        return nullptr;
    }

    for (const auto& [name, asset] : prefabs_) {
        if (asset.value("assetId", "") != info.assetId || !asset.contains("root")) {
            continue;
        }
        return FindSourceNodeRecursive(asset["root"], info.sourceObjectId);
    }
    return nullptr;
}

std::vector<PresetManager::PrefabPropertyOverride> PresetManager::GetPrefabOverrides(const Object3d* object) const {
    std::vector<PrefabPropertyOverride> overrides;
    if (!object || !object->IsPrefabInstance()) {
        return overrides;
    }

    const auto& info = object->GetPrefabInstanceInfo();
    const json* sourceNode = FindPrefabSourceNode(info);
    if (!sourceNode) {
        return overrides;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (const EditorPropertyDescriptor& property : registry->GetProperties()) {
        if (!HasEditorPropertyFlag(property.flags, EditorPropertyFlags::PrefabOverride) ||
            property.path == "identity.name" || IsInstancePlacementProperty(info, property.path)) {
            continue;
        }

        const EditorComponentDescriptor* component = registry->FindComponent(property.componentTypeId);
        if (component && IsPrefabComponentDescriptor(*component)) {
            const bool sourcePresent = registry->IsComponentPresent(*sourceNode, component->typeId);
            const bool instancePresent = registry->IsComponentPresent(object, component->typeId);
            // Component追加・削除中は、内部Propertyではなく構造差分として一括処理します。
            if (!sourcePresent || !instancePresent) {
                continue;
            }
        }

        const json sourceValue = ReadSourceProperty(*sourceNode, property.path);
        const json instanceValue = registry->GetValue(object, property.path);
        if (sourceValue.is_null() || instanceValue.is_null() || JsonValuesEqual(sourceValue, instanceValue)) {
            continue;
        }

        overrides.push_back({ property.path, property.displayName, sourceValue, instanceValue });
    }
    return overrides;
}

std::vector<PresetManager::PrefabComponentOverride> PresetManager::GetPrefabComponentOverrides(
    const Object3d* object) const {
    std::vector<PrefabComponentOverride> overrides;
    if (!object || !object->IsPrefabInstance()) {
        return overrides;
    }

    const json* sourceNode = FindPrefabSourceNode(object->GetPrefabInstanceInfo());
    if (!sourceNode) {
        return overrides;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (const EditorComponentDescriptor& component : registry->GetComponents()) {
        if (!IsPrefabComponentDescriptor(component) ||
            !registry->IsComponentApplicable(object, component.typeId)) {
            continue;
        }
        const bool sourcePresent = registry->IsComponentPresent(*sourceNode, component.typeId);
        const bool instancePresent = registry->IsComponentPresent(object, component.typeId);
        if (sourcePresent != instancePresent) {
            overrides.push_back({
                component.typeId,
                component.displayName,
                sourcePresent,
                instancePresent,
            });
        }
    }
    return overrides;
}

std::vector<PresetManager::PrefabVariantOverride> PresetManager::GetPrefabVariantOverrides(
    const Object3d* object) const {
    std::vector<PrefabVariantOverride> overrides;
    if (!object || !object->IsPrefabInstance()) {
        return overrides;
    }

    const auto& info = object->GetPrefabInstanceInfo();
    auto assetIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&info](const auto& pair) {
        return pair.second.value("assetId", "") == info.assetId;
    });
    if (assetIt == prefabs_.end()) {
        return overrides;
    }

    const std::string baseAssetId = assetIt->second.value("baseAssetId", "");
    if (baseAssetId.empty()) {
        return overrides;
    }
    auto baseIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&baseAssetId](const auto& pair) {
        return pair.second.value("assetId", "") == baseAssetId;
    });
    if (baseIt == prefabs_.end() || !baseIt->second.contains("root")) {
        return overrides;
    }

    const json& propertyOverrides = assetIt->second["propertyOverrides"];
    if (!propertyOverrides.is_object() || !propertyOverrides.contains(info.sourceObjectId) ||
        !propertyOverrides[info.sourceObjectId].is_object()) {
        return overrides;
    }

    const json* baseNode = FindSourceNodeRecursive(baseIt->second["root"], info.sourceObjectId);
    const json* variantNode = FindPrefabSourceNode(info);
    if (!baseNode || !variantNode) {
        return overrides;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (auto it = propertyOverrides[info.sourceObjectId].begin();
        it != propertyOverrides[info.sourceObjectId].end(); ++it) {
        const EditorPropertyDescriptor* property = registry->Find(it.key());
        const json& componentOverrides = assetIt->second["componentOverrides"];
        if (property && componentOverrides.is_object() &&
            componentOverrides.contains(info.sourceObjectId) &&
            componentOverrides[info.sourceObjectId].is_object() &&
            componentOverrides[info.sourceObjectId].contains(property->componentTypeId)) {
            continue;
        }
        overrides.push_back({
            it.key(),
            property ? property->displayName : it.key(),
            ReadSourceProperty(*baseNode, it.key()),
            ReadSourceProperty(*variantNode, it.key()),
        });
    }
    return overrides;
}

std::vector<PresetManager::PrefabVariantComponentOverride>
PresetManager::GetPrefabVariantComponentOverrides(const Object3d* object) const {
    std::vector<PrefabVariantComponentOverride> overrides;
    if (!object || !object->IsPrefabInstance()) {
        return overrides;
    }

    const auto& info = object->GetPrefabInstanceInfo();
    auto assetIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&info](const auto& pair) {
        return pair.second.value("assetId", "") == info.assetId;
    });
    if (assetIt == prefabs_.end()) {
        return overrides;
    }

    const std::string baseAssetId = assetIt->second.value("baseAssetId", "");
    auto baseIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&baseAssetId](const auto& pair) {
        return pair.second.value("assetId", "") == baseAssetId;
    });
    if (baseAssetId.empty() || baseIt == prefabs_.end() || !baseIt->second.contains("root")) {
        return overrides;
    }

    const json& allOverrides = assetIt->second["componentOverrides"];
    if (!allOverrides.is_object() || !allOverrides.contains(info.sourceObjectId) ||
        !allOverrides[info.sourceObjectId].is_object()) {
        return overrides;
    }

    const json* baseNode = FindSourceNodeRecursive(baseIt->second["root"], info.sourceObjectId);
    const json* variantNode = FindPrefabSourceNode(info);
    if (!baseNode || !variantNode) {
        return overrides;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (auto it = allOverrides[info.sourceObjectId].begin();
        it != allOverrides[info.sourceObjectId].end(); ++it) {
        if (!it.value().is_boolean()) {
            continue;
        }
        const EditorComponentDescriptor* component = registry->FindComponent(it.key());
        if (!component || !IsPrefabComponentDescriptor(*component)) {
            continue;
        }
        overrides.push_back({
            component->typeId,
            component->displayName,
            registry->IsComponentPresent(*baseNode, component->typeId),
            registry->IsComponentPresent(*variantNode, component->typeId),
        });
    }
    return overrides;
}

bool PresetManager::HasValidPrefabSource(const Object3d* object) const {
    return object && object->IsPrefabInstance() &&
        FindPrefabSourceNode(object->GetPrefabInstanceInfo()) != nullptr;
}

bool PresetManager::ApplyPrefabProperty(
    Object3d* object,
    const std::string& propertyPath,
    const std::vector<Object3d*>& sceneObjects) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }

    const auto info = object->GetPrefabInstanceInfo();
    if (propertyPath == "identity.name" || IsInstancePlacementProperty(info, propertyPath)) {
        return false;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    const EditorPropertyDescriptor* property = registry->Find(propertyPath);
    json* sourceNode = FindPrefabSourceNode(info);
    if (!property || !sourceNode) {
        return false;
    }

    std::string prefabName = info.prefabName;
    auto assetIt = prefabs_.find(prefabName);
    if (assetIt == prefabs_.end() || assetIt->second.value("assetId", "") != info.assetId) {
        assetIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&info](const auto& pair) {
            return pair.second.value("assetId", "") == info.assetId;
        });
        if (assetIt == prefabs_.end()) {
            return false;
        }
        prefabName = assetIt->first;
    }

    std::unordered_map<std::string, json> oldEffectiveValues;
    for (const auto& [name, asset] : prefabs_) {
        (void)name;
        if (!asset.contains("root") || !asset["root"].is_object()) continue;
        const json* node = FindSourceNodeRecursive(asset["root"], info.sourceObjectId);
        if (node) {
            oldEffectiveValues[asset.value("assetId", "")] = ReadSourceProperty(*node, propertyPath);
        }
    }

    const json beforeAsset = assetIt->second;
    const json newSourceValue = registry->GetValue(object, propertyPath);
    if (newSourceValue.is_null()) {
        return false;
    }

    const std::string baseAssetId = assetIt->second.value("baseAssetId", "");
    if (baseAssetId.empty()) {
        if (!WriteSourceProperty(*sourceNode, propertyPath, newSourceValue)) {
            return false;
        }
    } else {
        auto baseIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&baseAssetId](const auto& pair) {
            return pair.second.value("assetId", "") == baseAssetId;
        });
        const json* baseNode = baseIt != prefabs_.end() && baseIt->second.contains("root")
            ? FindSourceNodeRecursive(baseIt->second["root"], info.sourceObjectId)
            : nullptr;
        const json baseValue = baseNode ? ReadSourceProperty(*baseNode, propertyPath) : json();
        json& objectOverrides = assetIt->second["propertyOverrides"][info.sourceObjectId];
        if (!baseValue.is_null() && JsonValuesEqual(baseValue, newSourceValue)) {
            objectOverrides.erase(propertyPath);
            if (objectOverrides.empty()) {
                assetIt->second["propertyOverrides"].erase(info.sourceObjectId);
            }
        } else {
            objectOverrides[propertyPath] = newSourceValue;
        }
    }
    RefreshPrefabInheritance();

    for (Object3d* candidate : sceneObjects) {
        if (!candidate || candidate == object || !candidate->IsPrefabInstance()) {
            continue;
        }
        const auto& candidateInfo = candidate->GetPrefabInstanceInfo();
        if (candidateInfo.sourceObjectId != info.sourceObjectId || candidateInfo.instanceId == info.instanceId) {
            continue;
        }
        auto oldIt = oldEffectiveValues.find(candidateInfo.assetId);
        const json* newNode = FindPrefabSourceNode(candidateInfo);
        const json candidateNewValue = newNode ? ReadSourceProperty(*newNode, propertyPath) : json();
        if (oldIt != oldEffectiveValues.end() && !candidateNewValue.is_null() &&
            !JsonValuesEqual(oldIt->second, candidateNewValue) &&
            JsonValuesEqual(registry->GetValue(candidate, propertyPath), oldIt->second)) {
            registry->SetValue(candidate, propertyPath, candidateNewValue);
        }
    }

    SavePrefabs();
    if (!suppressPrefabAssetTransaction_) {
        RegisterPrefabAssetTransaction(this, prefabName, beforeAsset, assetIt->second, "Apply Prefab Override");
    }
    return true;
}

bool PresetManager::ApplyPrefabComponent(
    Object3d* object,
    const std::string& componentTypeId,
    const std::vector<Object3d*>& sceneObjects) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    const EditorComponentDescriptor* component = registry->FindComponent(componentTypeId);
    if (!component || !IsPrefabComponentDescriptor(*component) ||
        !registry->IsComponentApplicable(object, componentTypeId)) {
        return false;
    }

    const auto info = object->GetPrefabInstanceInfo();
    json* sourceNode = FindPrefabSourceNode(info);
    if (!sourceNode) {
        return false;
    }
    const bool sourcePresent = registry->IsComponentPresent(*sourceNode, componentTypeId);
    const bool instancePresent = registry->IsComponentPresent(object, componentTypeId);
    if (sourcePresent == instancePresent) {
        return false;
    }

    std::string prefabName = info.prefabName;
    auto assetIt = prefabs_.find(prefabName);
    if (assetIt == prefabs_.end() || assetIt->second.value("assetId", "") != info.assetId) {
        assetIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&info](const auto& pair) {
            return pair.second.value("assetId", "") == info.assetId;
        });
        if (assetIt == prefabs_.end()) {
            return false;
        }
        prefabName = assetIt->first;
    }
    MigrateLegacyComponentOverrides(assetIt->second);

    std::unordered_map<std::string, bool> oldEffectivePresence;
    for (const auto& [name, asset] : prefabs_) {
        (void)name;
        if (!asset.contains("root") || !asset["root"].is_object()) {
            continue;
        }
        const json* node = FindSourceNodeRecursive(asset["root"], info.sourceObjectId);
        if (node) {
            oldEffectivePresence[asset.value("assetId", "")] =
                registry->IsComponentPresent(*node, componentTypeId);
        }
    }

    const json beforeAsset = assetIt->second;
    const std::string baseAssetId = assetIt->second.value("baseAssetId", "");
    if (baseAssetId.empty()) {
        if (!WriteComponentStateFromObject(*sourceNode, object, *component, instancePresent)) {
            return false;
        }
    } else {
        auto baseIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&baseAssetId](const auto& pair) {
            return pair.second.value("assetId", "") == baseAssetId;
        });
        const json* baseNode = baseIt != prefabs_.end() && baseIt->second.contains("root")
            ? FindSourceNodeRecursive(baseIt->second["root"], info.sourceObjectId)
            : nullptr;
        if (!baseNode) {
            return false;
        }

        RemoveComponentPropertyOverrides(assetIt->second, info.sourceObjectId, componentTypeId);
        json& objectComponentOverrides = assetIt->second["componentOverrides"][info.sourceObjectId];
        const bool basePresent = registry->IsComponentPresent(*baseNode, componentTypeId);
        if (basePresent == instancePresent) {
            objectComponentOverrides.erase(componentTypeId);
            if (objectComponentOverrides.empty()) {
                assetIt->second["componentOverrides"].erase(info.sourceObjectId);
            }
        } else {
            objectComponentOverrides[componentTypeId] = instancePresent;
        }

        if (instancePresent) {
            for (const EditorPropertyDescriptor* property : registry->GetPropertiesForComponent(componentTypeId)) {
                if (!property || !HasEditorPropertyFlag(property->flags, EditorPropertyFlags::PrefabOverride) ||
                    property->serializedPath.empty()) {
                    continue;
                }
                const json instanceValue = registry->GetValue(object, property->path);
                const json baseValue = ReadSourceProperty(*baseNode, property->path);
                if (!instanceValue.is_null() && !JsonValuesEqual(baseValue, instanceValue)) {
                    assetIt->second["propertyOverrides"][info.sourceObjectId][property->path] = instanceValue;
                }
            }
        }
    }

    RefreshPrefabInheritance();
    for (Object3d* candidate : sceneObjects) {
        if (!candidate || candidate == object || !candidate->IsPrefabInstance()) {
            continue;
        }
        const auto& candidateInfo = candidate->GetPrefabInstanceInfo();
        if (candidateInfo.sourceObjectId != info.sourceObjectId ||
            candidateInfo.instanceId == info.instanceId) {
            continue;
        }
        const auto oldIt = oldEffectivePresence.find(candidateInfo.assetId);
        const json* newNode = FindPrefabSourceNode(candidateInfo);
        if (oldIt == oldEffectivePresence.end() || !newNode) {
            continue;
        }
        const bool newPresence = registry->IsComponentPresent(*newNode, componentTypeId);
        const bool candidatePresence = registry->IsComponentPresent(candidate, componentTypeId);
        if (newPresence != oldIt->second && candidatePresence == oldIt->second) {
            ApplySerializedComponentState(candidate, *newNode, *component);
        }
    }

    SavePrefabs();
    if (!suppressPrefabAssetTransaction_) {
        RegisterPrefabAssetTransaction(
            this,
            prefabName,
            beforeAsset,
            assetIt->second,
            "Apply Prefab Component Override");
    }
    return true;
}

bool PresetManager::RevertPrefabComponent(Object3d* object, const std::string& componentTypeId) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }

    const json* sourceNode = FindPrefabSourceNode(object->GetPrefabInstanceInfo());
    if (!sourceNode) {
        return false;
    }
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    const EditorComponentDescriptor* component = registry->FindComponent(componentTypeId);
    if (!component || !IsPrefabComponentDescriptor(*component)) {
        return false;
    }
    const bool sourcePresent = registry->IsComponentPresent(*sourceNode, componentTypeId);
    const bool instancePresent = registry->IsComponentPresent(object, componentTypeId);
    if (sourcePresent == instancePresent) {
        return false;
    }
    return ApplySerializedComponentState(object, *sourceNode, *component);
}

bool PresetManager::RevertPrefabVariantProperty(
    Object3d* object,
    const std::string& propertyPath,
    const std::vector<Object3d*>& sceneObjects) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }

    const auto info = object->GetPrefabInstanceInfo();
    auto assetIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&info](const auto& pair) {
        return pair.second.value("assetId", "") == info.assetId;
    });
    if (assetIt == prefabs_.end() || assetIt->second.value("baseAssetId", "").empty()) {
        return false;
    }

    json& allOverrides = assetIt->second["propertyOverrides"];
    if (!allOverrides.is_object() || !allOverrides.contains(info.sourceObjectId) ||
        !allOverrides[info.sourceObjectId].is_object() ||
        !allOverrides[info.sourceObjectId].contains(propertyPath)) {
        return false;
    }

    std::unordered_map<std::string, json> oldEffectiveValues;
    for (const auto& [name, asset] : prefabs_) {
        (void)name;
        if (!asset.contains("root") || !asset["root"].is_object()) continue;
        const json* node = FindSourceNodeRecursive(asset["root"], info.sourceObjectId);
        if (node) {
            oldEffectiveValues[asset.value("assetId", "")] = ReadSourceProperty(*node, propertyPath);
        }
    }

    const std::string prefabName = assetIt->first;
    const json beforeAsset = assetIt->second;
    allOverrides[info.sourceObjectId].erase(propertyPath);
    if (allOverrides[info.sourceObjectId].empty()) {
        allOverrides.erase(info.sourceObjectId);
    }
    RefreshPrefabInheritance();

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    for (Object3d* candidate : sceneObjects) {
        if (!candidate || !candidate->IsPrefabInstance() ||
            candidate->GetPrefabInstanceInfo().sourceObjectId != info.sourceObjectId) {
            continue;
        }
        const auto& candidateInfo = candidate->GetPrefabInstanceInfo();
        auto oldIt = oldEffectiveValues.find(candidateInfo.assetId);
        const json* newNode = FindPrefabSourceNode(candidateInfo);
        const json newValue = newNode ? ReadSourceProperty(*newNode, propertyPath) : json();
        if (oldIt != oldEffectiveValues.end() && !newValue.is_null() &&
            JsonValuesEqual(registry->GetValue(candidate, propertyPath), oldIt->second)) {
            registry->SetValue(candidate, propertyPath, newValue);
        }
    }

    SavePrefabs();
    RegisterPrefabAssetTransaction(this, prefabName, beforeAsset, assetIt->second,
        "Revert Prefab Variant Override");
    return true;
}

bool PresetManager::RevertPrefabVariantComponent(
    Object3d* object,
    const std::string& componentTypeId,
    const std::vector<Object3d*>& sceneObjects) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }

    const auto info = object->GetPrefabInstanceInfo();
    auto assetIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&info](const auto& pair) {
        return pair.second.value("assetId", "") == info.assetId;
    });
    if (assetIt == prefabs_.end() || assetIt->second.value("baseAssetId", "").empty()) {
        return false;
    }

    json& allOverrides = assetIt->second["componentOverrides"];
    if (!allOverrides.is_object() || !allOverrides.contains(info.sourceObjectId) ||
        !allOverrides[info.sourceObjectId].is_object() ||
        !allOverrides[info.sourceObjectId].contains(componentTypeId)) {
        return false;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    const EditorComponentDescriptor* component = registry->FindComponent(componentTypeId);
    if (!component || !IsPrefabComponentDescriptor(*component)) {
        return false;
    }

    std::unordered_map<std::string, bool> oldEffectivePresence;
    for (const auto& [name, asset] : prefabs_) {
        (void)name;
        if (!asset.contains("root") || !asset["root"].is_object()) continue;
        const json* node = FindSourceNodeRecursive(asset["root"], info.sourceObjectId);
        if (node) {
            oldEffectivePresence[asset.value("assetId", "")] =
                registry->IsComponentPresent(*node, componentTypeId);
        }
    }

    const std::string prefabName = assetIt->first;
    const json beforeAsset = assetIt->second;
    allOverrides[info.sourceObjectId].erase(componentTypeId);
    if (allOverrides[info.sourceObjectId].empty()) {
        allOverrides.erase(info.sourceObjectId);
    }
    RemoveComponentPropertyOverrides(assetIt->second, info.sourceObjectId, componentTypeId);
    RefreshPrefabInheritance();

    for (Object3d* candidate : sceneObjects) {
        if (!candidate || !candidate->IsPrefabInstance() ||
            candidate->GetPrefabInstanceInfo().sourceObjectId != info.sourceObjectId) {
            continue;
        }
        const auto& candidateInfo = candidate->GetPrefabInstanceInfo();
        const auto oldIt = oldEffectivePresence.find(candidateInfo.assetId);
        const json* newNode = FindPrefabSourceNode(candidateInfo);
        if (oldIt == oldEffectivePresence.end() || !newNode) continue;
        const bool newPresence = registry->IsComponentPresent(*newNode, componentTypeId);
        const bool currentPresence = registry->IsComponentPresent(candidate, componentTypeId);
        if (newPresence != oldIt->second && currentPresence == oldIt->second) {
            ApplySerializedComponentState(candidate, *newNode, *component);
        }
    }

    SavePrefabs();
    RegisterPrefabAssetTransaction(
        this,
        prefabName,
        beforeAsset,
        assetIt->second,
        "Revert Prefab Variant Component Override");
    return true;
}

bool PresetManager::RevertPrefabProperty(Object3d* object, const std::string& propertyPath) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }
    const auto& info = object->GetPrefabInstanceInfo();
    if (propertyPath == "identity.name" || IsInstancePlacementProperty(info, propertyPath)) {
        return false;
    }

    const json* sourceNode = FindPrefabSourceNode(info);
    if (!sourceNode) {
        return false;
    }
    const json value = ReadSourceProperty(*sourceNode, propertyPath);
    if (value.is_null()) {
        return false;
    }
    return EditorPropertyRegistry::GetInstance()->SetValue(object, propertyPath, value);
}

int PresetManager::ApplyAllPrefabOverrides(Object3d* object, const std::vector<Object3d*>& sceneObjects) {
    if (!object || !object->IsPrefabInstance()) {
        return 0;
    }
    const auto instanceInfo = object->GetPrefabInstanceInfo();
    const std::string instanceId = instanceInfo.instanceId;
    std::string prefabName = instanceInfo.prefabName;
    auto assetIt = prefabs_.find(prefabName);
    if (assetIt == prefabs_.end() || assetIt->second.value("assetId", "") != instanceInfo.assetId) {
        assetIt = std::find_if(prefabs_.begin(), prefabs_.end(), [&instanceInfo](const auto& pair) {
            return pair.second.value("assetId", "") == instanceInfo.assetId;
        });
        if (assetIt == prefabs_.end()) {
            return 0;
        }
        prefabName = assetIt->first;
    }
    const json beforeAsset = assetIt->second;

    int applied = 0;
    suppressPrefabAssetTransaction_ = true;
    for (Object3d* candidate : sceneObjects) {
        if (!candidate || !candidate->IsPrefabInstance() ||
            candidate->GetPrefabInstanceInfo().instanceId != instanceId) {
            continue;
        }
        const auto componentOverrides = GetPrefabComponentOverrides(candidate);
        for (const auto& entry : componentOverrides) {
            if (ApplyPrefabComponent(candidate, entry.componentTypeId, sceneObjects)) {
                ++applied;
            }
        }
        const auto overrides = GetPrefabOverrides(candidate);
        for (const auto& entry : overrides) {
            if (ApplyPrefabProperty(candidate, entry.propertyPath, sceneObjects)) {
                ++applied;
            }
        }
    }
    suppressPrefabAssetTransaction_ = false;
    assetIt = prefabs_.find(prefabName);
    if (assetIt != prefabs_.end()) {
        RegisterPrefabAssetTransaction(this, prefabName, beforeAsset, assetIt->second, "Apply All Prefab Overrides");
    }
    return applied;
}

int PresetManager::RevertAllPrefabOverrides(Object3d* object, const std::vector<Object3d*>& sceneObjects) {
    if (!object || !object->IsPrefabInstance()) {
        return 0;
    }
    const std::string instanceId = object->GetPrefabInstanceInfo().instanceId;
    int reverted = 0;
    for (Object3d* candidate : sceneObjects) {
        if (!candidate || !candidate->IsPrefabInstance() ||
            candidate->GetPrefabInstanceInfo().instanceId != instanceId) {
            continue;
        }
        const auto componentOverrides = GetPrefabComponentOverrides(candidate);
        for (const auto& entry : componentOverrides) {
            if (RevertPrefabComponent(candidate, entry.componentTypeId)) {
                ++reverted;
            }
        }
        const auto overrides = GetPrefabOverrides(candidate);
        for (const auto& entry : overrides) {
            if (RevertPrefabProperty(candidate, entry.propertyPath)) {
                ++reverted;
            }
        }
    }
    return reverted;
}

int PresetManager::UnpackPrefabInstance(Object3d* object, const std::vector<Object3d*>& sceneObjects) {
    if (!object || !object->IsPrefabInstance()) {
        return 0;
    }
    const std::string instanceId = object->GetPrefabInstanceInfo().instanceId;
    int unpacked = 0;
    for (Object3d* candidate : sceneObjects) {
        if (candidate && candidate->IsPrefabInstance() &&
            candidate->GetPrefabInstanceInfo().instanceId == instanceId) {
            candidate->ClearPrefabInstanceInfo();
            ++unpacked;
        }
    }
    return unpacked;
}

void PresetManager::AssignNewPrefabInstanceId(const std::vector<Object3d*>& objects) const {
    std::unordered_map<std::string, std::string> replacementIds;
    for (Object3d* object : objects) {
        if (!object || !object->IsPrefabInstance()) {
            continue;
        }
        auto info = object->GetPrefabInstanceInfo();
        auto [it, inserted] = replacementIds.emplace(info.instanceId, std::string());
        if (inserted) {
            it->second = MakeUniqueId("instance");
        }
        info.instanceId = it->second;
        object->SetPrefabInstanceInfo(info);
    }
}

void PresetManager::RemovePrefab(const std::string& prefabName) {
    if (prefabs_.erase(prefabName) > 0) {
        SavePrefabs();
    }
}

void PresetManager::RenamePrefab(const std::string& oldName, const std::string& newName) {
    auto it = prefabs_.find(oldName);
    if (it == prefabs_.end() || newName.empty() || oldName == newName ||
        prefabs_.find(newName) != prefabs_.end()) {
        return;
    }
    prefabs_[newName] = it->second;
    prefabs_.erase(it);
    SavePrefabs();
}

void PresetManager::SaveAll() {
    SavePresets("Resources/json/preset/presets.json");
    SavePrefabs();
}
