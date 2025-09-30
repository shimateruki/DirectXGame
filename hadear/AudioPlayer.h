#pragma once
#include <xaudio2.h>
#include <fstream>
#include <cassert>
#include <wrl.h>
struct SoundData
{
	WAVEFORMATEX wfex;
	BYTE* pBuffer;
	unsigned int bufferSize;
};

struct ChunkHeader
{
	char id[4];
	int32_t size;
};

struct RiffHeader
{
	ChunkHeader chunk;
	char type[4];
};

struct FormatChunk
{
	ChunkHeader chunk;
	WAVEFORMATEX format;
};

class AudioPlayer
{
public:
	
	SoundData SoundLoadWave(const char* filename);
	void SoundUnload(SoundData* soundData);
	void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData);



private:

};

