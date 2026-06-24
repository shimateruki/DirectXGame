#pragma once

#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"

struct StageData {
    std::string id;
    std::string name;
    std::string levelPath;
    std::string spritePath;
    std::string bgmPath;
    std::string description;
    int unlockStageIndex = -1;
    bool defaultUnlocked = false;
};

class StageManager {
public:
    static StageManager* GetInstance() {
        static StageManager instance;
        return &instance;
    }

    void Initialize() {
        if (LoadFromJson("Resources/json/stage_select/stages.json")) {
            LoadDevelopmentStageIndex();
            return;
        }

        stages_.clear();
        stages_.push_back({ "stage1", "Stage 1", "Resources/json/3Dobject/stage1.json", "Resources/json/sprite/stage1_sprite.json", "Resources/bgm/Alarm02.mp3", "", -1, true });
        stages_.push_back({ "stage2", "Stage 2", "Resources/json/3Dobject/stage2.json", "Resources/json/sprite/stage2_sprite.json", "Resources/bgm/Alarm02.mp3", "", 0, false });
        stages_.push_back({ "stage3", "Stage 3", "Resources/json/3Dobject/stage3.json", "Resources/json/sprite/stage3_sprite.json", "Resources/bgm/Alarm02.mp3", "", 1, false });
        stages_.push_back({ "stage4", "Stage 4", "Resources/json/3Dobject/stage4.json", "Resources/json/sprite/stage4_sprite.json", "Resources/bgm/Alarm02.mp3", "", 2, false });
        stages_.push_back({ "stage5", "Stage 5", "Resources/json/3Dobject/stage5.json", "Resources/json/sprite/stage5_sprite.json", "Resources/bgm/Alarm02.mp3", "", 3, false });
        currentStageIndex_ = 0;
        LoadDevelopmentStageIndex();
    }

    bool LoadFromJson(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json root;
        try {
            file >> root;
        }
        catch (...) {
            return false;
        }

        if (!root.contains("stages") || !root["stages"].is_array()) {
            return false;
        }

        std::vector<StageData> loadedStages;
        for (const auto& stageJson : root["stages"]) {
            StageData data;
            data.id = stageJson.value("id", "");
            data.name = stageJson.value("name", data.id);
            data.levelPath = stageJson.value("levelPath", "");
            data.spritePath = stageJson.value("spritePath", "Resources/json/sprite/stage1_sprite.json");
            data.bgmPath = stageJson.value("bgmPath", "Resources/bgm/Alarm02.mp3");
            data.description = stageJson.value("description", "");
            data.unlockStageIndex = stageJson.value("unlockStageIndex", -1);
            data.defaultUnlocked = stageJson.value("defaultUnlocked", false);

            if (data.id.empty() || data.levelPath.empty()) {
                continue;
            }
            loadedStages.push_back(std::move(data));
        }

        if (loadedStages.empty()) {
            return false;
        }

        stages_ = std::move(loadedStages);
        if (currentStageIndex_ < 0 || currentStageIndex_ >= static_cast<int>(stages_.size())) {
            currentStageIndex_ = 0;
        }
        return true;
    }

    const StageData& GetCurrentStage() const { return stages_[currentStageIndex_]; }
    void SetCurrentStage(int index) {
        if (index >= 0 && index < static_cast<int>(stages_.size())) {
            currentStageIndex_ = index;
            SaveDevelopmentStageIndex();
        }
    }
    int GetCurrentStageIndex() const { return currentStageIndex_; }
    const std::vector<StageData>& GetStages() const { return stages_; }

private:
    StageManager() = default;
    ~StageManager() = default;
    StageManager(const StageManager&) = delete;
    StageManager& operator=(const StageManager&) = delete;

    void LoadDevelopmentStageIndex() {
#if defined(USE_IMGUI) && defined(NDEBUG) && !defined(DD)
        std::ifstream file(kUserConfigPath);
        if (!file.is_open()) {
            return;
        }

        try {
            nlohmann::json root;
            file >> root;
            if (!root.is_object()) {
                return;
            }

            int savedIndex = root.value("lastStageIndex", currentStageIndex_);
            if (savedIndex >= 0 && savedIndex < static_cast<int>(stages_.size())) {
                currentStageIndex_ = savedIndex;
            }
        }
        catch (...) {
        }
#endif
    }

    void SaveDevelopmentStageIndex() const {
#if defined(USE_IMGUI) && defined(NDEBUG) && !defined(DD)
        nlohmann::json root;
        {
            std::ifstream input(kUserConfigPath);
            if (input.is_open()) {
                try {
                    input >> root;
                }
                catch (...) {
                    root = nlohmann::json::object();
                }
            }
        }

        if (!root.is_object()) {
            root = nlohmann::json::object();
        }

        root["lastStageIndex"] = currentStageIndex_;
        if (currentStageIndex_ >= 0 && currentStageIndex_ < static_cast<int>(stages_.size())) {
            root["lastStageId"] = stages_[currentStageIndex_].id;
        }

        std::ofstream output(kUserConfigPath);
        if (output.is_open()) {
            output << root.dump(4);
        }
#endif
    }

    static constexpr const char* kUserConfigPath = "Resources/json/user_config.json";
    std::vector<StageData> stages_;
    int currentStageIndex_ = 0;
};
