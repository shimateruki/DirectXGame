#include "PresetManager.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

PresetManager* PresetManager::GetInstance() {
    static PresetManager instance;
    return &instance;
}

void PresetManager::Initialize() {
    presets_.clear();
    // すべてのプリセットファイルを一括で読み込む
    LoadPresets("Resources/json/preset/presets.json");
    LoadPresets("Resources/json/preset/EnemyPresets.json");
    LoadPresets("Resources/json/preset/GimmickPresets.json");
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
    json enemyRoot = json::object();
    json gimmickRoot = json::object();

    for (auto& [name, data] : presets_) {
        // 保存先の判定（型チェックを厳密に行う）
        bool isEnemy = false;
        if (data.contains("type") && data["type"].is_string()) {
            if (data["type"] == "Enemy") isEnemy = true;
        }
        
        // typeで判定できなかった場合のフォールバック
        if (!isEnemy && data.contains("param") && data["param"].is_object()) {
            if (data["param"].contains("enemyType")) isEnemy = true;
        }

        if (isEnemy) {
            enemyRoot[name] = data;
        } else {
            gimmickRoot[name] = data;
        }
    }

    auto saveFunc = [](const std::string& path, const json& root) {
        try {
            fs::create_directories(fs::path(path).parent_path());
            std::ofstream file(path);
            if (file.is_open()) {
                file << root.dump(4);
            }
        } catch (...) {}
    };

    saveFunc("Resources/json/preset/EnemyPresets.json", enemyRoot);
    saveFunc("Resources/json/preset/GimmickPresets.json", gimmickRoot);
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
    presets_[presetName] = obj->ExportToJson();
    SaveAll();
}

void PresetManager::ApplyPresetToObject(const std::string& presetName, Object3d* obj) {
    if (presetName.empty() || presets_.find(presetName) == presets_.end() || !obj) return;

    // JSONデータをオブジェクトに流し込む
    try {
        obj->ImportFromJson(presets_[presetName]);
    } catch (...) {}
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