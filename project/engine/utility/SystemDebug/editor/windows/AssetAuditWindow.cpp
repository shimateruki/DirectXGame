#define NOMINMAX
#include "AssetAuditWindow.h"

#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr const char* kReportPath = "Resources/.cache/asset_audit/latest_report.json";

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::wstring ToLowerWide(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

std::string NormalizeSlash(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

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
        } else {
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

bool ReadJsonFile(const fs::path& path, nlohmann::json& outJson) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    try {
        file >> outJson;
        return true;
    } catch (...) {
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

std::string FormatSizeText(std::int64_t bytes) {
    static constexpr const char* kUnits[] = { "B", "KB", "MB", "GB" };
    double value = static_cast<double>(std::max<std::int64_t>(0, bytes));
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 3) {
        value /= 1024.0;
        ++unitIndex;
    }

    std::ostringstream stream;
    if (unitIndex == 0) {
        stream << bytes << " " << kUnits[unitIndex];
    } else {
        stream << std::fixed << std::setprecision(1) << value << " " << kUnits[unitIndex];
    }
    return stream.str();
}

int JsonInt(const nlohmann::json& item, const char* key) {
    return static_cast<int>(JsonInt64(item, key));
}

std::string JoinStringArray(const nlohmann::json& arrayValue, const char* fallback = "-") {
    if (!arrayValue.is_array() || arrayValue.empty()) {
        return fallback;
    }

    std::ostringstream stream;
    bool first = true;
    for (const auto& value : arrayValue) {
        if (!value.is_string()) continue;
        if (!first) stream << ", ";
        stream << value.get<std::string>();
        first = false;
    }
    const std::string result = stream.str();
    return result.empty() ? fallback : result;
}

const nlohmann::json& ArrayOrEmpty(const nlohmann::json& root, const char* key) {
    static const nlohmann::json empty = nlohmann::json::array();
    if (!root.is_object() || !root.contains(key) || !root.at(key).is_array()) {
        return empty;
    }
    return root.at(key);
}

std::string TimestampText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return stream.str();
}

bool IsPathInside(const fs::path& child, const fs::path& parent) {
    const fs::path childAbs = fs::absolute(child).lexically_normal();
    const fs::path parentAbs = fs::absolute(parent).lexically_normal();

    std::wstring childText = ToLowerWide(childAbs.native());
    std::wstring parentText = ToLowerWide(parentAbs.native());
    if (!parentText.empty() && parentText.back() != L'\\' && parentText.back() != L'/') {
        parentText.push_back(L'\\');
    }
    if (!childText.empty() && childText.back() != L'\\' && childText.back() != L'/') {
        childText.push_back(L'\\');
    }
    return childText.rfind(parentText, 0) == 0;
}

bool IsProtectedAssetPath(const std::string& relativePath) {
    const std::string lower = ToLowerAscii(NormalizeSlash(relativePath));
    if (lower.empty()) return true;
    if (lower.find("..") != std::string::npos) return true;
    if (lower.rfind("resources/", 0) != 0) return true;
    if (lower.rfind("resources/.cache/", 0) == 0) return true;
    if (lower.rfind("resources/.trash/", 0) == 0) return true;
    return false;
}

std::string RelativeToProjectSlash(const fs::path& fullPath) {
    std::error_code ec;
    fs::path relative = fs::relative(fullPath, fs::current_path(), ec);
    if (ec) {
        relative = fullPath;
    }
    return NormalizeSlash(relative.generic_string());
}

fs::path UniqueTrashPath(const fs::path& targetPath) {
    if (!fs::exists(targetPath)) {
        return targetPath;
    }

    const fs::path parent = targetPath.parent_path();
    const std::string stem = targetPath.stem().generic_string();
    const std::string extension = targetPath.extension().generic_string();
    for (int index = 1; index < 1000; ++index) {
        fs::path candidate = parent / (stem + "_" + std::to_string(index) + extension);
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }
    return parent / (stem + "_overflow" + extension);
}

void AddIfDeletable(std::vector<fs::path>& targets, const fs::path& path, const fs::path& resourcesRoot) {
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        return;
    }
    if (!IsPathInside(path, resourcesRoot)) {
        return;
    }

    const std::string relative = RelativeToProjectSlash(path);
    if (IsProtectedAssetPath(relative)) {
        return;
    }

    const fs::path normalized = fs::absolute(path).lexically_normal();
    const auto alreadyAdded = std::find_if(targets.begin(), targets.end(), [&](const fs::path& item) {
        return fs::equivalent(item, normalized);
    });
    if (alreadyAdded == targets.end()) {
        targets.push_back(normalized);
    }
}

