#include "AudioPlayer.h"

SoundData AudioPlayer::SoundLoadWave(const char* filename)
{
	//ファイルオープン
	std::ifstream file;
	file.open(filename, std::ios::binary);
	assert(file.is_open());
	//wavデータ読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0)
	{
		assert(0);
	}
	if (strncmp(riff.type, "WAVE", 4) != 0)
	{
		assert(0);
	}
	//fmtチャンク読み込み
	FormatChunk format = {};
	file.read((char*)&format, sizeof(ChunkHeader));
	if (strncmp(format.chunk.id, "fmt ", 4) != 0)
	{
		assert(0);
	}
	//チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.format));
	file.read((char*)&format.format, format.chunk.size);
	//dataチャンクの読み込み
	ChunkHeader data = {};
	file.read((char*)&data, sizeof(data));
	if (strncmp(data.id, "JUNK", 4) == 0)
	{

		file.seekg(data.size, std::ios_base::cur);
		file.read((char*)&data, sizeof(data));
	}
	if (strncmp(data.id, "data", 4) != 0)
	{
		assert(0);
	}
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	file.close();
	SoundData soundData = {};
	soundData.wfex = format.format;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;
	return soundData;
}

void AudioPlayer::SoundUnload(SoundData* soundData)
{
	delete[] soundData->pBuffer;
	soundData->pBuffer = 0;

	soundData->bufferSize = 0;
	soundData->wfex = {};
}

void AudioPlayer::SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData)
{

	HRESULT result;
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);

	XAUDIO2_BUFFER buffer = {};
	buffer.pAudioData = soundData.pBuffer; // 音声データのポインタ
	buffer.AudioBytes = soundData.bufferSize; // 音声データのサイズ
	buffer.Flags = XAUDIO2_END_OF_STREAM; // ストリームの終端を示すフラグ

	result = pSourceVoice->SubmitSourceBuffer(&buffer);
	result = pSourceVoice->Start(); // 音声の再生を開始
}
