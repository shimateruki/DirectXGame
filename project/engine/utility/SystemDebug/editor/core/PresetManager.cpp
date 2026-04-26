#include "PresetManager.h"
#include <fstream>
#include <iostream>

PresetManager* PresetManager::GetInstance() {
    static PresetManager instance;
    return &instance;
}

void PresetManager::LoadPresets(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    json root;
    file >> root;

    presets_.clear();
    for (auto& element : root.items()) {
        presets_[element.key()] = element.value();
    }
}

void PresetManager::SavePresets(const std::string& filename) {
    json root;
    for (const auto& pair : presets_) {
        root[pair.first] = pair.second;
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << root.dump(4);
    }
}

void PresetManager::AddPresetFromObject(const std::string& presetName, Object3d* obj) {
    if (!obj) return;
    // オブジェクトからJSONデータを抽出して保存
    presets_[presetName] = obj->ExportToJson();
    SavePresets(); // 即保存
}

void PresetManager::ApplyPresetToObject(const std::string& presetName, Object3d* obj) {
    if (presets_.find(presetName) == presets_.end() || !obj) return;

    // JSONデータをオブジェクトに流し込む
    obj->ImportFromJson(presets_[presetName]);
}

void PresetManager::RemovePreset(const std::string& presetName) {
    if (presets_.erase(presetName)) {
        SavePresets();
    }
}

void PresetManager::RenamePreset(const std::string& oldName, const std::string& newName) {
    if (presets_.find(oldName) != presets_.end() && !newName.empty()) {
        presets_[newName] = presets_[oldName];
        presets_.erase(oldName);
        SavePresets();
    }
}