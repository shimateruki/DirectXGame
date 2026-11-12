#pragma once
#include <string>
#include <format>
#include <filesystem>

/// <summary>ログの重大度。</summary>
enum class LogLevel {
    Info, Warning, Error
};

// 担当：メッセージと発生位置を整形してログへ出力する。
// 関連：DebugConsole（表示）、呼出元のファイル名・行番号（原因追跡）
class Log {
public:

    /// <summary>ファイルへのログ保存を有効または無効にする。</summary>
    static void SetFileEnabled(bool enabled) { isFileEnabled_ = enabled; }

    /// <summary>整形済みメッセージを重大度と発生位置付きで出力する。</summary>
    static void WriteInternal(LogLevel level, const std::string& file, int line, const std::string& message);

    /// <summary>書式と引数からメッセージを作り、重大度と発生位置付きで出力する。</summary>
    template<typename... Args>
    // 発生位置と書式付きメッセージを、指定の重要度でログへ出す。
    static void Write(LogLevel level, const std::string& file, int line, const std::string& formatStr, Args&&... args) {
        std::string message = std::vformat(formatStr, std::make_format_args(args...));
        WriteInternal(level, file, line, message);
    }

private:
    static bool isFileEnabled_; // ファイル出力フラグ
};

#define LOG(...) Log::Write(LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) Log::Write(LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Log::Write(LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
