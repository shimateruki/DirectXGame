#pragma once

#include "GameplayStatusManager.h"
#include "Object3d.h"
#include "json.hpp"

#include <array>
#include <exception>
#include <fstream>
#include <map>
#include <string>
#include <utility>

// 実行時に必要な単体プリセットだけを読みます。編集・移行・書き込みは行いません。
class RuntimePresetCatalog {
  public:
    void Load() {
        presets_.clear();
        // 旧ファイルが残る場合の上書き順は PresetManager と同じにします。
        constexpr std::array<const char*, 3> paths = {
            "Resources/json/preset/presets.json",
            "Resources/json/preset/EnemyPresets.json",
            "Resources/json/preset/GimmickPresets.json"};
        for (const char* path : paths) {
            std::ifstream file(path);
            if (!file) continue;
            try {
                nlohmann::json root;
                file >> root;
                if (!root.is_object()) continue;
                for (auto& entry : root.items()) {
                    nlohmann::json preset = entry.value();
                    RemoveSceneGuids(preset);
                    presets_[entry.key()] = std::move(preset);
                }
            } catch (const std::exception&) {
                // 破損したファイルを実行時に書き換えず、読めた定義だけを使います。
            }
        }
    }

    void ApplyToObject(const std::string& name, engine::Object3d* object) const {
        const auto it = presets_.find(name);
        if (!object || it == presets_.end()) return;
        try {
            object->ImportFromJson(it->second);
            auto* statusManager = GameplayStatusManager::GetInstance();
            statusManager->Initialize();
            statusManager->ApplyManagedStatus(object, false);
        } catch (const std::exception&) {
            // 元のオブジェクトを残し、壊れたプリセットによるロード停止を避けます。
        }
    }

  private:
    static void RemoveSceneGuids(nlohmann::json& node) {
        if (!node.is_object()) return;
        node.erase("guid");
        if (!node.contains("children") || !node["children"].is_array()) return;
        for (auto& child : node["children"]) RemoveSceneGuids(child);
    }

    std::map<std::string, nlohmann::json> presets_;
};
