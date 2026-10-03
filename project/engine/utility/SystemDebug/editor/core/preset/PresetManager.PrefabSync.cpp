#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.Internal.h"

using namespace PresetManagerImplementation;

// Prefab継承と既存インスタンスの同期。
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
