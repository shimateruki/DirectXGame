#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.Internal.h"

using namespace PresetManagerImplementation;

// Prefabの差分適用と取り消し。
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

    // 更新前の有効値をAsset別に退避し、後で手動上書きしていないInstanceだけ同期する。
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
    // 各PropertyごとのUndo履歴を抑え、階層全体のApplyを一操作として登録する。
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
