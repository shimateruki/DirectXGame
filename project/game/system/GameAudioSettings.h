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

    /// 音声設定を共有するインスタンスを返す。
    static GameAudioSettings* GetInstance();

    /// 保存済みの登録音声を読み込む。
    void Initialize();
    // JSON の不正な項目・重複 ID は読み飛ばす。Editor で登録した分だけ有効。
    void Load();
    /// 現在の音声登録と音量を JSON へ書き出す。
    void Save() const;

    std::vector<SoundEntry>& GetEntries() { return entries_; }
    const std::vector<SoundEntry>& GetEntries() const { return entries_; }

    /// ID に一致する登録音声を返す。見つからなければ nullptr。
    SoundEntry* FindEntry(const std::string& id);
    const SoundEntry* FindEntry(const std::string& id) const;
    // Editor で音声を付け直すための登録・削除。重複 ID と空の必須項目を拒否する。
    bool AddEntry(const SoundEntry& entry);
    bool RemoveEntry(const std::string& id);

    // 見つからない ID は fallback を返す。返す音量と設定する音量は 0～1 に収める。
    float GetVolume(const std::string& id, float fallback = 1.0f) const;
    void SetVolume(const std::string& id, float volume);

    // id ごとに音声ファイルのハンドルを再利用する。ファイルがなければ無効ハンドルを返す。
    AudioPlayer::AudioHandle LoadHandle(const SoundEntry& entry);
    // volumeScale は登録音量へ乗算する倍率。loopOverride はループ再生を追加指定する。
    void PlaySE(const std::string& id, float volumeScale = 1.0f, bool loopOverride = false);
    void PlayBGM(const std::string& id, float volumeScale = 1.0f, bool loopOverride = true);
    /// ID に対応する再生中の音声を停止する。
    void Stop(const std::string& id);

  private:
    GameAudioSettings() = default;
    GameAudioSettings(const GameAudioSettings&) = delete;
    GameAudioSettings& operator=(const GameAudioSettings&) = delete;

    static float ClampVolume(float value);

    std::string filePath_ = "Resources/json/audio/audio_settings.json";
    bool initialized_ = false;
    std::vector<SoundEntry> entries_;                                   // 登録されている音声設定。
    std::unordered_map<std::string, AudioPlayer::AudioHandle> handles_; // id ごとのロード済みハンドル。
};
