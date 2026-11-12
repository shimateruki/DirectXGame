#pragma once

#include "game/system/rules/GameplayStatusManager.h"
#include "Object3D.h"
#include "json.hpp"

#include <array>
#include <exception>
#include <fstream>
#include <map>
#include <string>
#include <utility>

// 実行時に必要な単体プリセットだけを読みます。編集・移行・書き込みは行いません。
// 関連：LevelLoaderなどへ実行用プリセットを返し、Editor側の編集・保存は行わない。
// 主な入口：Load / ApplyToObject
class RuntimePresetCatalog {
  public:
    void Load() {
        presets_.clear();

        // 旧ファイルが残る場合の上書き順は PresetManager と同じにします。
        constexpr std::array<const char*, 3> kPaths = {
            "Resources/json/preset/presets.json",
            "Resources/json/preset/EnemyPresets.json",
            "Resources/json/preset/GimmickPresets.json"};
        for (const char* path : kPaths) {
            std::ifstream file(path);
            if (!file)
                continue;
            try {
                nlohmann::json root;
                file >> root;
                if (!root.is_object())
                    continue;
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

    // 名前が存在する場合だけ読込値を適用し、管理対象の共通ステータスを再適用する。保存は行わない。
    void ApplyToObject(const std::string& name, engine::Object3D* object) const {
        const auto it = presets_.find(name);
        if (!object || it == presets_.end())
            return;
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
    // シーン個体のGUIDを子まで除き、同じ定義から複数配置しても識別子が重複しないようにする。
    static void RemoveSceneGuids(nlohmann::json& node) {
        if (!node.is_object())
            return;
        node.erase("guid");
        if (!node.contains("children") || !node["children"].is_array())
            return;
        for (auto& child : node["children"])
            RemoveSceneGuids(child);
    }

    std::map<std::string, nlohmann::json> presets_;
};
