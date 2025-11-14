#include "Log.h"
#include <fstream>   
#include <Windows.h> 

// .cpp ファイル内だけで使う、静的なファイルストリーム変数
static std::ofstream ofs;

/// <summary>
/// ログ書き出し
/// </summary>
void Log::Write(const std::string& message) {


    if (!ofs.is_open()) {
        ofs.open("Log.txt", std::ios::out);
    }

    // ファイルに "メッセージ" + "改行" を書き込む
    ofs << message << std::endl;

    // Visual Studio の出力ウィンドウにも "メッセージ" + "改行" を出す
    OutputDebugStringA(message.c_str());
    OutputDebugStringA("\n");
}