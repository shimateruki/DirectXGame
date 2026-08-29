#include "GameAudioSettings.h"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {
constexpr const char* kCategorySE = "SE";
constexpr const char* kCategoryBGM = "BGM";
}

GameAudioSettings* GameAudioSettings::GetInstance() {
    static GameAudioSettings instance;
    return &instance;
}

void GameAudioSettings::Initialize() {
    if (initialized_) {
        return;
    }

    initialized_ = true;
    Load();
}

void GameAudioSettings::Load() {
    entries_.clear();
    handles_.clear();

    nlohmann::json data;
    std::ifstream file(filePath_);
    if (file.is_open()) {
        try {
            file >> data;
        } catch (...) {
            data = nlohmann::json::object();
        }
    }

    if (data.is_object() && data.contains("entries") && data["entries"].is_array()) {
        for (const auto& item : data["entries"]) {
            if (!item.is_object()) {
                continue;
            }

            SoundEntry entry;
            entry.id = item.value("id", "");
            entry.displayName = item.value("displayName", entry.id);
            entry.category = item.value("category", kCategorySE);
            entry.path = item.value("path", "");
            entry.volume = ClampVolume(item.value("volume", 1.0f));
            entry.loop = item.value("loop", false);

            if (!entry.id.empty() && !entry.path.empty()) {
                entries_.push_back(entry);
            }
        }
    }

    EnsureDefaultEntries();
}

void GameAudioSettings::Save() const {
    nlohmann::json data;
    data["entries"] = nlohmann::json::array();

    for (const auto& entry : entries_) {
        data["entries"].push_back({
            { "id", entry.id },
            { "displayName", entry.displayName },
            { "category", entry.category },
            { "path", entry.path },
            { "volume", ClampVolume(entry.volume) },
            { "loop", entry.loop }
        });
    }

    std::filesystem::create_directories(std::filesystem::path(filePath_).parent_path());
    std::ofstream output(filePath_);
    if (output.is_open()) {
        output << data.dump(4);
    }
}

GameAudioSettings::SoundEntry* GameAudioSettings::FindEntry(const std::string& id) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const SoundEntry& entry) {
        return entry.id == id;
    });
    return it == entries_.end() ? nullptr : &(*it);
}

const GameAudioSettings::SoundEntry* GameAudioSettings::FindEntry(const std::string& id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const SoundEntry& entry) {
        return entry.id == id;
    });
    return it == entries_.end() ? nullptr : &(*it);
}

float GameAudioSettings::GetVolume(const std::string& id, float fallback) const {
    const SoundEntry* entry = FindEntry(id);
    return entry ? ClampVolume(entry->volume) : ClampVolume(fallback);
}

void GameAudioSettings::SetVolume(const std::string& id, float volume) {
    SoundEntry* entry = FindEntry(id);
    if (entry) {
        entry->volume = ClampVolume(volume);
    }
}

AudioPlayer::AudioHandle GameAudioSettings::LoadHandle(const SoundEntry& entry) {
    auto cached = handles_.find(entry.id);
    if (cached != handles_.end()) {
        return cached->second;
    }

    if (!std::filesystem::exists(entry.path)) {
        return AudioPlayer::kInvalidAudioHandle;
    }

    AudioPlayer::AudioHandle handle = AudioPlayer::GetInstance()->LoadSoundFile(entry.path);
    handles_[entry.id] = handle;
    return handle;
}

void GameAudioSettings::PlaySE(const std::string& id, float volumeScale, bool loopOverride) {
    Initialize();

    SoundEntry* entry = FindEntry(id);
    if (!entry) {
        return;
    }

    AudioPlayer::AudioHandle handle = LoadHandle(*entry);
    if (handle == AudioPlayer::kInvalidAudioHandle) {
        return;
    }

    AudioPlayer::GetInstance()->PlaySE(handle, entry->loop || loopOverride, ClampVolume(entry->volume * volumeScale));
}

void GameAudioSettings::PlayBGM(const std::string& id, float volumeScale, bool loopOverride) {
    Initialize();

    SoundEntry* entry = FindEntry(id);
    if (!entry) {
        return;
    }

    AudioPlayer::AudioHandle handle = LoadHandle(*entry);
    if (handle == AudioPlayer::kInvalidAudioHandle) {
        return;
    }

    AudioPlayer::GetInstance()->PlayBGM(handle, entry->loop || loopOverride, ClampVolume(entry->volume * volumeScale));
}

void GameAudioSettings::Stop(const std::string& id) {
    SoundEntry* entry = FindEntry(id);
    if (!entry) {
        return;
    }

    auto cached = handles_.find(entry->id);
    if (cached == handles_.end()) {
        return;
    }

    if (entry->category == kCategoryBGM) {
        AudioPlayer::GetInstance()->StopBGM();
    } else {
        AudioPlayer::GetInstance()->StopSe(cached->second);
    }
}

void GameAudioSettings::EnsureDefaultEntries() {
    // 新しいゲームで必要なSEは、Audio Settingsから明示的に登録します。
    AddDefaultEntry({ "sample_bgm", "確認用BGM", kCategoryBGM, "Resources/audio/Alarm02.mp3", 0.60f, true });
}

void GameAudioSettings::AddDefaultEntry(const SoundEntry& entry) {
    if (!FindEntry(entry.id)) {
        entries_.push_back(entry);
    }
}

float GameAudioSettings::ClampVolume(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}
