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

/// <summary>XAudio2 のバッファ再生完了をデコードスレッドへ通知します。</summary>
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

/// <summary>Media Foundation で逐次デコードする音声の再生状態です。</summary>
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


/// <summary>SE と BGM の読み込み、再生、停止、音量を管理します。</summary>
class AudioPlayer
{
public:
	// AudioHandle はロード済みファイル、PlaybackHandle は独立した再生インスタンスを指します。
	using AudioHandle = uint32_t;
	static const AudioHandle kInvalidAudioHandle = (std::numeric_limits<uint32_t>::max)();
	using PlaybackHandle = uint64_t;
	static constexpr PlaybackHandle kInvalidPlaybackHandle = 0;

public:
	/// <summary>エンジンで共有する音声管理インスタンスを取得します。</summary>
	static AudioPlayer* GetInstance();

	/// <summary>XAudio2 と Media Foundation を初期化します。</summary>
	void Initialize();

	/// <summary>再生を停止し、音声関連リソースを解放します。</summary>
	void Finalize();

	/// <summary>ファイルをロードし、以後の再生に使うハンドルを返します。</summary>
	/// <returns>成功時は音声ハンドル、失敗時は kInvalidAudioHandle。</returns>
	AudioHandle LoadSoundFile(const std::string& filename);

	/// <summary>ロード済みの SE を指定音量で再生します。</summary>
	void PlaySE(AudioHandle handle, bool loop, float volume);

	/// <summary>指定ハンドルの SE を停止します。</summary>
	void StopSe(AudioHandle handle);

	/// <summary>SE 全体の音量倍率を設定します。</summary>
	void SetSEMasterVolume(float volume);

	/// <summary>BGM 全体の音量倍率を設定します。</summary>
	void SetBGMMasterVolume(float volume);

	/// <summary>現在の SE 音量倍率を返します。</summary>
	float GetSEMasterVolume() const { return seMasterVolume_; }

	/// <summary>現在の BGM 音量倍率を返します。</summary>
	float GetBGMMasterVolume() const { return bgmMasterVolume_; }

	/// <summary>
	/// BGMを再生（または継続）します。
	/// </summary>
	/// <param name="handle">再生するBGMのハンドル</param>
	/// <param name="loop">ループ再生するか</param>
	/// <param name="volume">音量</param>
	void PlayBGM(AudioHandle handle, bool loop, float volume = 1.0f);

	/// <summary>
	/// 現在再生中のBGMを停止します。
	/// </summary>
	void StopBGM();

	/// <summary>
	/// 指定したハンドルが現在再生中か確認します。
	/// </summary>
	bool IsPlaying(AudioHandle handle) const;

	/// <summary>同じ SE を重ねて鳴らせる独立した再生インスタンスを開始します。</summary>
	/// <returns>再生インスタンスのハンドル。失敗時は kInvalidPlaybackHandle。</returns>
	PlaybackHandle PlayTransientSE(
		const std::string& filename,
		float volume = 1.0f,
		float pitch = 1.0f);

	/// <summary>独立した再生インスタンスが再生中か返します。</summary>
	bool IsTransientPlaying(PlaybackHandle handle) const;

	/// <summary>独立した再生インスタンスを停止します。</summary>
	void StopTransient(PlaybackHandle handle);

	/// <summary>再生完了したインスタンスを回収します。</summary>
	void Update();

private:
	AudioPlayer() = default;
	~AudioPlayer() = default;
	AudioPlayer(const AudioPlayer&) = delete;
	AudioPlayer& operator=(const AudioPlayer&) = delete;

	// ストリーミング音声を別スレッドでデコードし、XAudio2 へ供給します。
	void DecodeThread(SoundDataStreaming* data);

	// 通常の SE/BGM 再生で共有する逐次デコード処理です。
	void PlayStreaming(AudioHandle handle, bool loop, float volume);

	// Media Foundation の Reader と XAudio2 Voice を所有する再生状態を作成します。
	std::unique_ptr<SoundDataStreaming> CreateStreamingData(const std::string& filename) const;

	// デコードスレッドの終了後に再生状態を破棄します。
	void DestroyStreamingData(SoundDataStreaming* data);

	// 設定されたマスター音量を再生中の BGM へ反映します。
	void ApplyCurrentBGMVolume();

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masteringVoice_ = nullptr;

	// ファイル単位のハンドルと、同時再生できる一時的なインスタンスを分けて保持します。
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

/// <summary>SE の候補、音量・ピッチ揺らぎ、多重数、距離減衰の設定です。</summary>
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

/// <summary>JSON 定義に基づき、SE の候補選択と再生制限を管理します。</summary>
class AudioEventSystem {
public:
	/// <summary>エンジンで共有する音声イベント管理インスタンスを取得します。</summary>
    static AudioEventSystem* GetInstance();

	/// <summary>イベント JSON を読み込み、設定とエラーを返します。</summary>
    bool LoadEvent(
        const std::string& eventPath,
        AudioEventDefinition& definition,
        std::string* errorMessage = nullptr) const;

	/// <summary>イベント設定を JSON へ保存します。</summary>
    bool SaveEvent(
        const std::string& eventPath,
        const AudioEventDefinition& definition,
        std::string* errorMessage = nullptr);

	/// <summary>イベントを読み込み、参照する音声を事前ロードします。</summary>
    bool Prewarm(const std::string& eventPath, std::string* errorMessage = nullptr);

	/// <summary>イベント定義から SE を選び、必要なら距離減衰を適用して再生します。</summary>
    AudioPlayer::PlaybackHandle Play(
        const std::string& eventPath,
        const engine::Vector3* worldPosition = nullptr,
        float volumeScale = 1.0f);

	/// <summary>イベント名または音声パスから再生対象を解決して再生します。</summary>
    AudioPlayer::PlaybackHandle PlayReference(
        const std::string& reference,
        const engine::Vector3* worldPosition = nullptr,
        float volumeScale = 1.0f);

	/// <summary>終了した音声イベントのインスタンスを回収します。</summary>
    void Update();

	/// <summary>イベント名またはパスを JSON ファイルのパスへ解決します。</summary>
    std::string ResolveEventPath(const std::string& reference) const;

private:
    /// <summary>再生中のイベント定義と音声ハンドルの対応。</summary>
    struct ActiveInstance {
        std::string eventPath;
        AudioPlayer::PlaybackHandle handle = AudioPlayer::kInvalidPlaybackHandle;
    };

    std::unordered_map<std::string, AudioEventDefinition> eventCache_;
    std::unordered_map<std::string, std::size_t> lastClipByEvent_;
    std::vector<ActiveInstance> activeInstances_;
    std::mt19937 randomEngine_{ std::random_device{}() };
};
