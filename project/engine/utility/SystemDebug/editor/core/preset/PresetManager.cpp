#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.Internal.h"

using namespace PresetManagerImplementation;

PresetManager* PresetManager::GetInstance() {
    // 編集画面と配置処理が同じプリセット集合を参照する。
    static PresetManager instance;
    return &instance;
}

void PresetManager::Initialize() {
    // 現行形式を先に読み、旧ファイルが残る場合だけ統合して保存し直す。
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

    // 移行に失敗しても例外にしないが、旧ファイルの削除は保存後に試みる。
    if (needsMigration) {
        SaveAll();
        std::error_code error;
        fs::remove("Resources/json/preset/EnemyPresets.json", error);
        fs::remove("Resources/json/preset/GimmickPresets.json", error);
    }
}

void PresetManager::LoadPresets(const std::string& filename) {
    // Scene の GUID はプリセットの同一性に含めず、配置時に新しい識別子へ委ねる。
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
    // 保存時にも GUID を除き、配置元 Scene の固有 ID を持ち越さない。
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
    // 巡回集合を渡して階層を複製し、循環参照をたどり続けない。
    if (!obj || presetName.empty()) {
        return;
    }
    std::unordered_set<const Object3d*> visited;
    presets_[presetName] = BuildPresetNode(obj, visited);
    SaveAll();
}

void PresetManager::ApplyPresetToObject(const std::string& presetName, Object3d* obj) {
    // 保存元の GUID を除いて属性を反映した後、管理対象ステータスを再適用する。
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
    // 部分生成で例外が出た場合は返却配列を空にし、不完全な階層を配置させない。
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
    // 実際に登録が消えた場合だけ JSON を書き直す。
    if (presets_.erase(presetName) > 0) {
        SaveAll();
    }
}

void PresetManager::RenamePreset(const std::string& oldName, const std::string& newName) {
    // 名前衝突による上書きを防ぎ、キーだけを変更して内容を保持する。
    auto it = presets_.find(oldName);
    if (it == presets_.end() || newName.empty() || presets_.find(newName) != presets_.end()) {
        return;
    }
    presets_[newName] = it->second;
    presets_.erase(it);
    SaveAll();
}

void PresetManager::LoadPrefabs(const std::string& filename) {
    // 旧形式の直下マップと version 付きの prefabs マップを同じ形へ正規化する。
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
    // 派生元を解決した状態で version 3 形式へまとめて保存する。
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
    // 元プリセットの木に安定した Object ID を付け、後の差分管理の基準にする。
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
    // 派生元の assetId が設定されているものだけをバリアントと判定する。
    auto it = prefabs_.find(prefabName);
    return it != prefabs_.end() && !it->second.value("baseAssetId", "").empty();
}

std::string PresetManager::GetPrefabBaseName(const std::string& prefabName) const {
    // 名前は変更できるため、保存済みの assetId から現在の派生元名を探す。
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
    // 削除・付け替え・追加は構造差分、componentOverrides は成分ごとに数える。
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
    // assetId は元データへのリンクに保ち、配置ごとに別の instanceId を発行する。
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
    // 配置済み Object が記録した assetId と sourceObjectId の両方で元ノードを特定する。
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
    // const 経路でも同じ ID 対応で検索し、呼び出し元に編集権限を渡さない。
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
    // 同じ旧 instanceId を持つ複製群には同じ新 ID を割り当て、群のまとまりを保つ。
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
    // 未登録名ではディスクに触れず、削除が成立したときだけ再保存する。
    if (prefabs_.erase(prefabName) > 0) {
        SavePrefabs();
    }
}

void PresetManager::RenamePrefab(const std::string& oldName, const std::string& newName) {
    // 派生関係は assetId で維持し、表示名の変更だけを保存する。
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
    // プリセットと Prefab の両方を、各形式の保存処理に委ねる。
    SavePresets("Resources/json/preset/presets.json");
    SavePrefabs();
}
