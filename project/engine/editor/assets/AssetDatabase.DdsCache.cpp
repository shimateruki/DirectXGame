#define NOMINMAX
#include "AssetDatabase.h"
#include "AssetDatabase.Internal.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

using namespace AssetDatabaseInternal;

namespace {
std::uintmax_t gDdsNotificationSizeBeforeBuild = 0;
}

bool AssetDatabase::StartDdsCacheBuild() {
    // DDS 変換をエディタープロセスから分離して起動します。
    CloseDdsCacheProcessHandle();

    const fs::path scriptPath = (projectRoot_ / "tools/dds_cache/dds_cache_builder.ps1").lexically_normal();
    std::error_code error;

    if (!fs::exists(scriptPath, error) || error) {
        ddsCacheBuildState_ = DdsCacheBuildState::Failed;
        ddsCacheBuildMessage_ = "DDS変換スクリプトが見つかりません。";
        lastDdsCacheBuildExitCode_ = ERROR_FILE_NOT_FOUND;
        return false;
    }

    const fs::path notificationPath = resourcesRoot_ / ".cache/dds_cache_notifications.jsonl";
    // 前回分の通知を今回の変換結果と取り違えないよう、開始前の末尾位置を控える。
    gDdsNotificationSizeBeforeBuild = 0;

    if (fs::exists(notificationPath, error) && !error) {
        gDdsNotificationSizeBeforeBuild = fs::file_size(notificationPath, error);

        if (error) {
            gDdsNotificationSizeBeforeBuild = 0;
            error.clear();
        }
    }

    std::wstring commandLine =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
        scriptPath.wstring() + L"\" -Root \"" + resourcesRoot_.wstring() + L"\"";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo{};
    // CreateProcessWはコマンドラインを書き換えるため、変更可能な文字列を渡す。
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        projectRoot_.c_str(),
        &startupInfo,
        &processInfo);
    if (!created) {
        lastDdsCacheBuildExitCode_ = GetLastError();
        ddsCacheBuildState_ = DdsCacheBuildState::Failed;
        ddsCacheBuildMessage_ = "DDS変換プロセスを開始できませんでした。";
        return false;
    }

    CloseHandle(processInfo.hThread);
    ddsCacheProcessHandle_ = processInfo.hProcess;
    lastDdsCacheBuildExitCode_ = STILL_ACTIVE;
    ddsCacheBuildState_ = DdsCacheBuildState::Running;
    ddsCacheBuildMessage_ = "未変換・更新済みでない画像をバックグラウンドで確認しています。";
    return true;
}

bool AssetDatabase::PollDdsCacheBuild() {
    // 終了するまで待機せず、Update() から状態だけを回収します。
    if (ddsCacheBuildState_ != DdsCacheBuildState::Running || !ddsCacheProcessHandle_) {
        return false;
    }

    HANDLE process = static_cast<HANDLE>(ddsCacheProcessHandle_);
    const DWORD waitResult = WaitForSingleObject(process, 0);

    if (waitResult == WAIT_TIMEOUT) {
        return false;
    }

    DWORD exitCode = 1;

    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(process, &exitCode);
    }
    else {
        exitCode = GetLastError();
    }

    lastDdsCacheBuildExitCode_ = exitCode;
    CloseDdsCacheProcessHandle();

    // 変換通知が増えた場合だけ、追加されたDDSをAsset Databaseへ取り込む。
    // 全DDSが最新だった場合の無駄な二回目の全索引を避ける。
    const fs::path notificationPath = resourcesRoot_ / ".cache/dds_cache_notifications.jsonl";
    std::error_code error;
    std::uintmax_t notificationSizeAfterBuild = 0;

    if (fs::exists(notificationPath, error) && !error) {
        notificationSizeAfterBuild = fs::file_size(notificationPath, error);

        if (error) {
            notificationSizeAfterBuild = 0;
        }
    }

    const bool generatedDds = notificationSizeAfterBuild > gDdsNotificationSizeBeforeBuild;
    refreshAfterDdsCacheBuild_ = generatedDds;

    if (waitResult == WAIT_OBJECT_0 && exitCode == 0) {
        ddsCacheBuildState_ = DdsCacheBuildState::Succeeded;
        ddsCacheBuildMessage_ = generatedDds
            ? "DDSキャッシュの確認・変換が完了しました。"
            : "DDSキャッシュは最新です。再索引は不要です。";
    }
    else {
        ddsCacheBuildState_ = DdsCacheBuildState::Failed;
        ddsCacheBuildMessage_ = "DDS変換に失敗しました。Exit Code: " + std::to_string(exitCode);
    }

    return true;
}

void AssetDatabase::CloseDdsCacheProcessHandle() {
    // 終了済みの変換プロセスのハンドルだけ閉じ、重複解放を避ける。
    if (!ddsCacheProcessHandle_) {
        return;
    }

    CloseHandle(static_cast<HANDLE>(ddsCacheProcessHandle_));
    ddsCacheProcessHandle_ = nullptr;
}
