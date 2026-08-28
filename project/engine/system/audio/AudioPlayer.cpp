#include "AudioPlayer.h"
#include <algorithm>
#include <string>
#include "CameraManager.h"
#include "json.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

AudioPlayer* AudioPlayer::GetInstance() {
	static AudioPlayer instance;
	return &instance;
}

void AudioPlayer::Initialize() {
	HRESULT result = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(result));
	result = xAudio2_->CreateMasteringVoice(&masteringVoice_);
	assert(SUCCEEDED(result));
	result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
	assert(SUCCEEDED(result));
}

void AudioPlayer::Finalize() {
	for (auto& [handle, data] : transientSoundDatas_) {
		DestroyStreamingData(data.get());
	}
	transientSoundDatas_.clear();



	for (auto const& [handle, dataPtr] : streamingSoundDatas_) {
		SoundDataStreaming* data = dataPtr.get();
		if (!data) {
			continue;
		}

		// 1. もし再生中 (スレッドが動作中) なら、まず Stop を呼ぶ
		if (data->isPlaying) {
			StopSe(handle); // Stop がスレッド join と DestroyVoice を行う
		}
		// 2. 再生中でなくても、古いボイスが残っている場合 (ワンショット再生終了後など)
		else if (data->sourceVoice) {
			if (data->decodeThread.joinable()) {
				data->decodeThread.join();
			}
			data->sourceVoice->Stop(0);
			data->sourceVoice->FlushSourceBuffers();
			data->sourceVoice->DestroyVoice();
			data->sourceVoice = nullptr;
		}
		// 3. スレッドだけ残っている場合 (ほぼないはずだが念のため)
		else if (data->decodeThread.joinable()) {
			data->decodeThread.join();
		}


		if (data->sourceReader) {
			data->sourceReader.Reset();
		}
	}

	// ★ マップ自体もクリアして、unique_ptr がデストラクタを呼ぶのを防ぐ
	// これでプログラム終了時にデストラクタが走っても、中身は空になっている
	streamingSoundDatas_.clear();

	MFShutdown();
	// xAudio2_ は ComPtr なので、この関数の後デストラクタで自動解放される
}

std::unique_ptr<SoundDataStreaming> AudioPlayer::CreateStreamingData(
	const std::string& filename) const {
	auto data = std::make_unique<SoundDataStreaming>();

	const int stringSize =
		MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0);
	if (stringSize <= 0) {
		return nullptr;
	}
	std::wstring wideFilename(static_cast<std::size_t>(stringSize), L'\0');
	MultiByteToWideChar(
		CP_UTF8, 0, filename.c_str(), -1, wideFilename.data(), stringSize);

	HRESULT result = MFCreateSourceReaderFromURL(
		wideFilename.c_str(), nullptr, &data->sourceReader);
	if (FAILED(result)) {
		return nullptr;
	}

	Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
	result = MFCreateMediaType(&mediaType);
	if (FAILED(result)) {
		return nullptr;
	}
	mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	result = data->sourceReader->SetCurrentMediaType(
		static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
		nullptr,
		mediaType.Get());
	if (FAILED(result)) {
		return nullptr;
	}

	Microsoft::WRL::ComPtr<IMFMediaType> outputMediaType;
	result = data->sourceReader->GetCurrentMediaType(
		static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
		&outputMediaType);
	if (FAILED(result)) {
		return nullptr;
	}
	UINT32 waveFormatSize = 0;
	result = MFCreateWaveFormatExFromMFMediaType(
		outputMediaType.Get(), &data->waveFormat, &waveFormatSize);
	if (FAILED(result) || !data->waveFormat) {
		return nullptr;
	}
	return data;
}

void AudioPlayer::DestroyStreamingData(SoundDataStreaming* data) {
	if (!data) {
		return;
	}
	data->isPlaying = false;
	data->cv.notify_one();
	if (data->decodeThread.joinable()) {
		data->decodeThread.join();
	}
	if (data->sourceVoice) {
		data->sourceVoice->Stop(0);
		data->sourceVoice->FlushSourceBuffers();
		data->sourceVoice->DestroyVoice();
		data->sourceVoice = nullptr;
	}
	data->sourceReader.Reset();
}
AudioPlayer::AudioHandle AudioPlayer::LoadSoundFile(const std::string& filename) {
	std::lock_guard<std::mutex> lock(audioDataMutex_);
	auto existing = audioHandleMap_.find(filename);
	if (existing != audioHandleMap_.end()) {
		return existing->second;
	}

	auto data = CreateStreamingData(filename);
	if (!data) {
		return kInvalidAudioHandle;
	}

	const AudioHandle newHandle = nextHandle_++;
	audioHandleMap_[filename] = newHandle;
	streamingSoundDatas_[newHandle] = std::move(data);
	return newHandle;
}


