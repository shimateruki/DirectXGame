#pragma once
#include <string>

class SaveDataManager {
public:
    static SaveDataManager* GetInstance();

    void Load();
    void Save();

    // クリアタイムを記録し、ベストタイムなら更新する
    void RecordClearTime(float time);

    float GetBestTime() const { return bestTime_; }
    float GetLatestClearTime() const { return latestClearTime_; }

private:
    SaveDataManager() = default;
    ~SaveDataManager() = default;

    // 初期値はすごく遅いタイム（9999秒など）にしておく
    float bestTime_ = 9999.0f;
    float latestClearTime_ = 0.0f;
    std::string saveFilePath_ = "Resources/json/save/savedata.json";
};