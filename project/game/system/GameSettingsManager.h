#pragma once

#include <string>

// ユーザー設定ファイルから音量と操作設定を読み書きするクラス。
class GameSettingsManager {
public:
    // ステージセレクトの天候を自動、晴れ、雨に切り替える設定。
    enum class StageSelectWeatherMode {
        Auto = 0,
        Clear = 1,
        Rain = 2
    };

    static GameSettingsManager* GetInstance();

    void Initialize();
    void Load();
    void Save() const;

    float GetBGMVolume() const { return bgmVolume_; }
    float GetSEVolume() const { return seVolume_; }
    float GetCameraSensitivity() const { return cameraSensitivity_; }
    StageSelectWeatherMode GetStageSelectWeatherMode() const { return stageSelectWeatherMode_; }

    void SetBGMVolume(float volume);
    void SetSEVolume(float volume);
    void SetCameraSensitivity(float sensitivity);
    void SetStageSelectWeatherMode(StageSelectWeatherMode mode);
    void ApplyAudioSettings() const;

private:
    GameSettingsManager() = default;
    GameSettingsManager(const GameSettingsManager&) = delete;
    GameSettingsManager& operator=(const GameSettingsManager&) = delete;

    std::string filePath_ = "Resources/json/user_config.json";
    bool initialized_ = false;
    float bgmVolume_ = 0.8f;
    float seVolume_ = 0.8f;
    float cameraSensitivity_ = 1.0f;
    StageSelectWeatherMode stageSelectWeatherMode_ = StageSelectWeatherMode::Auto;
};
