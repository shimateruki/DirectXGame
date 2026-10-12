#include "GameAudioSettings.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace {
constexpr const char* kCategorySE = "SE";
constexpr const char* kCategoryBGM = "BGM";
} // namespace

GameAudioSettings* GameAudioSettings::GetInstance() {
    // 音声 ID とロード済みハンドルを全シーンで共有する。
    static GameAudioSettings instance;
    return &instance;
}

void GameAudioSettings::Initialize() {
    // 登録表は初回だけ読み込み、シーン切替のたびに再構築しない。
    if (initialized_) {
        return;
    }

    initialized_ = true;
    Load();
}

void GameAudioSettings::Load() {
    // 壊れた項目は飛ばし、Editor で登録した項目だけを読み込みます。
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
        const auto readString = [](const nlohmann::json& item, const char* key, const std::string& fallback) {
            return item.contains(key) && item[key].is_string() ? item[key].get<std::string>() : fallback;
        };
        std::unordered_set<std::string> usedIds;
        for (const auto& item : data["entries"]) {
            if (!item.is_object()) {
                continue;
            }

            SoundEntry entry;
            entry.id = readString(item, "id", "");
            entry.displayName = readString(item, "displayName", entry.id);
            entry.category = readString(item, "category", kCategorySE);
            entry.path = readString(item, "path", "");
            entry.volume = item.contains("volume") && item["volume"].is_number()
                               ? ClampVolume(item["volume"].get<float>())
                               : 1.0f;
            entry.loop = item.contains("loop") && item["loop"].is_boolean() ? item["loop"].get<bool>() : false;

            if (!entry.id.empty() && !entry.path.empty() && usedIds.insert(entry.id).second) {
                entries_.push_back(entry);
            }
        }
    }

    // 既定の仮SE・BGMは補充しない。Editor から登録した項目だけを使う。
}

void GameAudioSettings::Save() const {
    // エディターに登録された音だけを書き出し、仮の既定 SE を再生成しない。
    nlohmann::json data;
    data["entries"] = nlohmann::json::array();

    for (const auto& entry : entries_) {
        data["entries"].push_back({{"id", entry.id},
                                   {"displayName", entry.displayName},
                                   {"category", entry.category},
                                   {"path", entry.path},
                                   {"volume", ClampVolume(entry.volume)},
                                   {"loop", entry.loop}});
    }

    std::filesystem::create_directories(std::filesystem::path(filePath_).parent_path());
    std::ofstream output(filePath_);
    if (output.is_open()) {
        output << data.dump(4);
    }
}

GameAudioSettings::SoundEntry* GameAudioSettings::FindEntry(const std::string& id) {
    // 存在しない ID は nullptr とし、呼び出し側が再生を省略できる。
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const SoundEntry& entry) { return entry.id == id; });
    return it == entries_.end() ? nullptr : &(*it);
}

const GameAudioSettings::SoundEntry* GameAudioSettings::FindEntry(const std::string& id) const {
    // 読み取り専用の設定参照にも同じ ID 検索を使う。
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const SoundEntry& entry) { return entry.id == id; });
    return it == entries_.end() ? nullptr : &(*it);
}

bool GameAudioSettings::AddEntry(const SoundEntry& entry) {
    // 空 ID・空パス・重複 ID を拒否して保存データの一意性を保つ。
    if (entry.id.empty() || entry.path.empty() || FindEntry(entry.id)) {
        return false;
    }
    entries_.push_back(entry);
    return true;
}

bool GameAudioSettings::RemoveEntry(const std::string& id) {
    // 削除前に再生を停止し、古いロードハンドルを表から外す。
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const SoundEntry& entry) { return entry.id == id; });
    if (it == entries_.end()) {
        return false;
    }
    Stop(id);
    handles_.erase(id);
    entries_.erase(it);
    return true;
}

float GameAudioSettings::GetVolume(const std::string& id, float fallback) const {
    // 未登録の音でも呼び出し側が指定した音量で安全に扱える。
    const SoundEntry* entry = FindEntry(id);
    return entry ? ClampVolume(entry->volume) : ClampVolume(fallback);
}

void GameAudioSettings::SetVolume(const std::string& id, float volume) {
    // エディター上の設定だけを変更し、保存は明示操作に委ねる。
    SoundEntry* entry = FindEntry(id);
    if (entry) {
        entry->volume = ClampVolume(volume);
    }
}

AudioPlayer::AudioHandle GameAudioSettings::LoadHandle(const SoundEntry& entry) {
    // 同じ音声ファイルを繰り返し読み込まないよう、読み込み済みハンドルを共有します。
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
    // ID で登録された音だけを、設定音量と呼び出し倍率の積で再生する。
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
    // 未登録・ファイル欠落時は再生を省略し、現在シーンを止めない。
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
    // カテゴリに応じて BGM 全体停止か、該当 SE ハンドル停止を選ぶ。
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

float GameAudioSettings::ClampVolume(float value) {
    // 不正な浮動小数が音声 API に届かないようにする。
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f;
}
