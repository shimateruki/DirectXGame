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
        if (j.contains("previousBestTime")) previousBestTime_ = j["previousBestTime"];
        if (j.contains("latestClearWasBest")) latestClearWasBest_ = j["latestClearWasBest"];
        if (j.contains("masterVolume")) masterVolume_ = j["masterVolume"];
        if (j.contains("bgmVolume")) bgmVolume_ = j["bgmVolume"];
        if (j.contains("seVolume")) seVolume_ = j["seVolume"];
        if (j.contains("cameraSensitivity")) cameraSensitivity_ = j["cameraSensitivity"];
        file.close();
    }
}

void SaveDataManager::Save() {
    std::filesystem::create_directories("Resources/json/save/");
    json j;
    j["bestTime"] = bestTime_;
    j["latestClearTime"] = latestClearTime_;
    j["previousBestTime"] = previousBestTime_;
    j["latestClearWasBest"] = latestClearWasBest_;
    j["masterVolume"] = masterVolume_;
    j["bgmVolume"] = bgmVolume_;
    j["seVolume"] = seVolume_;
    j["cameraSensitivity"] = cameraSensitivity_;

    std::ofstream file(saveFilePath_);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
    }
}

void SaveDataManager::RecordClearTime(float time) {
    Load(); // まず現在のデータを読み込む
    previousBestTime_ = bestTime_;
    latestClearTime_ = time;

    // 初回プレイ、またはベストタイムを更新した場合
    latestClearWasBest_ = (time < bestTime_ || bestTime_ == 0.0f);
    if (latestClearWasBest_) {
        bestTime_ = time;
    }
    Save(); // JSONに書き込む
}
