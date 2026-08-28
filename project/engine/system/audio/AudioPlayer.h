#pragma once

#include <xaudio2.h>
#include <fstream>
#include <cassert>
#include <wrl.h>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <cstdint>
#include <random>
#include <unordered_map>
#include "engine/utility/math/Math.h"

// Media Foundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "uuid.lib")

// --- (SoundData, ChunkHeader等の構造体は変更なし) ---
// SoundDataは、WAVを一括読み込みする旧形式の音声データです。
struct SoundData { WAVEFORMATEX wfex; BYTE* pBuffer; unsigned int bufferSize; };
struct ChunkHeader { char id[4]; int32_t size; };
struct RiffHeader { ChunkHeader chunk; char type[4]; };
struct FormatChunk { ChunkHeader chunk; WAVEFORMATEX format; };

// --- (AudioVoiceCallback, SoundDataStreaming 構造体も変更なし) ---
// AudioVoiceCallbackは、XAudio2のバッファ再生完了を通知するためのコールバックです。
class AudioVoiceCallback : public IXAudio2VoiceCallback {
public:
	void OnBufferEnd(void* pBufferContext) override {
		std::condition_variable* cv = reinterpret_cast<std::condition_variable*>(pBufferContext);
		if (cv) { cv->notify_one(); }
	}
	void OnStreamEnd() override {}
	void OnVoiceProcessingPassStart(UINT32 BytesRequired) override {}
	void OnVoiceProcessingPassEnd() override {}
	void OnBufferStart(void* pBufferContext) override {}
	void OnLoopEnd(void* pBufferContext) override {}
	void OnVoiceError(void* pBufferContext, HRESULT Error) override {}
};
// SoundDataStreamingは、MediaFoundationでデコードしながら再生するストリーミング音声の状態です。
struct SoundDataStreaming {
	Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
	WAVEFORMATEX* waveFormat = nullptr;
	IXAudio2SourceVoice* sourceVoice = nullptr;
	AudioVoiceCallback voiceCallback;
	std::thread decodeThread;
	std::mutex mtx;
	std::condition_variable cv;
	std::atomic_bool isPlaying = false;
	bool isEndOfStream = false;
	bool loop = false;
	static const int kNumBuffers = 3;
	std::vector<BYTE> buffers[kNumBuffers];
	int currentBufferIndex = 0;
	~SoundDataStreaming() {
		if (waveFormat) {
			CoTaskMemFree(waveFormat);
			waveFormat = nullptr;
		}
	}
};


/// <summary>
/// オーディオの再生を管理するクラス
/// </summary>
// AudioPlayerは、SEとBGMの読み込み、再生、停止、音量管理をまとめて行います。
class AudioPlayer
{
public:
	// サウンドハンドル用の型エイリアスを定義
	using AudioHandle = uint32_t;
	static const AudioHandle kInvalidAudioHandle = (std::numeric_limits<uint32_t>::max)();
	using PlaybackHandle = uint64_t;
	static constexpr PlaybackHandle kInvalidPlaybackHandle = 0;

public:
		// エンジン全体で共有する音声管理インスタンスを取得します。
static AudioPlayer* GetInstance();
		// XAudio2とMediaFoundationを初期化し、音声再生の準備をします。
void Initialize();
		// 再生中の音声を停止し、音声関連リソースを解放します。
void Finalize();

	// WAV再生機能 
	SoundData SoundLoadWave(const char* filename);
	void SoundUnload(SoundData* soundData);
	void SoundPlayWave(const SoundData& soundData, bool loop = false);

	// 新しいストリーミング再生機能
	AudioHandle LoadSoundFile(const std::string& filename);
		// 指定ハンドルのSEを指定音量で再生します。
void PlaySE(AudioHandle handle, bool loop, float volume);
	void StopSe(AudioHandle handle);
	void SetSEMasterVolume(float volume);
	void SetBGMMasterVolume(float volume);
	float GetSEMasterVolume() const { return seMasterVolume_; }
	float GetBGMMasterVolume() const { return bgmMasterVolume_; }