void AudioPlayer::PlaySE(AudioHandle handle, bool loop, float volume)
{
	PlayStreaming(handle, loop, std::clamp(volume, 0.0f, 1.0f) * seMasterVolume_);
}

void AudioPlayer::SetSEMasterVolume(float volume) {
	seMasterVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioPlayer::SetBGMMasterVolume(float volume) {
	bgmMasterVolume_ = std::clamp(volume, 0.0f, 1.0f);
	ApplyCurrentBGMVolume();
}

void AudioPlayer::PlayStreaming(AudioHandle handle, bool loop, float volume)
{
	auto it = streamingSoundDatas_.find(handle);
	if (it == streamingSoundDatas_.end()) {
		return; // 無効なハンドル
	}

	SoundDataStreaming* data = it->second.get();


	// 1. もし今まさに再生中 (スレッドが動作中) なら、まず Stop する
	if (data->isPlaying) {
		StopSe(handle); // Stop がスレッド join と DestroyVoice を行う
	}
	// 2. 再生中でなくても、古いボイスが残っている場合 (ワンショット再生終了後など)
	else if (data->sourceVoice) {
		// (念のため) スレッドがもし万が一 joinable なら終了を待つ
		if (data->decodeThread.joinable()) {
			data->decodeThread.join();
		}
		// 古いボイスを明示的に破棄する
		data->sourceVoice->Stop(0);
		data->sourceVoice->FlushSourceBuffers();
		data->sourceVoice->DestroyVoice();
		data->sourceVoice = nullptr;
	}

	PROPVARIANT var = {};
	PropVariantInit(&var);
	var.vt = VT_I8;
	var.hVal.QuadPart = 0;
	data->sourceReader->SetCurrentPosition(GUID_NULL, var);
	PropVariantClear(&var);


	xAudio2_->CreateSourceVoice(&data->sourceVoice, data->waveFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &data->voiceCallback, NULL, NULL);

	// ★ 新しい引数 volume を設定 ★
	data->sourceVoice->SetVolume(std::clamp(volume, 0.0f, 1.0f));

	data->sourceVoice->Start(0);

	// 状態をリセット
	data->isPlaying = true;
	data->isEndOfStream = false;
	data->loop = loop;

	// 新しいデコードスレッドを開始
	data->decodeThread = std::thread(&AudioPlayer::DecodeThread, this, data);
}

void AudioPlayer::StopSe(AudioHandle handle) {
	auto it = streamingSoundDatas_.find(handle);
	if (it == streamingSoundDatas_.end() || !it->second->isPlaying) {
		return;
	}

	SoundDataStreaming* data = it->second.get();
	data->isPlaying = false;
	data->cv.notify_one();

	if (data->decodeThread.joinable()) {
		data->decodeThread.join();
	}

	if (data->sourceVoice) {
		data->sourceVoice->Stop(0);
		data->sourceVoice->FlushSourceBuffers();
		data->sourceVoice->DestroyVoice();
		data->sourceVoice = nullptr;
	}

	PROPVARIANT var = {};
	PropVariantInit(&var);
	var.vt = VT_I8;
	var.hVal.QuadPart = 0;
	data->sourceReader->SetCurrentPosition(GUID_NULL, var);
	PropVariantClear(&var);

	data->isEndOfStream = false;
}
void AudioPlayer::DecodeThread(SoundDataStreaming* data) {
	if (!data) return;

	while (data->isPlaying) {
		XAUDIO2_VOICE_STATE state = {};
		data->sourceVoice->GetState(&state);

		// バッファの空きができたらデコードを進める
		if (state.BuffersQueued < SoundDataStreaming::kNumBuffers) {

			DWORD streamFlags = 0;
			Microsoft::WRL::ComPtr<IMFSample> mfSample;
			HRESULT hr = data->sourceReader->ReadSample(
				(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
				0, NULL, &streamFlags, NULL, &mfSample);

			if (SUCCEEDED(hr) && (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM)) {
				if (data->loop) {
					// ループ再生: ストリームの先頭に戻す
					PROPVARIANT var = {};
					PropVariantInit(&var);
					var.vt = VT_I8;
					var.hVal.QuadPart = 0;
					data->sourceReader->SetCurrentPosition(GUID_NULL, var);
					PropVariantClear(&var);
					// この後、次のループで通常通りデコード処理に進む
				} else {
					// ループしない: 再生キューが空になるのを待ってスレッド終了
					while (state.BuffersQueued > 0 && data->isPlaying) {
						data->sourceVoice->GetState(&state);
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
					}
					break;
				}
			}

			if (SUCCEEDED(hr) && mfSample) {
				Microsoft::WRL::ComPtr<IMFMediaBuffer> mfBuffer;
				BYTE* localAudioData = nullptr;
				DWORD localAudioDataBytes = 0;

				mfSample->ConvertToContiguousBuffer(&mfBuffer);
				mfBuffer->Lock(&localAudioData, NULL, &localAudioDataBytes);

				// ★★★ バグ修正: リングバッファの該当インデックスのバッファにデータをコピー ★★★
				auto& currentBuffer = data->buffers[data->currentBufferIndex];
				currentBuffer.assign(localAudioData, localAudioData + localAudioDataBytes);

				mfBuffer->Unlock();

				// XAudio2にバッファを送信
				XAUDIO2_BUFFER audioBuffer = {};
				audioBuffer.pAudioData = currentBuffer.data();
				audioBuffer.AudioBytes = (UINT32)currentBuffer.size();
				audioBuffer.pContext = &data->cv;
				data->sourceVoice->SubmitSourceBuffer(&audioBuffer);

				// ★★★ バグ修正: 次に使うバッファのインデックスを更新 ★★★
				data->currentBufferIndex = (data->currentBufferIndex + 1) % SoundDataStreaming::kNumBuffers;
			}
		} else {
			// バッファが一杯なら、コールバックからの通知を待つ
			std::unique_lock<std::mutex> lock(data->mtx);
			data->cv.wait_for(lock, std::chrono::milliseconds(10));
		}
	}
	// isPlayingがfalseになったらループを抜けてスレッド終了
	data->isPlaying = false;
}

// --- 既存のWAV再生関数（変更なし） ---
SoundData AudioPlayer::SoundLoadWave(const char* filename)
{
	// (省略: 変更なし)
	std::ifstream file;
	file.open(filename, std::ios::binary);
	assert(file.is_open());
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0) { assert(0); }
	if (strncmp(riff.type, "WAVE", 4) != 0) { assert(0); }
	FormatChunk format = {};
	file.read((char*)&format, sizeof(ChunkHeader));
	if (strncmp(format.chunk.id, "fmt ", 4) != 0) { assert(0); }
	assert(format.chunk.size <= sizeof(format.format));
	file.read((char*)&format.format, format.chunk.size);
	ChunkHeader data;
	file.read((char*)&data, sizeof(data));
	if (strncmp(data.id, "JUNK", 4) == 0) {
		file.seekg(data.size, std::ios_base::cur);
		file.read((char*)&data, sizeof(data));
	}
	if (strncmp(data.id, "data", 4) != 0) { assert(0); }
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);
	file.close();
	SoundData soundData = {};
	soundData.wfex = format.format;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;
	return soundData;
}
void AudioPlayer::SoundUnload(SoundData* soundData) {/* (省略) */ delete[] soundData->pBuffer; soundData->pBuffer = 0; soundData->bufferSize = 0; soundData->wfex = {}; }
void AudioPlayer::SoundPlayWave(const SoundData& soundData, bool loop)
{
	// (省略: 変更なし)
	HRESULT result;
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2_->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));
	XAUDIO2_BUFFER buf{};
	buf.pAudioData = soundData.pBuffer;
	buf.AudioBytes = soundData.bufferSize;
	buf.Flags = XAUDIO2_END_OF_STREAM;
	if (loop) {
		buf.LoopCount = XAUDIO2_LOOP_INFINITE;
	}
	result = pSourceVoice->SubmitSourceBuffer(&buf);
	result = pSourceVoice->Start(0);
}

