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
    AddDefaultEntry({ "cursor_move", "カーソル移動", kCategorySE, "Resources/audio/se/generated/cursor_move.wav", 0.75f, false });
    AddDefaultEntry({ "decide", "決定", kCategorySE, "Resources/audio/se/generated/decide.wav", 0.80f, false });
    AddDefaultEntry({ "cancel", "キャンセル", kCategorySE, "Resources/audio/se/generated/cancel.wav", 0.70f, false });
    AddDefaultEntry({ "coin_get", "コイン取得", kCategorySE, "Resources/audio/se/generated/coin_get.wav", 0.80f, false });
    AddDefaultEntry({ "crown_get", "王冠取得", kCategorySE, "Resources/audio/se/generated/crown_get.wav", 0.85f, false });
    AddDefaultEntry({ "explosion", "爆発", kCategorySE, "Resources/audio/se/generated/explosion.wav", 0.85f, false });
    AddDefaultEntry({ "hit_damage", "被弾", kCategorySE, "Resources/audio/se/generated/hit_damage.wav", 0.82f, false });
    AddDefaultEntry({ "enemy_bind_loop", "敵拘束ループ", kCategorySE, "Resources/audio/se/generated/enemy_bind_loop.wav", 0.45f, true });
    AddDefaultEntry({ "enemy_transform", "敵取り込み変身", kCategorySE, "Resources/audio/se/generated/enemy_transform.wav", 0.82f, false });
    AddDefaultEntry({ "gate_enter", "ゲート進入", kCategorySE, "Resources/audio/se/generated/gate_enter.wav", 0.78f, false });
    AddDefaultEntry({ "jump", "ジャンプ", kCategorySE, "Resources/audio/se/generated/jump.wav", 0.70f, false });
    AddDefaultEntry({ "land", "着地", kCategorySE, "Resources/audio/se/generated/land.wav", 0.58f, false });
    AddDefaultEntry({ "slime_stretch", "スライム伸び", kCategorySE, "Resources/audio/se/generated/slime_stretch.wav", 0.62f, false });
    AddDefaultEntry({ "throw", "投げ", kCategorySE, "Resources/audio/se/generated/throw.wav", 0.70f, false });
    AddDefaultEntry({ "beam_charge", "ビーム溜め", kCategorySE, "Resources/audio/se/generated/beam_charge.wav", 0.68f, false });
    AddDefaultEntry({ "beam_fire", "ビーム発射", kCategorySE, "Resources/audio/se/generated/beam_fire.wav", 0.75f, false });
    AddDefaultEntry({ "menu_open", "メニュー開始", kCategorySE, "Resources/audio/se/generated/menu_open.wav", 0.65f, false });
    AddDefaultEntry({ "menu_close", "メニュー終了", kCategorySE, "Resources/audio/se/generated/menu_close.wav", 0.60f, false });

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
