#pragma once

#include <string>

class GameSettingsManager {
public:
    static GameSettingsManager* GetInstance();

    void Initialize();
    void Load();
    void Save() const;

    float GetBGMVolume() const { return bgmVolume_; }
    float GetSEVolume() const { return seVolume_; }
    float GetCameraSensitivity() const { return cameraSensitivity_; }

    void SetBGMVolume(float volume);
    void SetSEVolume(float volume);
    void SetCameraSensitivity(float sensitivity);
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
};
