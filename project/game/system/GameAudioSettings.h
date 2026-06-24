#pragma once

#include "AudioPlayer.h"

#include <string>
#include <unordered_map>
#include <vector>

// 効果音/BGM の個別音量と再生ハンドルを管理する
class GameAudioSettings {
public:
    // JSON に保存する音声1件分の設定
    struct SoundEntry {
        std::string id;
        std::string displayName;
        std::string category;
        std::string path;
        float volume = 1.0f;
        bool loop = false;
    };

    static GameAudioSettings* GetInstance();

    void Initialize();
    void Load();
    void Save() const;

    std::vector<SoundEntry>& GetEntries() { return entries_; }
    const std::vector<SoundEntry>& GetEntries() const { return entries_; }

    SoundEntry* FindEntry(const std::string& id);
    const SoundEntry* FindEntry(const std::string& id) const;

    float GetVolume(const std::string& id, float fallback = 1.0f) const;
    void SetVolume(const std::string& id, float volume);

    AudioPlayer::AudioHandle LoadHandle(const SoundEntry& entry);
    void PlaySE(const std::string& id, float volumeScale = 1.0f, bool loopOverride = false);
    void PlayBGM(const std::string& id, float volumeScale = 1.0f, bool loopOverride = true);
    void Stop(const std::string& id);

private:
    GameAudioSettings() = default;
    GameAudioSettings(const GameAudioSettings&) = delete;
    GameAudioSettings& operator=(const GameAudioSettings&) = delete;

    void EnsureDefaultEntries();
    void AddDefaultEntry(const SoundEntry& entry);
    static float ClampVolume(float value);

    std::string filePath_ = "Resources/json/audio/audio_settings.json";
    bool initialized_ = false;
    std::vector<SoundEntry> entries_; // 登録されている音声設定。
    std::unordered_map<std::string, AudioPlayer::AudioHandle> handles_; // id ごとのロード済みハンドル。
};