/// <summary>
/// BGMを再生（または継続）します。
/// </summary>
void AudioPlayer::PlayBGM(AudioHandle handle, bool loop, float volume) {
	currentBgmBaseVolume_ = std::clamp(volume, 0.0f, 1.0f);

	// 1. 既に再生したいBGMが再生中なら、何もしない
	if (currentBgmHandle_ == handle && IsPlaying(handle)) {
		ApplyCurrentBGMVolume();
		return; // そのまま再生を続ける
	}

	// 2. 違うBGMが再生中なら、それを止める
	if (currentBgmHandle_ != kInvalidAudioHandle && IsPlaying(currentBgmHandle_)) {
		StopSe(currentBgmHandle_);
	}

	// 3. 新しいBGMを再生する
	PlayStreaming(handle, loop, currentBgmBaseVolume_ * bgmMasterVolume_);
	currentBgmHandle_ = handle; // 今再生中のBGMとして記憶
}

/// <summary>
/// 現在再生中のBGMを停止します。
/// </summary>
void AudioPlayer::StopBGM() {
	if (currentBgmHandle_ != kInvalidAudioHandle) {
		StopSe(currentBgmHandle_); // 既存のStop関数を呼び出す
		currentBgmHandle_ = kInvalidAudioHandle; // 再生中BGMの記憶をクリア
	}
}

