#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.Internal.h"

using namespace PresetManagerImplementation;

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
        auto* statusManager = GameplayStatusManager::GetInstance();
        statusManager->Initialize();
        statusManager->ApplyManagedStatus(obj, false);
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
