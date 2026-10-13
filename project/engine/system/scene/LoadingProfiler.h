#pragma once

#include <Windows.h>
#include <fstream>
#include <mutex>
#include <string>

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