/// <summary>
/// 指定したハンドルが現在再生中か確認します。
/// </summary>
bool AudioPlayer::IsPlaying(AudioHandle handle) const {
	auto it = streamingSoundDatas_.find(handle);
	if (it == streamingSoundDatas_.end()) {
		return false; // ロードされていない
	}
	// スレッドが動作中か (isPlaying フラグ) で判断
	return it->second->isPlaying;
}

void AudioPlayer::ApplyCurrentBGMVolume() {
	if (currentBgmHandle_ == kInvalidAudioHandle) {
		return;
	}

	auto it = streamingSoundDatas_.find(currentBgmHandle_);
	if (it == streamingSoundDatas_.end() || !it->second || !it->second->sourceVoice) {
		return;
	}

	it->second->sourceVoice->SetVolume(currentBgmBaseVolume_ * bgmMasterVolume_);
}
AudioPlayer::PlaybackHandle AudioPlayer::PlayTransientSE(
	const std::string& filename,
	float volume,
	float pitch) {
	Update();
	if (!xAudio2_) {
		return kInvalidPlaybackHandle;
	}

	auto data = CreateStreamingData(filename);
	if (!data || !data->waveFormat) {
		return kInvalidPlaybackHandle;
	}

	HRESULT result = xAudio2_->CreateSourceVoice(
		&data->sourceVoice,
		data->waveFormat,
		0,
		XAUDIO2_DEFAULT_FREQ_RATIO,
		&data->voiceCallback,
		nullptr,
		nullptr);
	if (FAILED(result) || !data->sourceVoice) {
		return kInvalidPlaybackHandle;
	}

	data->sourceVoice->SetVolume(
		std::clamp(volume, 0.0f, 1.0f) * seMasterVolume_);
	data->sourceVoice->SetFrequencyRatio(std::clamp(pitch, 0.5f, 2.0f));
	data->isEndOfStream = false;
	data->loop = false;
	data->currentBufferIndex = 0;
	data->isPlaying = true;
	data->sourceVoice->Start(0);

	const PlaybackHandle handle = nextPlaybackHandle_++;
	SoundDataStreaming* rawData = data.get();
	transientSoundDatas_.emplace(handle, std::move(data));
	rawData->decodeThread = std::thread(&AudioPlayer::DecodeThread, this, rawData);
	return handle;
}

bool AudioPlayer::IsTransientPlaying(PlaybackHandle handle) const {
	const auto found = transientSoundDatas_.find(handle);
	return found != transientSoundDatas_.end() &&
		found->second &&
		found->second->isPlaying.load();
}

void AudioPlayer::StopTransient(PlaybackHandle handle) {
	auto found = transientSoundDatas_.find(handle);
	if (found == transientSoundDatas_.end()) {
		return;
	}
	DestroyStreamingData(found->second.get());
	transientSoundDatas_.erase(found);
}

