#include "Log.h"
#include <fstream>
#include <iostream>
#include <Windows.h> 

// ログファイルの保存先
const std::string kLogFileName = "GameLog.txt";

// ファイルストリーム
static std::ofstream ofs;

void Log::WriteInternal(LogLevel level, const std::string& file, int line, const std::string& message) {

    // 1. ファイルがまだ開いていなければ開く 
    if (!ofs.is_open()) {
        // ios::out で開く 
        ofs.open(kLogFileName, std::ios::out);
    }

    // 2. ファイルパスから「ファイル名だけ」を取り出す
    // (フルパスだと長すぎて見づらいため)
    std::filesystem::path p(file);
    std::string filename = p.filename().string();

    // 3. レベルに応じた装飾 (タグと色)
    std::string levelTag;
    WORD consoleColor = 0x07; // デフォルト(白)

    switch (level) {
    case LogLevel::Info:
        levelTag = "[INFO] ";
        consoleColor = 0x0F; // 明るい白
        break;
    case LogLevel::Warning:
        levelTag = "[WARN] ";
        consoleColor = 0x0E; // 黄色
        break;
    case LogLevel::Error:
        levelTag = "[ERROR]";
        consoleColor = 0x0C; // 赤
        break;
    }

    // 4. 出力文字列の作成
    // Visual Studioの形式に合わせる: "Filename(Line): Message"
    // これにすると、出力ウィンドウでダブルクリックした時にその行にジャンプできます！
    std::string formattedLog = std::format("{}({}): {}{}\n", filename, line, levelTag, message);


    // 5. テキストファイルへ書き込み
    if (ofs.is_open()) {
        ofs << formattedLog;
        // クラッシュ時にログが欠けないようにフラッシュする
        ofs.flush();
    }

    // 6. Visual Studio の「出力ウィンドウ」へ書き込み
    OutputDebugStringA(formattedLog.c_str());

    // 7. (もしあれば) コンソールウィンドウへ色付きで出力
    // コンソールが無い環境では無視されます
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, consoleColor); // 色変更
        std::cout << formattedLog; // 出力
        SetConsoleTextAttribute(hConsole, 0x07); // 色を戻す
    }
}