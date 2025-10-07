#include "AudioPlayer.h"

// WAVファイルを読み込む関数
SoundData AudioPlayer::SoundLoadWave(const char* filename)
{
	// ファイルをバイナリモードで開く
	std::ifstream file;
	file.open(filename, std::ios::binary);
	assert(file.is_open()); // ファイルオープン失敗をチェック

	// --- RIFFヘッダーの読み込み ---
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));
	// ファイルがRIFF形式かチェック
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0)
	{
		assert(0); // エラー
	}
	// ファイルがWAVE形式かチェック
	if (strncmp(riff.type, "WAVE", 4) != 0)
	{
		assert(0); // エラー
	}

	// --- フォーマット("fmt ")チャンクの読み込み ---
	FormatChunk format = {};
	// チャンクヘッダーを読み込む
	file.read((char*)&format, sizeof(ChunkHeader));
	// "fmt "チャンクかチェック
	if (strncmp(format.chunk.id, "fmt ", 4) != 0)
	{
		assert(0); // エラー
	}
	// チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.format)); // チャンクサイズが構造体のサイズを超えていないかチェック
	file.read((char*)&format.format, format.chunk.size);

	// --- データ("data")チャンクの読み込み ---
	ChunkHeader data = {};
	file.read((char*)&data, sizeof(data));
	// "JUNK"チャンクをスキップする処理 (一部のオーディオソフトが挿入することがある)
	if (strncmp(data.id, "JUNK", 4) == 0)
	{
		// JUNKチャンクのサイズ分、ファイルポインタを進める
		file.seekg(data.size, std::ios_base::cur);
		// 再度次のチャンクヘッダーを読み込む
		file.read((char*)&data, sizeof(data));
	}
	// "data"チャンクかチェック
	if (strncmp(data.id, "data", 4) != 0)
	{
		assert(0); // エラー
	}

	// --- 音声データの読み込み ---
	// データサイズ分のメモリを動的に確保
	char* pBuffer = new char[data.size];
	// ファイルから音声データを読み込む
	file.read(pBuffer, data.size);

	// ファイルを閉じる
	file.close();

	// 読み込んだデータをSoundData構造体に格納して返す
	SoundData soundData = {};
	soundData.wfex = format.format; // 波形フォーマット
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer); // 音声データバッファ
	soundData.bufferSize = data.size; // バッファサイズ

	return soundData;
}

// SoundLoadWaveで確保したメモリを解放する関数
void AudioPlayer::SoundUnload(SoundData* soundData)
{
	// 動的に確保したメモリを解放
	delete[] soundData->pBuffer;

	// 各メンバを初期化して無効な状態にする
	soundData->pBuffer = 0;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}

// WAV音声を再生する関数
void AudioPlayer::SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData)
{
	HRESULT result;

	// --- ソースボイスの作成 ---
	// ソースボイスは、再生する音の元となるオブジェクト
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result)); // 作成成功をチェック

	// --- オーディオバッファの作成と送信 ---
	// 再生する音声データを設定する
	XAUDIO2_BUFFER buffer = {};
	buffer.pAudioData = soundData.pBuffer;    // 音声データのポインタ
	buffer.AudioBytes = soundData.bufferSize; // 音声データのサイズ (バイト単位)
	buffer.Flags = XAUDIO2_END_OF_STREAM;     // これが最後のデータであることを示す

	// 作成したバッファをソースボイスに送信
	result = pSourceVoice->SubmitSourceBuffer(&buffer);
	assert(SUCCEEDED(result));

	// --- 再生の開始 ---
	result = pSourceVoice->Start(); // 音声の再生を開始
	assert(SUCCEEDED(result));



}