void AudioPlayer::Update() {
	for (auto iterator = transientSoundDatas_.begin();
		iterator != transientSoundDatas_.end();) {
		if (iterator->second && iterator->second->isPlaying.load()) {
			++iterator;
			continue;
		}
		DestroyStreamingData(iterator->second.get());
		iterator = transientSoundDatas_.erase(iterator);
	}
}

namespace {
std::string NormalizeAudioPath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	while (path.rfind("./", 0) == 0) {
		path.erase(0, 2);
	}
	return path;
}

bool IsAudioFilePath(const std::filesystem::path& path) {
	std::string extension = path.extension().string();
	std::transform(
		extension.begin(),
		extension.end(),
		extension.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
	return extension == ".wav" || extension == ".mp3" ||
		extension == ".ogg" || extension == ".flac";
}

void SetAudioEventError(std::string* destination, const std::string& message) {
	if (destination) {
		*destination = message;
	}
}
}

AudioEventSystem* AudioEventSystem::GetInstance() {
	static AudioEventSystem instance;
	return &instance;
}

bool AudioEventSystem::LoadEvent(
	const std::string& eventPath,
	AudioEventDefinition& definition,
	std::string* errorMessage) const {
	const std::string normalizedPath = NormalizeAudioPath(eventPath);
	std::ifstream file(normalizedPath, std::ios::binary);
	if (!file) {
		SetAudioEventError(errorMessage, "Audio Eventを開けません: " + normalizedPath);
		return false;
	}

	try {
		nlohmann::json root;
		file >> root;
		AudioEventDefinition loaded;

		if (!root.contains("clips") || !root["clips"].is_array()) {
			SetAudioEventError(errorMessage, "clips配列がありません: " + normalizedPath);
			return false;
		}
		const std::filesystem::path parent =
			std::filesystem::path(normalizedPath).parent_path();
		for (const auto& value : root["clips"]) {
			if (!value.is_string()) {
				continue;
			}
			std::string clip = NormalizeAudioPath(value.get<std::string>());
			if (clip.empty()) {
				continue;
			}
			std::filesystem::path clipPath(clip);
			if (!clipPath.is_absolute() && clip.rfind("Resources/", 0) != 0) {
				const std::filesystem::path besideEvent = parent / clipPath;
				const std::filesystem::path underSe =
					std::filesystem::path("Resources/audio/se") / clipPath;
				if (std::filesystem::exists(besideEvent)) {
					clip = NormalizeAudioPath(besideEvent.generic_string());
				}
				else {
					clip = NormalizeAudioPath(underSe.generic_string());
				}
			}
			if (IsAudioFilePath(std::filesystem::path(clip))) {
				loaded.clips.push_back(std::move(clip));
			}
		}
		if (loaded.clips.empty()) {
			SetAudioEventError(errorMessage, "再生可能なclipがありません: " + normalizedPath);
			return false;
		}

		const auto readRange = [&root](
			const char* key,
			float& minimum,
			float& maximum) {
			if (!root.contains(key) || !root[key].is_array() ||
				root[key].size() < 2) {
				return;
			}
			minimum = root[key][0].get<float>();
			maximum = root[key][1].get<float>();
			if (minimum > maximum) {
				std::swap(minimum, maximum);
			}
		};
		readRange("volumeRange", loaded.volumeMin, loaded.volumeMax);
		readRange("pitchRange", loaded.pitchMin, loaded.pitchMax);
		loaded.volumeMin = std::clamp(loaded.volumeMin, 0.0f, 1.0f);
		loaded.volumeMax = std::clamp(loaded.volumeMax, 0.0f, 1.0f);
		loaded.pitchMin = std::clamp(loaded.pitchMin, 0.5f, 2.0f);
		loaded.pitchMax = std::clamp(loaded.pitchMax, 0.5f, 2.0f);
		loaded.maxInstances =
			std::clamp(root.value("maxInstances", loaded.maxInstances), 1, 64);
		loaded.spatial = root.value("spatial", loaded.spatial);
		loaded.minDistance =
			(std::max)(0.0f, root.value("minDistance", loaded.minDistance));
		loaded.maxDistance =
			(std::max)(loaded.minDistance + 0.01f,
				root.value("maxDistance", loaded.maxDistance));

		definition = std::move(loaded);
		SetAudioEventError(errorMessage, "");
		return true;
	}
	catch (const std::exception& exception) {
		SetAudioEventError(
			errorMessage,
			"Audio Eventの解析に失敗しました: " + std::string(exception.what()));
		return false;
	}
}

