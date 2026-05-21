#pragma once
#include <string>
#include <format>
#include <filesystem>

enum class LogLevel {
    Info, Warning, Error
};

class Log {
public:

    static void SetFileEnabled(bool enabled) { isFileEnabled_ = enabled; }

    static void WriteInternal(LogLevel level, const std::string& file, int line, const std::string& message);

    template<typename... Args>
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