std::vector<fs::path> BuildDeleteTargets(const fs::path& mainPath, const fs::path& resourcesRoot) {
    std::vector<fs::path> targets;
    AddIfDeletable(targets, mainPath, resourcesRoot);

    const std::string extension = ToLowerAscii(mainPath.extension().generic_string());
    if (extension == ".png") {
        AddIfDeletable(targets, fs::path(mainPath).replace_extension(".dds"), resourcesRoot);
    } else if (extension == ".dds") {
        AddIfDeletable(targets, fs::path(mainPath).replace_extension(".png"), resourcesRoot);
    } else if (extension == ".gltf") {
        AddIfDeletable(targets, fs::path(mainPath).replace_extension(".bin"), resourcesRoot);
    } else if (extension == ".obj") {
        AddIfDeletable(targets, fs::path(mainPath).replace_extension(".mtl"), resourcesRoot);
    }

    return targets;
}

} // namespace

void AssetAuditWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    LoadLatestReport();
}

void AssetAuditWindow::DrawImGui() {
#ifdef USE_IMGUI
    (void)editor_;
    ImGui::Text(ICON_FA_SEARCH " アセット監査 / Heavy Asset Profiler + Unused Asset Scanner");
    ImGui::Separator();
    ImGui::TextWrapped("外部ツール tools/asset_audit.ps1 で Resources を解析し、生成されたJSONをエンジン側で確認します。重い素材と未使用候補を見つけるための作業補助ツールです。");

    if (ImGui::Button(ICON_FA_SEARCH " 監査ツール実行")) {
        RunAuditTool();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " レポート再読み込み")) {
        LoadLatestReport();
    }

    ImGui::TextWrapped("%s", lastStatus_.c_str());

    if (!hasReport_) {
        ImGui::TextDisabled("まだレポートが読み込まれていません。監査ツールを実行してください。");
        return;
    }

    DrawSummary();
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("検索", searchBuffer_, sizeof(searchBuffer_));

    if (ImGui::BeginTabBar("AssetAuditTabs")) {
        if (ImGui::BeginTabItem("重い素材")) {
            DrawHeavyAssets();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("未使用候補")) {
            DrawUnusedAssets();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("欠損参照")) {
            DrawMissingReferences();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    DrawDeleteConfirmPopup();
#endif
}

bool AssetAuditWindow::RunAuditTool() {
    const std::string command =
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
        QuoteCommandArg("tools/asset_audit.ps1");

    DWORD exitCode = 1;
    if (!RunHiddenProcessAndWait(command, &exitCode)) {
        lastStatus_ = "監査ツールの起動に失敗しました。PowerShellまたは tools/asset_audit.ps1 を確認してください。";
        return false;
    }
    if (exitCode != 0) {
        lastStatus_ = "監査ツールがエラー終了しました。tools/asset_audit.ps1 を単体で実行して詳細を確認してください。";
        return false;
    }

    if (!LoadLatestReport()) {
        lastStatus_ = "監査ツールは完了しましたが、レポートJSONを読み込めませんでした。";
        return false;
    }

    lastStatus_ = "監査ツールが完了しました。最新レポートを読み込みました。";
    return true;
}

bool AssetAuditWindow::LoadLatestReport() {
    nlohmann::json report;
    if (!ReadJsonFile(kReportPath, report)) {
        hasReport_ = false;
        lastStatus_ = "レポートが見つからないか、JSONとして読み込めません: " + std::string(kReportPath);
        return false;
    }

    latestReport_ = std::move(report);
    hasReport_ = true;
    const std::string generatedAt = JsonString(latestReport_, "generatedAt", "unknown");
    lastStatus_ = "読み込み済み: " + std::string(kReportPath) + " / " + generatedAt;
    return true;
}

void AssetAuditWindow::DrawSummary() {
#ifdef USE_IMGUI
    const nlohmann::json summary = latestReport_.value("summary", nlohmann::json::object());
    const nlohmann::json thresholds = latestReport_.value("thresholds", nlohmann::json::object());

    if (ImGui::BeginTable("AssetAuditSummary", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        auto row = [](const char* labelA, const std::string& valueA, const char* labelB, const std::string& valueB) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(labelA);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(valueA.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(labelB);
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(valueB.c_str());
        };

        row("総ファイル数", JsonString(summary, "totalFiles"), "総容量", JsonString(summary, "totalSizeText"));
        row("使用検出数", JsonString(summary, "usedFiles"), "未使用候補", JsonString(summary, "unusedFiles") + " / " + JsonString(summary, "unusedSizeText"));
        row("重い素材警告", JsonString(summary, "heavyWarningCount"), "欠損参照", JsonString(summary, "missingReferenceCount"));
        row("Texture閾値", JsonString(thresholds, "heavyTextureMB") + " MB", "Model閾値", JsonString(thresholds, "heavyModelMB") + " MB");

        ImGui::EndTable();
    }
#endif
}

void AssetAuditWindow::DrawHeavyAssets() {
#ifdef USE_IMGUI
    const auto& heavyAssets = ArrayOrEmpty(latestReport_, "heavyAssets");
    ImGui::TextDisabled("警告素材と容量上位の素材を表示します。削除や圧縮はここでは行いません。");

    if (ImGui::BeginTable("HeavyAssetTable", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 360))) {
        ImGui::TableSetupColumn("種別", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("容量", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("幅", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("高さ", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("頂点", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("三角形", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("メモ", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableHeadersRow();

        for (const auto& item : heavyAssets) {
            if (!item.is_object() || !MatchesSearch(item)) continue;

            const bool warning = JsonString(item, "severity") == "warning";
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (warning) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s", JsonString(item, "category").c_str());
            } else {
                ImGui::TextUnformatted(JsonString(item, "category").c_str());
            }
            ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", JsonString(item, "path").c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(JsonString(item, "sizeText").c_str());
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", JsonInt(item, "width"));
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", JsonInt(item, "height"));
            ImGui::TableSetColumnIndex(5); ImGui::Text("%d", JsonInt(item, "vertices"));
            ImGui::TableSetColumnIndex(6); ImGui::Text("%d", JsonInt(item, "triangles"));
            ImGui::TableSetColumnIndex(7); ImGui::TextWrapped("%s", JoinStringArray(item.value("notes", nlohmann::json::array())).c_str());
        }

        ImGui::EndTable();
    }
#endif
}

void AssetAuditWindow::DrawUnusedAssets() {
#ifdef USE_IMGUI
    const auto& unusedAssets = ArrayOrEmpty(latestReport_, "unusedAssets");
    ImGui::TextDisabled("JSONとコードから直接参照を見つけられなかった候補です。削除ボタンは Resources/.trash/asset_audit/ へ退避します。");

    if (ImGui::BeginTable("UnusedAssetTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 360))) {
        ImGui::TableSetupColumn("種別", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("容量", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("理由", ImGuiTableColumnFlags_WidthFixed, 230.0f);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableHeadersRow();

        for (const auto& item : unusedAssets) {
            if (!item.is_object() || !MatchesSearch(item)) continue;

            const std::string path = JsonString(item, "path");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(JsonString(item, "category").c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", path.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(JsonString(item, "sizeText").c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextWrapped("%s", JsonString(item, "reason").c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(path.c_str());
            if (ImGui::Button(ICON_FA_TRASH_ALT " 削除", ImVec2(-1.0f, 0.0f))) {
                pendingDeletePath_ = path;
                ImGui::OpenPopup("AssetAuditDeleteConfirm");
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
#endif
}

void AssetAuditWindow::DrawMissingReferences() {
#ifdef USE_IMGUI
    const auto& missingReferences = ArrayOrEmpty(latestReport_, "missingReferences");
    ImGui::TextDisabled("Resources/ から始まる参照のうち、実ファイルやディレクトリが見つからなかったものです。");

    if (ImGui::BeginTable("MissingReferenceTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 360))) {
        ImGui::TableSetupColumn("参照元", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("参照値", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("候補", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& item : missingReferences) {
            if (!item.is_object() || !MatchesSearch(item)) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("%s", JsonString(item, "source").c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.45f, 1.0f), "%s", JsonString(item, "value").c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextWrapped("%s", JoinStringArray(item.value("expectedCandidates", nlohmann::json::array())).c_str());
        }

        ImGui::EndTable();
    }
#endif
}

void AssetAuditWindow::DrawDeleteConfirmPopup() {
#ifdef USE_IMGUI
    if (ImGui::BeginPopupModal("AssetAuditDeleteConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(ICON_FA_EXCLAMATION_TRIANGLE " アセットを削除しますか？");
        ImGui::Separator();
        ImGui::TextWrapped("%s", pendingDeletePath_.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("完全削除ではなく Resources/.trash/asset_audit/ へ退避します。PNG/DDSやGLTF/BINなどの相方ファイルがある場合は一緒に退避します。");
        ImGui::Spacing();

        if (ImGui::Button(ICON_FA_TRASH_ALT " 削除する", ImVec2(150.0f, 0.0f))) {
            std::vector<std::string> movedPaths;
            std::string errorMessage;
            if (MoveAssetToTrash(pendingDeletePath_, movedPaths, errorMessage)) {
                RemoveMovedAssetsFromReport(movedPaths);
                lastStatus_ = "アセットを退避しました: " + std::to_string(movedPaths.size()) + " 件。必要なら Resources/.trash/asset_audit/ から戻せます。";
            } else {
                lastStatus_ = "アセット削除に失敗しました: " + errorMessage;
            }
            pendingDeletePath_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
            pendingDeletePath_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
#endif
}

bool AssetAuditWindow::MoveAssetToTrash(const std::string& relativePath, std::vector<std::string>& movedPaths, std::string& errorMessage) {
    movedPaths.clear();
    errorMessage.clear();

    const std::string normalizedPath = NormalizeSlash(relativePath);
    if (IsProtectedAssetPath(normalizedPath)) {
        errorMessage = "Resources 配下の通常アセットだけ削除できます。";
        return false;
    }

    const fs::path projectRoot = fs::current_path();
    const fs::path resourcesRoot = projectRoot / "Resources";
    const fs::path mainPath = (projectRoot / fs::path(normalizedPath)).lexically_normal();
    if (!fs::exists(mainPath) || !fs::is_regular_file(mainPath)) {
        errorMessage = "対象ファイルが見つかりません: " + normalizedPath;
        return false;
    }
    if (!IsPathInside(mainPath, resourcesRoot)) {
        errorMessage = "Resources 配下ではないファイルは削除できません。";
        return false;
    }

    std::vector<fs::path> targets = BuildDeleteTargets(mainPath, resourcesRoot);
    if (targets.empty()) {
        errorMessage = "削除できる対象がありません。";
        return false;
    }

    const fs::path trashRoot = projectRoot / "Resources" / ".trash" / "asset_audit" / TimestampText();

    try {
        for (const fs::path& target : targets) {
            const fs::path relativeFromResources = fs::relative(target, resourcesRoot);
            fs::path trashPath = UniqueTrashPath(trashRoot / relativeFromResources);
            fs::create_directories(trashPath.parent_path());
            fs::rename(target, trashPath);
            movedPaths.push_back(RelativeToProjectSlash(target));
        }
    } catch (const std::exception& e) {
        errorMessage = e.what();
        return false;
    }

    return true;
}

void AssetAuditWindow::RemoveMovedAssetsFromReport(const std::vector<std::string>& movedPaths) {
    if (!hasReport_ || !latestReport_.is_object() || movedPaths.empty()) {
        return;
    }

    std::set<std::string> movedSet;
    for (std::string path : movedPaths) {
        movedSet.insert(ToLowerAscii(NormalizeSlash(std::move(path))));
    }

    auto removeFromArray = [&](const char* key) {
        if (!latestReport_.contains(key) || !latestReport_.at(key).is_array()) {
            return;
        }

        auto& array = latestReport_[key];
        array.erase(std::remove_if(array.begin(), array.end(), [&](const nlohmann::json& item) {
            const std::string itemPath = ToLowerAscii(NormalizeSlash(JsonString(item, "path")));
            return movedSet.find(itemPath) != movedSet.end();
        }), array.end());
    };

    removeFromArray("unusedAssets");
    removeFromArray("heavyAssets");

    if (latestReport_.contains("summary") && latestReport_["summary"].is_object() &&
        latestReport_.contains("unusedAssets") && latestReport_["unusedAssets"].is_array()) {
        std::int64_t unusedBytes = 0;
        for (const auto& item : latestReport_["unusedAssets"]) {
            unusedBytes += JsonInt64(item, "sizeBytes");
        }
        latestReport_["summary"]["unusedFiles"] = latestReport_["unusedAssets"].size();
        latestReport_["summary"]["unusedBytes"] = unusedBytes;
        latestReport_["summary"]["unusedSizeText"] = FormatSizeText(unusedBytes);
    }
}

bool AssetAuditWindow::MatchesSearch(const nlohmann::json& item) const {
    if (searchBuffer_[0] == '\0') {
        return true;
    }

    std::string haystack;
    if (item.is_object()) {
        haystack += JsonString(item, "category");
        haystack += " ";
        haystack += JsonString(item, "path");
        haystack += " ";
        haystack += JsonString(item, "source");
        haystack += " ";
        haystack += JsonString(item, "value");
        haystack += " ";
        if (item.contains("notes")) {
            haystack += JoinStringArray(item.at("notes"), "");
        }
    }

    return ToLowerAscii(haystack).find(ToLowerAscii(searchBuffer_)) != std::string::npos;
}
