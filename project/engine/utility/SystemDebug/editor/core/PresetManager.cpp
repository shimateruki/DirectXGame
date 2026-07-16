#include "PresetManager.h"
#include "EnemyFactory.h"
#include "GameplayStatusManager.h"
#include "CollisionConfig.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <unordered_set>
#include <algorithm>
#include <cctype>

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
        modelName == "primitives/cube" ||
        ContainsGroundKeyword(modelName);
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

json BuildPresetNode(Object3d* object, std::unordered_set<const Object3d*>& visited) {
    if (!object || visited.count(object) != 0) {
        return json::object();
    }

    visited.insert(object);
    json node = object->ExportToJson();
    json children = json::array();

    for (Object3d* child : object->GetChildren()) {
        json childNode = BuildPresetNode(child, visited);
        if (!childNode.empty()) {
            children.push_back(childNode);
        }
    }

    if (!children.empty()) {
        node["children"] = children;
    }
    return node;
}

void CreateObjectFromPresetNode(
    const json& node,
    Object3dCommon* common,
    Object3d* parent,
    std::vector<std::unique_ptr<Object3d>>& outObjects) {
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
    if (!enemyType.empty()) {
        object->SetClassName("Enemy");
        object->SetEnemyType(enemyType);
        if (!object->param_.has_value()) {
            object->param_.emplace();
        }
        object->param_->enemyType = enemyType;
        auto* statusManager = GameplayStatusManager::GetInstance();
        statusManager->Initialize();
        statusManager->ApplyEnemyStatus(object.get(), true);
    }
    ApplyGroundDefaultsIfNeeded(object.get());
    if (node.contains("name") && node["name"].is_string()) {
        object->SetName(node["name"].get<std::string>());
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
        CreateObjectFromPresetNode(childNode, common, raw, outObjects);
    }
}
}

PresetManager* PresetManager::GetInstance() {
    static PresetManager instance;
    return &instance;
}

void PresetManager::Initialize() {
    presets_.clear();
    prefabs_.clear();

    // 1. まずメインの統合ファイルを読み込む
    LoadPresets("Resources/json/preset/presets.json");

        LoadPrefabs();
bool needsMigration = false; // お引越しが必要かどうかのフラグ

    // 2. 古いファイルが残っていたら読み込む
    if (std::filesystem::exists("Resources/json/preset/EnemyPresets.json")) {
        LoadPresets("Resources/json/preset/EnemyPresets.json");
            LoadPrefabs();
needsMigration = true;
    }
    if (std::filesystem::exists("Resources/json/preset/GimmickPresets.json")) {
        LoadPresets("Resources/json/preset/GimmickPresets.json");
            LoadPrefabs();
needsMigration = true;
    }

    // 3. 統合セーブを実行し、移行元の旧ファイルを削除
    if (needsMigration) {
        SaveAll(); // 全プリセットを統合ファイル presets.json に書き出す

        std::filesystem::remove("Resources/json/preset/EnemyPresets.json");
        std::filesystem::remove("Resources/json/preset/GimmickPresets.json");

    }
}
void PresetManager::LoadPresets(const std::string& filename) {
    std::ifstream file(filename);
        LoadPrefabs();
if (!file.is_open()) return;

    json root;
    try {
        file >> root;
        if (root.is_object()) {
            for (auto& element : root.items()) {
                presets_[element.key()] = element.value();
            }
        }
    } catch (...) {
        std::cerr << "Failed to parse preset file: " << filename << std::endl;
    }
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
        prefabs_[it.key()] = it.value();
    }
}

void PresetManager::SavePrefabs(const std::string& filename) {
    std::filesystem::path path(filename);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    json data;
    data["version"] = 1;
    data["prefabs"] = json::object();

    for (const auto& [name, prefab] : prefabs_) {
        data["prefabs"][name] = prefab;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        return;
    }

    file << data.dump(4);
}

void PresetManager::AddPrefabFromObject(const std::string& prefabName, Object3d* obj) {
    if (!obj || prefabName.empty()) {
        return;
    }

    std::unordered_set<const Object3d*> visited;
    prefabs_[prefabName] = BuildPresetNode(obj, visited);
    SavePrefabs();
}

std::vector<std::unique_ptr<Object3d>> PresetManager::CreateObjectsFromPrefab(const std::string& prefabName, Object3dCommon* common) const {
    std::vector<std::unique_ptr<Object3d>> result;
    if (!common) {
        return result;
    }

    auto it = prefabs_.find(prefabName);
    if (it == prefabs_.end()) {
        return result;
    }

    CreateObjectFromPresetNode(it->second, common, nullptr, result);
    return result;
}

void PresetManager::RemovePrefab(const std::string& prefabName) {
    if (prefabs_.erase(prefabName) > 0) {
        SavePrefabs();
    }
}

void PresetManager::RenamePrefab(const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) {
        return;
    }

    auto it = prefabs_.find(oldName);
    if (it == prefabs_.end() || prefabs_.find(newName) != prefabs_.end()) {
        return;
    }

    prefabs_[newName] = it->second;
    prefabs_.erase(it);
    SavePrefabs();
}
void PresetManager::SaveAll() {

    SavePresets("Resources/json/preset/presets.json");
}

void PresetManager::SavePresets(const std::string& filename) {
    json root = json::object();
    for (const auto& pair : presets_) {
        root[pair.first] = pair.second;
    }

    try {
        fs::create_directories(fs::path(filename).parent_path());
        std::ofstream file(filename);
        if (file.is_open()) {
            file << root.dump(4);
        }
    } catch (...) {}
}

void PresetManager::AddPresetFromObject(const std::string& presetName, Object3d* obj) {
    if (!obj) return;
    // オブジェクトからJSONデータを抽出して保存
    std::unordered_set<const Object3d*> visited;
    presets_[presetName] = BuildPresetNode(obj, visited);
    SaveAll();
}

void PresetManager::ApplyPresetToObject(const std::string& presetName, Object3d* obj) {
    if (presetName.empty() || presets_.find(presetName) == presets_.end() || !obj) return;

    // JSONデータをオブジェクトに流し込む
    try {
        obj->ImportFromJson(presets_[presetName]);
        auto* statusManager = GameplayStatusManager::GetInstance();
        statusManager->Initialize();
        statusManager->ApplyManagedStatus(obj, false);
    } catch (...) {}
}

std::vector<std::unique_ptr<Object3d>> PresetManager::CreateObjectsFromPreset(const std::string& presetName, Object3dCommon* common) const {
    std::vector<std::unique_ptr<Object3d>> objects;
    if (presetName.empty() || !common) return objects;

    auto it = presets_.find(presetName);
    if (it == presets_.end()) return objects;

    try {
        CreateObjectFromPresetNode(it->second, common, nullptr, objects);
    } catch (...) {
        objects.clear();
    }
    return objects;
}

void PresetManager::RemovePreset(const std::string& presetName) {
    if (presets_.erase(presetName)) {
        SaveAll();
    }
}

void PresetManager::RenamePreset(const std::string& oldName, const std::string& newName) {
    if (presets_.find(oldName) != presets_.end() && !newName.empty()) {
        presets_[newName] = presets_[oldName];
        presets_.erase(oldName);
        SaveAll();
    }
}