bool AudioEventSystem::SaveEvent(
	const std::string& eventPath,
	const AudioEventDefinition& definition,
	std::string* errorMessage) {
	if (definition.clips.empty()) {
		SetAudioEventError(errorMessage, "1つ以上のclipが必要です。");
		return false;
	}

	const std::filesystem::path path(NormalizeAudioPath(eventPath));
	std::error_code error;
	if (!path.parent_path().empty()) {
		std::filesystem::create_directories(path.parent_path(), error);
		if (error) {
			SetAudioEventError(errorMessage, "保存フォルダを作成できません。");
			return false;
		}
	}

	AudioEventDefinition sanitized = definition;
	if (sanitized.volumeMin > sanitized.volumeMax) {
		std::swap(sanitized.volumeMin, sanitized.volumeMax);
	}
	if (sanitized.pitchMin > sanitized.pitchMax) {
		std::swap(sanitized.pitchMin, sanitized.pitchMax);
	}
	sanitized.volumeMin = std::clamp(sanitized.volumeMin, 0.0f, 1.0f);
	sanitized.volumeMax = std::clamp(sanitized.volumeMax, 0.0f, 1.0f);
	sanitized.pitchMin = std::clamp(sanitized.pitchMin, 0.5f, 2.0f);
	sanitized.pitchMax = std::clamp(sanitized.pitchMax, 0.5f, 2.0f);
	sanitized.maxInstances = std::clamp(sanitized.maxInstances, 1, 64);
	sanitized.minDistance = (std::max)(0.0f, sanitized.minDistance);
	sanitized.maxDistance =
		(std::max)(sanitized.minDistance + 0.01f, sanitized.maxDistance);

	nlohmann::json root = {
		{ "schemaVersion", 1 },
		{ "clips", sanitized.clips },
		{ "volumeRange", { sanitized.volumeMin, sanitized.volumeMax } },
		{ "pitchRange", { sanitized.pitchMin, sanitized.pitchMax } },
		{ "maxInstances", sanitized.maxInstances },
		{ "spatial", sanitized.spatial },
		{ "minDistance", sanitized.minDistance },
		{ "maxDistance", sanitized.maxDistance }
	};

	std::ofstream file(path, std::ios::binary);
	if (!file) {
		SetAudioEventError(errorMessage, "Audio Eventを保存できません。");
		return false;
	}
	file << root.dump(2);
	if (!file.good()) {
		SetAudioEventError(errorMessage, "Audio Eventの書き込みに失敗しました。");
		return false;
	}

	eventCache_[NormalizeAudioPath(path.generic_string())] = std::move(sanitized);
	SetAudioEventError(errorMessage, "");
	return true;
}

bool AudioEventSystem::Prewarm(
	const std::string& eventPath,
	std::string* errorMessage) {
	AudioEventDefinition definition;
	if (!LoadEvent(eventPath, definition, errorMessage)) {
		return false;
	}

	bool succeeded = true;
	for (const std::string& clip : definition.clips) {
		if (AudioPlayer::GetInstance()->LoadSoundFile(clip) ==
			AudioPlayer::kInvalidAudioHandle) {
			succeeded = false;
			SetAudioEventError(errorMessage, "音声を準備できません: " + clip);
		}
	}
	if (succeeded) {
		eventCache_[NormalizeAudioPath(eventPath)] = std::move(definition);
	}
	return succeeded;
}

