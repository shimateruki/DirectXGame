#pragma once
#include <string>

class SaveDataManager {
public:
    static SaveDataManager* GetInstance();

    void Load();
    void Save();

    // 設定関連
    float GetMasterVolume() const { return masterVolume_; }
    void SetMasterVolume(float volume) { masterVolume_ = volume; }
    float GetBGMVolume() const { return bgmVolume_; }
    void SetBGMVolume(float volume) { bgmVolume_ = volume; }
    float GetSEVolume() const { return seVolume_; }
    void SetSEVolume(float volume) { seVolume_ = volume; }
    int GetCameraSensitivity() const { return cameraSensitivity_; }
    void SetCameraSensitivity(int sensitivity) { cameraSensitivity_ = sensitivity; }

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

    // 設定
    float masterVolume_ = 1.0f;
    float bgmVolume_ = 1.0f;
    float seVolume_ = 1.0f;
    int cameraSensitivity_ = 0;

    std::string saveFilePath_ = "Resources/json/save/savedata.json";
};