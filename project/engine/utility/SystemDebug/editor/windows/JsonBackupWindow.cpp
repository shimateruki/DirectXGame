#define NOMINMAX
#include "JsonBackupWindow.h"

#include "DebugConsole.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr const char* kReportPath = "Resources/.cache/json_backup/latest_report.json";
constexpr const char* kToolPath = "tools/json_backup/json_backup_watcher.ps1";

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::string QuoteCommandArg(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"') {
            escaped += "\\\"";
        }
        else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('"');
    return escaped;
}

bool RunHiddenProcessAndWait(const std::string& commandLine, DWORD* exitCode) {
    std::wstring command = Utf8ToWide(commandLine);
    if (command.empty()) {
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    if (!created) {
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(processInfo.hProcess, &code);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (exitCode) {
        *exitCode = code;
    }
    return true;
}

bool RunHiddenProcessDetached(const std::string& commandLine) {
    std::wstring command = Utf8ToWide(commandLine);
    if (command.empty()) {
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    if (!created) {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool ReadJsonFile(const fs::path& path, nlohmann::json& outJson) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    try {
        file >> outJson;
        return outJson.is_object();
    }
    catch (...) {
        return false;
    }
}

std::string JsonString(const nlohmann::json& item, const char* key, const std::string& fallback = "") {
    if (!item.is_object() || !item.contains(key)) {
        return fallback;
    }

    const auto& value = item.at(key);
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<std::int64_t>());
    }
    if (value.is_number_float()) {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%.2f", value.get<double>());
        return buffer;
    }
    return fallback;
}

int JsonInt(const nlohmann::json& item, const char* key) {
    if (!item.is_object() || !item.contains(key)) {
        return 0;
    }

    const auto& value = item.at(key);
    if (value.is_number_integer()) {
        return static_cast<int>(value.get<std::int64_t>());
    }
    if (value.is_number_float()) {
        return static_cast<int>(value.get<double>());
    }
    return 0;
}

std::string FormatSize(std::int64_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB" };
    double value = static_cast<double>((std::max)(std::int64_t{ 0 }, bytes));
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 3) {
        value /= 1024.0;
        ++unitIndex;
    }

    char buffer[64]{};
    if (unitIndex == 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld %s", static_cast<long long>(bytes), units[unitIndex]);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unitIndex]);
    }
    return buffer;
}

std::int64_t JsonInt64(const nlohmann::json& item, const char* key) {
    if (!item.is_object() || !item.contains(key)) {
        return 0;
    }
    const auto& value = item.at(key);
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number_float()) {
        return static_cast<std::int64_t>(value.get<double>());
    }
    return 0;
}

std::string BuildToolCommand(bool watch) {
    std::ostringstream command;
    command << "powershell.exe -NoProfile -ExecutionPolicy Bypass -File "
            << QuoteCommandArg(kToolPath);
    if (watch) {
        command << " -Watch -Interval 3";
    }
    else {
        command << " -Once";
    }
    return command.str();
}

} // namespace

void JsonBackupWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    LoadLatestReport();

    if (!watcherStartAttempted_) {
        watcherStartAttempted_ = true;
        StartWatcher(true);
    }
}

void JsonBackupWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_SAVE " JSON Backup");
    ImGui::TextWrapped("Resources/json 配下のJSON変更を外部PowerShellツールで検知し、Resources/.backup/json に世代バックアップします。");
    ImGui::TextDisabled("クラッシュで保存データが壊れた時は、バックアップフォルダから直近のJSONを戻せます。");
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_SAVE " 今すぐバックアップ")) {
        RunBackupOnce();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLAY " 監視開始")) {
        StartWatcher(false);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " レポート再読み込み")) {
        if (LoadLatestReport()) {
            lastStatus_ = "JSONバックアップレポートを読み込みました。";
        }
        else {
            lastStatus_ = "JSONバックアップレポートがまだありません。";
        }
    }

    ImVec4 statusColor = hasReport_ ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f) : ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
    ImGui::TextColored(statusColor, "%s", lastStatus_.c_str());

    if (!hasReport_) {
        ImGui::TextDisabled("まだレポートがありません。まずは手動バックアップか監視開始を実行してください。");
        return;
    }

    DrawSummary();
    DrawRecentBackups();
