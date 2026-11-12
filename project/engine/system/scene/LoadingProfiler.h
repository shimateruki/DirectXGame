#pragma once

#include <Windows.h>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

// 16msを超えたロード処理だけを記録する。環境変数を指定した検証実行ではファイルにも残す。
inline void LogSlowLoadingOperation(const std::string& operation, double elapsedMs) {
    if (elapsedMs < 16.0) {
        return;
    }
    const std::string line = "[LoadingProfile] " + operation + ": " +
                             std::to_string(elapsedMs) + " ms\n";
    OutputDebugStringA(line.c_str());

    char path[2048]{};
    const DWORD length = GetEnvironmentVariableA("CG2_LOADING_PROFILE_LOG", path, sizeof(path));
    if (length == 0 || length >= sizeof(path)) {
        return;
    }
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream log(path, std::ios::app);
    log << line;
}

// 起動開始から初回表示までの時間を、既存のロード計測ログへ記録する。
inline std::chrono::steady_clock::time_point& StartupProfileStartTime() {
    static auto startTime = std::chrono::steady_clock::now();
    return startTime;
}

// 起動時間の計測基準を現在時刻へ戻す。
inline void BeginStartupProfile() {
    StartupProfileStartTime() = std::chrono::steady_clock::now();
}

// 起動基準からの経過ミリ秒を、指定の区切り名で記録する。
inline void LogStartupProfileCheckpoint(const std::string& checkpoint) {
    const double elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - StartupProfileStartTime()).count();
    LogSlowLoadingOperation("Startup / " + checkpoint + " (elapsed)", elapsedMs);
}

// 担当：スコープの処理時間を測り、起動・ロード計測ログへ記録する。
// 関連：LogSlowLoadingOperation（出力）、BeginStartupProfile（起動計測の基準）
// 主な入口：StartupProfileScope / ~StartupProfileScope
// 16ms未満は既存のログ出力方針に従い省略する。
class StartupProfileScope {
public:
    explicit StartupProfileScope(std::string operation)
        : operation_(std::move(operation)), start_(std::chrono::steady_clock::now()) {}

    ~StartupProfileScope() {
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count();
        LogSlowLoadingOperation("Startup / " + operation_, elapsedMs);
    }

private:
    std::string operation_;
    std::chrono::steady_clock::time_point start_;
};
