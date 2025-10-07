#pragma once // ヘッダーファイルの多重インクルードを防止する

#include <xaudio2.h>   // XAudio2 APIを使用するためのヘッダー
#include <fstream>     // ファイル入出力 (WAVファイル読み込み) のためのヘッダー
#include <cassert>     // assertマクロを使用するためのヘッダー
#include <wrl.h>       // Windows Runtime C++ Template Library (ComPtrなどに使用)

/// <summary>
/// 読み込んだサウンドデータを格納する構造体
/// </summary>
struct SoundData
{
	WAVEFORMATEX wfex;      // 波形フォーマット情報
	BYTE* pBuffer;          // オーディオデータが格納されているバッファへのポインタ
	unsigned int bufferSize;  // オーディオデータのサイズ (バイト単位)
};

// --- WAVファイルチャンク構造体 ---
// WAVファイルはRIFF(Resource Interchange File Format)という形式で、
// チャンクと呼ばれるデータブロックの集まりで構成されている

/// <summary>
/// チャンクのヘッダー情報
/// </summary>
struct ChunkHeader
{
	char id[4];     // チャンクのID (例: "RIFF", "fmt ", "data")
	int32_t size;   // チャンクのデータ部分のサイズ
};

/// <summary>
/// RIFFヘッダーチャンク
/// </summary>
struct RiffHeader
{
	ChunkHeader chunk; // "RIFF"
	char type[4];      // ファイルタイプ ("WAVE")
};

/// <summary>
/// フォーマットチャンク
/// </summary>
struct FormatChunk
{
	ChunkHeader chunk;  // "fmt "
	WAVEFORMATEX format; // 波形フォーマット情報
};


/// <summary>
/// オーディオの再生を管理するクラス
/// </summary>
class AudioPlayer
{
public:

	/// <summary>
	/// WAVファイルを読み込み、SoundData構造体に格納する
	/// </summary>
	/// <param name="filename">読み込むWAVファイル名</param>
	/// <returns>読み込んだサウンドデータ</returns>
	SoundData SoundLoadWave(const char* filename);

	/// <summary>
	/// SoundLoadWaveで確保されたメモリを解放する
	/// </summary>
	/// <param name="soundData">解放するサウンドデータへのポインタ</param>
	void SoundUnload(SoundData* soundData);

	/// <summary>
	/// 読み込んだサウンドデータを再生する
	/// </summary>
	/// <param name="xAudio2">XAudio2のインターフェース</param>
	/// <param name="soundData">再生するサウンドデータ</param>
	void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData);

private:
	// プライベートなメンバ変数や関数 (現在は空)
};