#endif
}

bool JsonBackupWindow::RunBackupOnce() {
    DWORD exitCode = 1;
    const bool ok = RunHiddenProcessAndWait(BuildToolCommand(false), &exitCode);
    LoadLatestReport();
    if (!ok) {
        lastStatus_ = "JSONバックアップツールを起動できませんでした。tools/json_backup/json_backup_watcher.ps1 を確認してください。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return false;
    }
    if (exitCode != 0) {
        lastStatus_ = "JSONバックアップツールがエラー終了しました。レポートを確認してください。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return false;
    }

    const auto& summary = latestReport_.value("summary", nlohmann::json::object());
    lastStatus_ = "JSONバックアップ完了: " + std::to_string(JsonInt(summary, "backedUp")) + "件保存しました。";
    DebugConsole::GetInstance()->AddLog(lastStatus_);
    return true;
}

bool JsonBackupWindow::StartWatcher(bool quiet) {
    const bool ok = RunHiddenProcessDetached(BuildToolCommand(true));
    LoadLatestReport();
    if (!ok) {
        lastStatus_ = "JSONバックアップ監視を開始できませんでした。";
        if (!quiet) {
            DebugConsole::GetInstance()->AddLog(lastStatus_);
        }
        return false;
    }

    lastStatus_ = "JSONバックアップ監視をバックグラウンドで開始しました。";
    if (!quiet) {
        DebugConsole::GetInstance()->AddLog(lastStatus_);
    }
    return true;
}

bool JsonBackupWindow::LoadLatestReport() {
    nlohmann::json report;
    if (!ReadJsonFile(kReportPath, report)) {
        hasReport_ = false;
        return false;
    }

    latestReport_ = std::move(report);
    hasReport_ = true;
    return true;
}

void JsonBackupWindow::DrawSummary() {
#ifdef USE_IMGUI
    const auto& summary = latestReport_.value("summary", nlohmann::json::object());
    ImGui::Separator();
    ImGui::Text("生成時刻: %s", JsonString(latestReport_, "generatedAt", "-").c_str());

    if (latestReport_.value("alreadyRunning", false)) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "監視プロセスは既に起動しています。");
    }

    if (ImGui::BeginTable("JsonBackupSummary", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Scanned");
        ImGui::TableSetupColumn("Backed Up");
        ImGui::TableSetupColumn("Unchanged");
        ImGui::TableSetupColumn("Errors");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%d", JsonInt(summary, "scanned"));
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", JsonInt(summary, "backedUp"));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d", JsonInt(summary, "unchanged"));
        ImGui::TableSetColumnIndex(3);
        const int errors = JsonInt(summary, "errors");
        if (errors > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%d", errors);
        }
        else {
            ImGui::Text("%d", errors);
        }
        ImGui::EndTable();
    }
#endif
}

void JsonBackupWindow::DrawRecentBackups() {
#ifdef USE_IMGUI
    ImGui::Separator();
    ImGui::Text("直近のバックアップ");

    if (!latestReport_.contains("backups") || !latestReport_.at("backups").is_array() || latestReport_.at("backups").empty()) {
        ImGui::TextDisabled("今回のスキャンでは変更されたJSONはありません。");
        return;
    }

    if (ImGui::BeginTable("JsonBackupEntries", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Reason");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Backup");
        ImGui::TableHeadersRow();

        for (const auto& entry : latestReport_.at("backups")) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", JsonString(entry, "source", "-").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", JsonString(entry, "reason", "-").c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", FormatSize(JsonInt64(entry, "size")).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", JsonString(entry, "backup", "-").c_str());
        }
        ImGui::EndTable();
    }

    if (latestReport_.contains("errors") && latestReport_.at("errors").is_array() && !latestReport_.at("errors").empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " エラー");
        for (const auto& error : latestReport_.at("errors")) {
            ImGui::BulletText("%s: %s",
                JsonString(error, "path", "-").c_str(),
                JsonString(error, "message", "-").c_str());
        }
    }
#endif
}