AudioPlayer::PlaybackHandle AudioEventSystem::Play(
	const std::string& eventPath,
	const Vector3* worldPosition,
	float volumeScale) {
	Update();
	const std::string normalizedPath = NormalizeAudioPath(eventPath);

	auto cached = eventCache_.find(normalizedPath);
	if (cached == eventCache_.end()) {
		AudioEventDefinition definition;
		if (!LoadEvent(normalizedPath, definition, nullptr)) {
			return AudioPlayer::kInvalidPlaybackHandle;
		}
		cached = eventCache_.emplace(normalizedPath, std::move(definition)).first;
	}
	const AudioEventDefinition& definition = cached->second;

	const std::size_t activeCount = static_cast<std::size_t>(std::count_if(
		activeInstances_.begin(),
		activeInstances_.end(),
		[&](const ActiveInstance& instance) {
			return instance.eventPath == normalizedPath;
		}));
	if (activeCount >= static_cast<std::size_t>(definition.maxInstances)) {
		return AudioPlayer::kInvalidPlaybackHandle;
	}

	std::uniform_int_distribution<std::size_t> clipDistribution(
		0, definition.clips.size() - 1);
	std::size_t clipIndex = clipDistribution(randomEngine_);
	const auto last = lastClipByEvent_.find(normalizedPath);
	if (definition.clips.size() > 1 &&
		last != lastClipByEvent_.end() &&
		clipIndex == last->second) {
		clipIndex = (clipIndex + 1) % definition.clips.size();
	}
	lastClipByEvent_[normalizedPath] = clipIndex;

	std::uniform_real_distribution<float> volumeDistribution(
		definition.volumeMin, definition.volumeMax);
	std::uniform_real_distribution<float> pitchDistribution(
		definition.pitchMin, definition.pitchMax);
	float volume = volumeDistribution(randomEngine_) *
		std::clamp(volumeScale, 0.0f, 1.0f);

	if (definition.spatial && worldPosition) {
		if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
			const float distance = Math::Length(*worldPosition - camera->GetEye());
			const float attenuation = 1.0f - std::clamp(
				(distance - definition.minDistance) /
					(definition.maxDistance - definition.minDistance),
				0.0f,
				1.0f);
			volume *= attenuation;
		}
	}

	const AudioPlayer::PlaybackHandle handle =
		AudioPlayer::GetInstance()->PlayTransientSE(
			definition.clips[clipIndex],
			volume,
			pitchDistribution(randomEngine_));
	if (handle != AudioPlayer::kInvalidPlaybackHandle) {
		activeInstances_.push_back({ normalizedPath, handle });
	}
	return handle;
}

AudioPlayer::PlaybackHandle AudioEventSystem::PlayReference(
	const std::string& reference,
	const Vector3* worldPosition,
	float volumeScale) {
	const std::string eventPath = ResolveEventPath(reference);
	if (!eventPath.empty()) {
		return Play(eventPath, worldPosition, volumeScale);
	}

	std::string audioPath = NormalizeAudioPath(reference);
	if (audioPath.rfind("Resources/", 0) != 0) {
		audioPath = NormalizeAudioPath(
			(std::filesystem::path("Resources/audio/se") / audioPath)
				.generic_string());
	}
	if (!IsAudioFilePath(std::filesystem::path(audioPath))) {
		return AudioPlayer::kInvalidPlaybackHandle;
	}
	return AudioPlayer::GetInstance()->PlayTransientSE(
		audioPath,
		std::clamp(volumeScale, 0.0f, 1.0f),
		1.0f);
}

void AudioEventSystem::Update() {
	AudioPlayer* audioPlayer = AudioPlayer::GetInstance();
	audioPlayer->Update();
	activeInstances_.erase(
		std::remove_if(
			activeInstances_.begin(),
			activeInstances_.end(),
			[audioPlayer](const ActiveInstance& instance) {
				return !audioPlayer->IsTransientPlaying(instance.handle);
			}),
		activeInstances_.end());
}

std::string AudioEventSystem::ResolveEventPath(
	const std::string& reference) const {
	std::string normalized = NormalizeAudioPath(reference);
	if (normalized.empty()) {
		return {};
	}

	std::filesystem::path direct(normalized);
	if (direct.extension() == ".json" && std::filesystem::exists(direct)) {
		return NormalizeAudioPath(direct.generic_string());
	}

	std::filesystem::path fileName = direct.filename();
	if (fileName.extension().empty()) {
		fileName.replace_extension(".json");
	}
	for (const std::filesystem::path& directory : {
		std::filesystem::path("Resources/json/audio_events"),
		std::filesystem::path("Resources/json/audio") }) {
		const std::filesystem::path candidate = directory / fileName;
		if (std::filesystem::exists(candidate)) {
			return NormalizeAudioPath(candidate.generic_string());
		}
	}
	return {};
}
