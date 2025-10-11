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

// Media Foundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "uuid.lib")

// --- (SoundData, ChunkHeader等の構造体は変更なし) ---
struct SoundData { WAVEFORMATEX wfex; BYTE* pBuffer; unsigned int bufferSize; };
struct ChunkHeader { char id[4]; int32_t size; };
struct RiffHeader { ChunkHeader chunk; char type[4]; };
struct FormatChunk { ChunkHeader chunk; WAVEFORMATEX format; };

// --- (AudioVoiceCallback, SoundDataStreaming 構造体も変更なし) ---
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
struct SoundDataStreaming {
	Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
	WAVEFORMATEX* waveFormat = nullptr;
	IXAudio2SourceVoice* sourceVoice = nullptr;
	AudioVoiceCallback voiceCallback;
	std::thread decodeThread;
	std::mutex mtx;
	std::condition_variable cv;
	bool isPlaying = false;
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
class AudioPlayer
{
public:
	// サウンドハンドル用の型エイリアスを定義
	using AudioHandle = uint32_t;
	static const AudioHandle kInvalidAudioHandle = (std::numeric_limits<uint32_t>::max)();

public:
	static AudioPlayer* GetInstance();
	void Initialize();
	void Finalize();

	// WAV再生機能 
	SoundData SoundLoadWave(const char* filename);
	void SoundUnload(SoundData* soundData);
	void SoundPlayWave(const SoundData& soundData, bool loop = false);

	// 新しいストリーミング再生機能
	AudioHandle LoadSoundFile(const std::string& filename);
	void Play(AudioHandle handle, bool loop = false);
	void Stop(AudioHandle handle);

private:
	AudioPlayer() = default;
	~AudioPlayer() = default;
	AudioPlayer(const AudioPlayer&) = delete;
	AudioPlayer& operator=(const AudioPlayer&) = delete;

	void DecodeThread(SoundDataStreaming* data);

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masteringVoice_ = nullptr;

	std::map<std::string, SoundData> legacySoundDatas_;

	// ハンドルでサウンドデータを管理
	std::map<AudioHandle, std::unique_ptr<SoundDataStreaming>> streamingSoundDatas_;
	std::map<std::string, AudioHandle> audioHandleMap_; // ファイルパスからハンドルを引くためのマップ
	AudioHandle nextHandle_ = 0; // 次に割り当てるハンドル番号
};