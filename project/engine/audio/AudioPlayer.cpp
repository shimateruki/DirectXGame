#include "AudioPlayer.h"
#include <string>

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
	// ★★★ ハンドルで管理しているサウンドデータを停止 ★★★
	for (auto const& [handle, data] : streamingSoundDatas_) {
		if (data->isPlaying) {
			Stop(handle);
		}
	}
	MFShutdown();
}

AudioPlayer::AudioHandle AudioPlayer::LoadSoundFile(const std::string& filename) {
	// 既に読み込み済みの場合は、既存のハンドルを返す
	auto it = audioHandleMap_.find(filename);
	if (it != audioHandleMap_.end()) {
		return it->second;
	}

	// --- 新規読み込み処理 ---
	auto data = std::make_unique<SoundDataStreaming>();
	HRESULT result;

	std::wstring wFilename;
	int strSize = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0);
	wFilename.resize(strSize);
	MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, &wFilename[0], strSize);

	result = MFCreateSourceReaderFromURL(wFilename.c_str(), NULL, &data->sourceReader);
	if (FAILED(result)) {
		return kInvalidAudioHandle; // 読み込み失敗
	}

	// (デコーダー設定などは変更なし)
	Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
	MFCreateMediaType(&mediaType);
	mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	result = data->sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mediaType.Get());
	assert(SUCCEEDED(result));
	Microsoft::WRL::ComPtr<IMFMediaType> outputMediaType;
	data->sourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &outputMediaType);
	UINT32 waveFormatSize = 0;
	MFCreateWaveFormatExFromMFMediaType(outputMediaType.Get(), &data->waveFormat, &waveFormatSize);
	assert(data->waveFormat != nullptr);

	// ★★★ 新しいハンドルを生成して、各種マップに登録 ★★★
	AudioHandle newHandle = nextHandle_++;
	audioHandleMap_[filename] = newHandle;
	streamingSoundDatas_[newHandle] = std::move(data);

	return newHandle;
}

void AudioPlayer::Play(AudioHandle handle, bool loop) {
	auto it = streamingSoundDatas_.find(handle);
	if (it == streamingSoundDatas_.end()) {
		return; // 無効なハンドル
	}

	SoundDataStreaming* data = it->second.get();

	if (data->isPlaying) {
		Stop(handle);
	}

	xAudio2_->CreateSourceVoice(&data->sourceVoice, data->waveFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &data->voiceCallback, NULL, NULL);
	data->sourceVoice->Start(0);

	data->isPlaying = true;
	data->isEndOfStream = false;
	data->loop = loop;

	data->decodeThread = std::thread(&AudioPlayer::DecodeThread, this, data);
}

void AudioPlayer::Stop(AudioHandle handle) {
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
			data->cv.wait(lock);
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