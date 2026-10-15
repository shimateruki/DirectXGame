#include "GameSettingsManager.h"

#include "AudioPlayer.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {
// 保存・再生に使う音量を設定可能な範囲へ制限します。
float ClampVolume(float value) {
    // 壊れた保存値の NaN は音量計算へ流さず既定値へ戻す。
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f;
}

// マウス感度を設定可能な範囲へ制限します。
float ClampSensitivity(float value) {
    // 無効な設定でカメラ操作が止まらないよう有効範囲へ丸める。
    return std::isfinite(value) ? std::clamp(value, 0.2f, 1.0f) : 1.0f;
}

GameSettingsManager::StageSelectWeatherMode ClampStageSelectWeatherMode(int value) {
    // 未知の列挙値は自動天候へ戻して古い設定ファイルも受け入れる。
    if (value < static_cast<int>(GameSettingsManager::StageSelectWeatherMode::Auto) ||
        value > static_cast<int>(GameSettingsManager::StageSelectWeatherMode::Rain)) {
        return GameSettingsManager::StageSelectWeatherMode::Auto;
    }
    return static_cast<GameSettingsManager::StageSelectWeatherMode>(value);
}
} // namespace

GameSettingsManager* GameSettingsManager::GetInstance() {
    // 保存設定は全シーンで同じ実体を共有する。
    static GameSettingsManager instance;
    return &instance;
}

void GameSettingsManager::Initialize() {
    // 二度目以降の初期化では読み直さず、保持中の音量だけ適用する。
    if (initialized_) {
        ApplyAudioSettings();
        return;
    }

    initialized_ = true;
    Load();
}

// 音量は直ちに反映し、操作設定などは利用側が保持値を参照する。
void GameSettingsManager::Load() {
    // 保存値を項目ごとに検証し、一つ壊れていても残りの設定を維持します。
    nlohmann::json data = nlohmann::json::object();
    std::ifstream file(filePath_);
    if (file.is_open()) {
        try {
            file >> data;
        } catch (...) {
            data = nlohmann::json::object();
        }
    }

    if (!data.is_object()) {
        data = nlohmann::json::object();
    }

    const auto readFloat = [&](const char* key, float fallback) {
        const auto it = data.find(key);
        if (it == data.end() || !it->is_number()) {
            return fallback;
        }
        const float value = it->get<float>();
        return std::isfinite(value) ? value : fallback;
    };
    const auto readBool = [&](const char* key, bool fallback) {
        const auto it = data.find(key);
        return it != data.end() && it->is_boolean() ? it->get<bool>() : fallback;
    };

    bgmVolume_ = ClampVolume(readFloat("bgmVolume", bgmVolume_));
    seVolume_ = ClampVolume(readFloat("seVolume", seVolume_));
    cameraSensitivity_ = ClampSensitivity(readFloat("cameraSensitivity", cameraSensitivity_));
    cameraInvertX_ = readBool("cameraInvertX", false);
    cameraInvertY_ = readBool("cameraInvertY", false);

    const auto mode = data.find("stageSelectWeatherMode");
    if (mode != data.end() && mode->is_number_integer()) {
        const auto maxMode = static_cast<uint64_t>(StageSelectWeatherMode::Rain);
        if (mode->is_number_unsigned()) {
            const uint64_t value = mode->get<uint64_t>();
            if (value <= maxMode) {
                stageSelectWeatherMode_ = ClampStageSelectWeatherMode(static_cast<int>(value));
            }
        } else {
            const int64_t value = mode->get<int64_t>();
            if (value >= 0 && static_cast<uint64_t>(value) <= maxMode) {
                stageSelectWeatherMode_ = ClampStageSelectWeatherMode(static_cast<int>(value));
            }
        }
    }

    ApplyAudioSettings();
}

bool GameSettingsManager::Save() const {
    // 他システムの設定キーを残しつつ、廃止した表示設定だけ整理する。
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
    data["cameraInvertX"] = cameraInvertX_;
    data["cameraInvertY"] = cameraInvertY_;
    data["stageSelectWeatherMode"] = static_cast<int>(stageSelectWeatherMode_);
    // 廃止済みの表示設定は再保存時に残さない。
    data.erase("brightness");
    data.erase("resolutionIndex");
    data.erase("fullscreen");

    std::filesystem::create_directories(std::filesystem::path(filePath_).parent_path());
    std::ofstream output(filePath_);
    if (output.is_open()) {
        output << data.dump(4);
    }
    output.flush();
    return output.good();
}

void GameSettingsManager::SetBGMVolume(float volume) {
    // 音量調整は保存前でも再生中の BGM に即時適用する。
    bgmVolume_ = ClampVolume(volume);
    AudioPlayer::GetInstance()->SetBGMMasterVolume(bgmVolume_);
}

void GameSettingsManager::SetSEVolume(float volume) {
    // 音量調整は保存前でも再生中の SE に即時適用する。
    seVolume_ = ClampVolume(volume);
    AudioPlayer::GetInstance()->SetSEMasterVolume(seVolume_);
}

void GameSettingsManager::SetCameraSensitivity(float sensitivity) {
    // 操作側が参照する値を有効範囲で保持する。
    cameraSensitivity_ = ClampSensitivity(sensitivity);
}

void GameSettingsManager::SetStageSelectWeatherMode(StageSelectWeatherMode mode) {
    // UI からの設定でも未知の値を自動モードへ丸める。
    stageSelectWeatherMode_ = ClampStageSelectWeatherMode(static_cast<int>(mode));
}

void GameSettingsManager::ApplyAudioSettings() const {
    // 起動時の読込と再初期化で、保持値を音声ミキサーへ同期する。
    AudioPlayer::GetInstance()->SetBGMMasterVolume(bgmVolume_);
    AudioPlayer::GetInstance()->SetSEMasterVolume(seVolume_);
}
