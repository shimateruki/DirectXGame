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

    // 3. お引越しが必要な場合のみ、統合セーブしてから古い家を壊す！
    if (needsMigration) {
        SaveAll(); // ★ここで全員を presets.json に書き込む！

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