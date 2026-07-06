#include "Log.h"
#include "../PathUtility.h"
#include <fstream>
#include <iostream>
#include <Windows.h>

const std::string kLogFileName = "GameLog.txt";
static std::ofstream ofs;

// --- 静的変数の実体定義 (初期値は false) ---
bool Log::isFileEnabled_ = false;

void Log::WriteInternal(LogLevel level, const std::string& file, int line, const std::string& message) {

    // ファイルパスからファイル名を取得
    std::string filename = cg2::path::ToUtf8String(std::filesystem::path(file).filename());

    // ログの整形
    std::string levelTag = (level == LogLevel::Error) ? "[ERROR]" : (level == LogLevel::Warning) ? "[WARN] " : "[INFO] ";
    std::string formattedLog = std::format("{}({}): {}{}\\n", filename, line, levelTag, message);

    // 1. ファイル出力が有効な場合のみ書き出し
    if (isFileEnabled_) {
        if (!ofs.is_open()) {
            ofs.open(kLogFileName, std::ios::out);
        }
        if (ofs.is_open()) {
            ofs << formattedLog;
            ofs.flush(); // 即座に書き出し
        }
    }

    // 2. Visual Studioの出力ウィンドウへ 
    OutputDebugStringA(formattedLog.c_str());

    // 3. 標準出力へ
    std::cout << formattedLog;
}
