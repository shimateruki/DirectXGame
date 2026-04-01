#include "SaveDataManager.h"
#include <fstream>
#include <filesystem>
#include "json.hpp" // プロジェクトのJSONライブラリへのパスに合わせる

using json = nlohmann::json;

SaveDataManager* SaveDataManager::GetInstance() {
    static SaveDataManager instance;
    return &instance;
}

void SaveDataManager::Load() {
    std::ifstream file(saveFilePath_);
    if (file.is_open()) {
        json j;
        file >> j;
        if (j.contains("bestTime")) bestTime_ = j["bestTime"];
        if (j.contains("latestClearTime")) latestClearTime_ = j["latestClearTime"];
        file.close();
    }
}

void SaveDataManager::Save() {
    std::filesystem::create_directories("Resources/json/save/");
    json j;
    j["bestTime"] = bestTime_;
    j["latestClearTime"] = latestClearTime_;

    std::ofstream file(saveFilePath_);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
    }
}

void SaveDataManager::RecordClearTime(float time) {
    Load(); // まず現在のデータを読み込む
    latestClearTime_ = time;

    // 初回プレイ、またはベストタイムを更新した場合
    if (time < bestTime_ || bestTime_ == 0.0f) {
        bestTime_ = time;
    }
    Save(); // JSONに書き込む！
}