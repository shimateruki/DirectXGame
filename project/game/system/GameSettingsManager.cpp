#include "GameSettingsManager.h"

#include "AudioPlayer.h"
#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {
float ClampVolume(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float ClampSensitivity(float value) {
    return std::clamp(value, 0.2f, 1.0f);
}

GameSettingsManager::StageSelectWeatherMode ClampStageSelectWeatherMode(int value) {
    if (value < static_cast<int>(GameSettingsManager::StageSelectWeatherMode::Auto) ||
        value > static_cast<int>(GameSettingsManager::StageSelectWeatherMode::Rain)) {
        return GameSettingsManager::StageSelectWeatherMode::Auto;
    }
    return static_cast<GameSettingsManager::StageSelectWeatherMode>(value);
}
}

GameSettingsManager* GameSettingsManager::GetInstance() {
    static GameSettingsManager instance;
    return &instance;
}

void GameSettingsManager::Initialize() {
    if (initialized_) {
        ApplyAudioSettings();
        return;
    }

    initialized_ = true;
    Load();
}

void GameSettingsManager::Load() {
    nlohmann::json data;
    std::ifstream file(filePath_);
    if (file.is_open()) {
        try {
            file >> data;
        } catch (...) {
            data = nlohmann::json::object();
        }
    }

    bgmVolume_ = ClampVolume(data.value("bgmVolume", bgmVolume_));
    seVolume_ = ClampVolume(data.value("seVolume", seVolume_));
    cameraSensitivity_ = ClampSensitivity(data.value("cameraSensitivity", cameraSensitivity_));
    stageSelectWeatherMode_ = ClampStageSelectWeatherMode(data.value(
        "stageSelectWeatherMode",
        static_cast<int>(stageSelectWeatherMode_)));

    ApplyAudioSettings();
}

void GameSettingsManager::Save() const {
    nlohmann::json data;
    std::ifstream input(filePath_);
    if (input.is_open()) {
        try {
            input >> data;
        } catch (...) {
            data = nlohmann::json::object();
        }
    }

    if (!data.is_object()) {
        data = nlohmann::json::object();
    }

    data["bgmVolume"] = bgmVolume_;
    data["seVolume"] = seVolume_;
    data["cameraSensitivity"] = cameraSensitivity_;
    data["stageSelectWeatherMode"] = static_cast<int>(stageSelectWeatherMode_);
    data.erase("brightness");
    data.erase("resolutionIndex");
    data.erase("fullscreen");

    std::filesystem::create_directories(std::filesystem::path(filePath_).parent_path());
    std::ofstream output(filePath_);
    if (output.is_open()) {
        output << data.dump(4);
    }
}

void GameSettingsManager::SetBGMVolume(float volume) {
    bgmVolume_ = ClampVolume(volume);
    AudioPlayer::GetInstance()->SetBGMMasterVolume(bgmVolume_);
}

void GameSettingsManager::SetSEVolume(float volume) {
    seVolume_ = ClampVolume(volume);
    AudioPlayer::GetInstance()->SetSEMasterVolume(seVolume_);
}

void GameSettingsManager::SetCameraSensitivity(float sensitivity) {
    cameraSensitivity_ = ClampSensitivity(sensitivity);
}

void GameSettingsManager::SetStageSelectWeatherMode(StageSelectWeatherMode mode) {
    stageSelectWeatherMode_ = ClampStageSelectWeatherMode(static_cast<int>(mode));
}

void GameSettingsManager::ApplyAudioSettings() const {
    AudioPlayer::GetInstance()->SetBGMMasterVolume(bgmVolume_);
    AudioPlayer::GetInstance()->SetSEMasterVolume(seVolume_);
}
