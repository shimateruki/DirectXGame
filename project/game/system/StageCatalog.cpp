#include "game/system/StageCatalog.h"

#include "json.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

bool StageCatalog::LoadFromJson(const std::string& path, std::vector<StageData>& output) {
    // 壊れた要素を除いてから一括反映し、失敗時は既存の一覧を維持します。
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    if (!root.is_object() || !root.contains("stages") || !root["stages"].is_array()) {
        return false;
    }

    auto readString = [](const nlohmann::json& value, const char* key, const std::string& fallback) {
        return value.contains(key) && value[key].is_string() ? value[key].get<std::string>() : fallback;
    };
    auto readInt = [](const nlohmann::json& value, const char* key, int fallback) {
        if (!value.contains(key) || !value[key].is_number_integer()) {
            return fallback;
        }
        const auto& field = value[key];
        if (field.is_number_unsigned()) {
            const uint64_t number = field.get<uint64_t>();
            return number <= static_cast<uint64_t>((std::numeric_limits<int>::max)()) ? static_cast<int>(number)
                                                                                : fallback;
        }
        const int64_t number = field.get<int64_t>();
        return number >= (std::numeric_limits<int>::min)() && number <= (std::numeric_limits<int>::max)()
                   ? static_cast<int>(number)
                   : fallback;
    };
    auto readBool = [](const nlohmann::json& value, const char* key, bool fallback) {
        return value.contains(key) && value[key].is_boolean() ? value[key].get<bool>() : fallback;
    };

    std::vector<StageData> loadedStages;
    std::unordered_set<std::string> usedIds;
    for (const auto& stageJson : root["stages"]) {
        if (!stageJson.is_object()) {
            continue;
        }
        StageData data;
        data.id = readString(stageJson, "id", "");
        data.name = readString(stageJson, "name", data.id);
        data.levelPath = readString(stageJson, "levelPath", "");
        data.spritePath = readString(stageJson, "spritePath", "Resources/json/sprite/stage1_sprite.json");
        data.bgmPath = readString(stageJson, "bgmPath", "");
        data.description = readString(stageJson, "description", "");
        data.unlockStageIndex = readInt(stageJson, "unlockStageIndex", -1);
        data.defaultUnlocked = readBool(stageJson, "defaultUnlocked", false);
        data.lightPath = readString(stageJson, "lightPath", "");
        data.skyboxPath = readString(stageJson, "skyboxPath", "");
        data.gateNameTexturePath = readString(stageJson, "gateNameTexturePath", "");

        if (data.id.empty() || data.levelPath.empty() || !usedIds.insert(data.id).second) {
            continue;
        }
        loadedStages.push_back(std::move(data));
    }

    if (loadedStages.empty()) {
        return false;
    }

    output = std::move(loadedStages);
    return true;
}

std::vector<StageData> StageCatalog::CreateFallback() {
    // カタログ JSON が読めない場合も既存のステージ順と解放条件を維持する。
    return {{"stage1", "Stage 1", "Resources/json/3Dobject/stage1.json", "Resources/json/sprite/stage1_sprite.json",
             "", "", -1, true},
            {"stage2", "Stage 2", "Resources/json/3Dobject/stage2.json", "Resources/json/sprite/stage2_sprite.json",
             "", "", 0, false},
            {"stage3", "Stage 3", "Resources/json/3Dobject/stage3.json", "Resources/json/sprite/stage3_sprite.json",
             "", "", 1, false},
            {"stage4", "Stage 4", "Resources/json/3Dobject/stage4.json", "Resources/json/sprite/stage4_sprite.json",
             "", "", 2, false},
            {"stage5", "Stage 5", "Resources/json/3Dobject/stage5.json", "Resources/json/sprite/stage5_sprite.json",
             "", "", 3, false}};
}
