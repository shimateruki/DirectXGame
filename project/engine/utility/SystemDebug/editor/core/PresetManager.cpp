#include "PresetManager.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
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

    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    object->ImportFromJson(node);
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

    // 1. まずメインの統合ファイルを読み込む
    LoadPresets("Resources/json/preset/presets.json");

    bool needsMigration = false; // お引越しが必要かどうかのフラグ

    // 2. 古いファイルが残っていたら読み込む
    if (std::filesystem::exists("Resources/json/preset/EnemyPresets.json")) {
        LoadPresets("Resources/json/preset/EnemyPresets.json");
        needsMigration = true;
    }
    if (std::filesystem::exists("Resources/json/preset/GimmickPresets.json")) {
        LoadPresets("Resources/json/preset/GimmickPresets.json");
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