	/// <summary>
	/// BGMを再生（または継続）します。
	/// </summary>
	/// <param name="handle">再生するBGMのハンドル</param>
	/// <param name="loop">ループ再生するか</param>
	/// <param name="volume">音量</param>
		// 指定ハンドルのBGMを再生し、BGM音量設定を反映します。
void PlayBGM(AudioHandle handle, bool loop, float volume = 1.0f);

	/// <summary>
	/// 現在再生中のBGMを停止します。
	/// </summary>
	void StopBGM();

	// ▼▼▼ IsPlaying を追加（あると便利） ▼▼▼
	/// <summary>
	/// 指定したハンドルが現在再生中か確認します。
	/// </summary>
	bool IsPlaying(AudioHandle handle) const;
	// 同じSEを重ねて鳴らすための独立したワンショット再生です。
	PlaybackHandle PlayTransientSE(
		const std::string& filename,
		float volume = 1.0f,
		float pitch = 1.0f);
	bool IsTransientPlaying(PlaybackHandle handle) const;
	void StopTransient(PlaybackHandle handle);
	void Update();

private:
	AudioPlayer() = default;
	~AudioPlayer() = default;
	AudioPlayer(const AudioPlayer&) = delete;
	AudioPlayer& operator=(const AudioPlayer&) = delete;

		// ストリーミング音声を別スレッドでデコードし、XAudio2へ供給します。
void DecodeThread(SoundDataStreaming* data);
	void PlayStreaming(AudioHandle handle, bool loop, float volume);
	std::unique_ptr<SoundDataStreaming> CreateStreamingData(const std::string& filename) const;
	void DestroyStreamingData(SoundDataStreaming* data);
	void ApplyCurrentBGMVolume();

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masteringVoice_ = nullptr;

	std::map<std::string, SoundData> legacySoundDatas_;

	// ハンドルでサウンドデータを管理
	std::map<AudioHandle, std::unique_ptr<SoundDataStreaming>> streamingSoundDatas_;
	std::map<std::string, AudioHandle> audioHandleMap_; // ファイルパスからハンドルを引くためのマップ
	AudioHandle nextHandle_ = 0; // 次に割り当てるハンドル番号
	std::unordered_map<PlaybackHandle, std::unique_ptr<SoundDataStreaming>> transientSoundDatas_;
	PlaybackHandle nextPlaybackHandle_ = 1;
	mutable std::mutex audioDataMutex_;
	AudioHandle currentBgmHandle_ = kInvalidAudioHandle;
	float seMasterVolume_ = 1.0f;
	float bgmMasterVolume_ = 1.0f;
	float currentBgmBaseVolume_ = 1.0f;
};

struct AudioEventDefinition {
    std::vector<std::string> clips;
    float volumeMin = 1.0f;
    float volumeMax = 1.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    int maxInstances = 4;
    bool spatial = false;
    float minDistance = 2.0f;
    float maxDistance = 30.0f;
};

// 効果音のバリエーション、音量・Pitch揺らぎ、多重数、距離減衰をJSONから再生します。
class AudioEventSystem {
public:
    static AudioEventSystem* GetInstance();

    bool LoadEvent(
        const std::string& eventPath,
        AudioEventDefinition& definition,
        std::string* errorMessage = nullptr) const;
    bool SaveEvent(
        const std::string& eventPath,
        const AudioEventDefinition& definition,
        std::string* errorMessage = nullptr);
    bool Prewarm(const std::string& eventPath, std::string* errorMessage = nullptr);
    AudioPlayer::PlaybackHandle Play(
        const std::string& eventPath,
        const Vector3* worldPosition = nullptr,
        float volumeScale = 1.0f);
    AudioPlayer::PlaybackHandle PlayReference(
        const std::string& reference,
        const Vector3* worldPosition = nullptr,
        float volumeScale = 1.0f);
    void Update();

    std::string ResolveEventPath(const std::string& reference) const;

private:
    struct ActiveInstance {
        std::string eventPath;
        AudioPlayer::PlaybackHandle handle = AudioPlayer::kInvalidPlaybackHandle;
    };

    std::unordered_map<std::string, AudioEventDefinition> eventCache_;
    std::unordered_map<std::string, std::size_t> lastClipByEvent_;
    std::vector<ActiveInstance> activeInstances_;
    std::mt19937 randomEngine_{ std::random_device{}() };
};
