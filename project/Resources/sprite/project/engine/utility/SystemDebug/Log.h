#pragma once
#include <string>
#include <format> // C++20 (Visual Studio 2019/2022なら使えます)
#include <filesystem>

// ログの重要度レベル
enum class LogLevel {
    Info,       // 通常 (白)
    Warning,    // 警告 (黄)
    Error       // エラー (赤)
};

class Log {
public:
    // 実際にログを出力する内部関数
    static void WriteInternal(LogLevel level, const std::string& file, int line, const std::string& message);

    // フォーマット出力用テンプレート関数
    // 例: Log::Write(LogLevel::Info, __FILE__, __LINE__, "Pos: {}, {}", x, y);
    template<typename... Args>
    static void Write(LogLevel level, const std::string& file, int line, const std::string& formatStr, Args&&... args) {
        // C++20 std::format を使用して文字列を整形
        std::string message = std::vformat(formatStr, std::make_format_args(args...));
        WriteInternal(level, file, line, message);
    }
};

// ========================================================================
//  ★ マクロ定義
//  これをコード内で使います。__FILE__ と __LINE__ を自動で渡します。
// ========================================================================

// 通常ログ: LOG("Hello {}", name);
#define LOG(...) Log::Write(LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)

// 警告ログ: LOG_WARN("HP is low: {}", hp);
#define LOG_WARN(...) Log::Write(LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)

// エラーログ: LOG_ERROR("Failed to load: {}", filename);
#define LOG_ERROR(...) Log::Write(LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)