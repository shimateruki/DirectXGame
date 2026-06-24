#define NOMINMAX
#include "AssetAuditWindow.h"

#include "AudioPlayer.h"
#include "BaseScene.h"
#include "DebugEditor.h"
#include "DebugConsole.h"
#include "EditorManager.h"
#include "EffectPreviewStage.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "SrvManager.h"
#include "TextureManager.h"

#include <Windows.h>
#include <shellapi.h>

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
#include <initializer_list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace {
namespace fs = std::filesystem;

constexpr const char* kReportPath = "Resources/.cache/asset_audit/latest_report.json";
constexpr const char* kMarkdownReportPath = "Resources/.cache/asset_audit/asset_audit_report.md";
constexpr const char* kReportDirectoryPath = "Resources/.cache/asset_audit";
constexpr std::int64_t kMaxInlinePreviewBytes = 2LL * 1024LL * 1024LL;
constexpr int kMaxInlinePreviewDimension = 2048;

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

bool OpenShellPath(const fs::path& path) {
    const fs::path normalized = fs::absolute(path).lexically_normal();
    const std::wstring widePath = normalized.wstring();
    HINSTANCE result = ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<std::intptr_t>(result) > 32;
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

bool HasExtension(const std::string& path, std::initializer_list<const char*> extensions) {
    const std::string ext = ToLowerAscii(fs::path(path).extension().generic_string());
    for (const char* candidate : extensions) {
        if (ext == candidate) {
            return true;
        }
    }
    return false;
}

bool IsTextureAssetPath(const std::string& path) {
    return HasExtension(path, { ".png", ".dds", ".jpg", ".jpeg", ".bmp", ".tga" });
}

bool IsAudioAssetPath(const std::string& path) {
    return HasExtension(path, { ".wav", ".mp3", ".ogg" });
}

bool IsModelAssetPath(const std::string& path) {
    return HasExtension(path, { ".gltf", ".glb", ".obj", ".fbx" });
}

bool IsBgmCategory(const std::string& category, const std::string& path) {
    const std::string lowerCategory = ToLowerAscii(category);
    const std::string lowerPath = ToLowerAscii(NormalizeSlash(path));
    return lowerCategory.find("bgm") != std::string::npos ||
        lowerPath.find("/bgm/") != std::string::npos ||
        lowerPath.find("bgm") != std::string::npos;
}

bool IsSafeInlineTexturePreview(const nlohmann::json& item) {
    const std::int64_t sizeBytes = JsonInt64(item, "sizeBytes");
    const int width = JsonInt(item, "width");
    const int height = JsonInt(item, "height");

    if (sizeBytes > kMaxInlinePreviewBytes) {
        return false;
    }
    if (width > kMaxInlinePreviewDimension || height > kMaxInlinePreviewDimension) {
        return false;
    }
    return true;
}

std::string BuildHeavyAssetDetailText(const nlohmann::json& item) {
    const int width = JsonInt(item, "width");
    const int height = JsonInt(item, "height");
    const int vertices = JsonInt(item, "vertices");
    const int triangles = JsonInt(item, "triangles");

    if (width > 0 || height > 0) {
        return std::to_string(width) + " x " + std::to_string(height);
    }
    if (vertices > 0 || triangles > 0) {
        return "V:" + std::to_string(vertices) + " / T:" + std::to_string(triangles);
    }
    return "-";
}

void DrawSingleLineCellText(const std::string& text) {
    const std::string displayText = text.empty() ? "-" : text;
    ImGui::TextUnformatted(displayText.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(520.0f);
        ImGui::TextUnformatted(displayText.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string GetCategoryLabel(const std::string& category) {
    if (category == "Texture") return "画像";
    if (category == "Model") return "モデル";
    if (category == "ModelData") return "モデルデータ";
    if (category == "Audio-BGM") return "BGM";
    if (category == "Audio-SE") return "SE";
    if (category == "Audio") return "音声";
    return category;
}

std::string GetModelNameFromAssetPath(const std::string& relativePath) {
    std::string path = NormalizeSlash(relativePath);
    const std::string prefix = "Resources/3DModel/";
    if (path.rfind(prefix, 0) == 0) {
        path = path.substr(prefix.size());
    }

    fs::path modelPath = path;
    const fs::path parent = modelPath.parent_path();
    const std::string stem = modelPath.stem().generic_string();
    if (!parent.empty() && parent.filename().generic_string() == stem) {
        return NormalizeSlash(parent.generic_string());
    }

    modelPath.replace_extension();
    return NormalizeSlash(modelPath.generic_string());
}

constexpr const char* kAssetAuditPreviewPrefix = "__Editor_AssetAuditPreview_";

bool IsAssetAuditPreviewObject(const Object3d* object) {
    if (!object) return false;
    return object->GetName().rfind(kAssetAuditPreviewPrefix, 0) == 0 ||
        object->GetClassName() == "EditorOnly_AssetAuditPreview";
}

} // namespace

void AssetAuditWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    hasReport_ = false;
    lastStatus_ = "アセット監査を開きました。前回レポートを確認する場合は、レポート再読み込みを押してください。";
}

void AssetAuditWindow::DrawImGui() {
#ifdef USE_IMGUI
    (void)editor_;
    UpdateAuditProcess();
    ImGui::Text(ICON_FA_SEARCH " アセット監査 / Heavy Asset Profiler + Unused Asset Scanner");
    ImGui::Separator();
    ImGui::TextWrapped("外部ツール tools/asset_audit/asset_audit.ps1 で Resources を解析し、生成されたJSONをエンジン側で確認します。重い素材と未使用候補を見つけるための作業補助ツールです。");

    if (ImGui::Button(ICON_FA_SEARCH " 監査ツール実行")) {
        RunAuditTool();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " レポート再読み込み")) {
        LoadLatestReport();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_ALT " 外部レポート")) {
        OpenExternalPath(kMarkdownReportPath);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " レポートフォルダ")) {
        OpenExternalPath(kReportDirectoryPath);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_EYE_SLASH " モデル確認を消す")) {
        RemoveModelPreviews();
    }

    ImGui::Checkbox("軽い画像だけサムネイル表示", &showPreviewThumbnails_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("大きい画像はゲーム内に読み込まず、外部確認ボタンで開きます。");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragInt("表示上限", &maxRowsToDraw_, 10.0f, 50, 2000);
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

void AssetAuditWindow::UpdateAuditProcess() {
    if (!auditRunning_) {
        return;
    }
    if (!auditFuture_.valid()) {
        auditRunning_ = false;
        return;
    }
    if (auditFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    const std::uint32_t exitCode = auditFuture_.get();
    auditRunning_ = false;
    if (exitCode == 0xFFFFFFFFu) {
        lastStatus_ = "アセット監査ツールの起動に失敗しました。PowerShellまたはtools/asset_audit/asset_audit.ps1を確認してください。";
        return;
    }
    if (exitCode != 0) {
        lastStatus_ = "アセット監査ツールがエラー終了しました。ツール単体で実行して詳細を確認してください。";
        return;
    }
    if (!LoadLatestReport()) {
        lastStatus_ = "監査は完了しましたが、レポートJSONを読み込めませんでした。";
        return;
    }
    lastStatus_ = "アセット監査が完了しました。最新レポートを読み込みました。";
}

bool AssetAuditWindow::RunAuditTool() {
    if (auditRunning_) {
        lastStatus_ = "アセット監査を実行中です。完了まで少し待ってください。";
        return false;
    }

    const std::string toolCommand =
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
        QuoteCommandArg("tools/asset_audit/asset_audit.ps1");

    auditRunning_ = true;
    lastStatus_ = "アセット監査をバックグラウンドで実行しています。";
    auditFuture_ = std::async(std::launch::async, [toolCommand]() -> std::uint32_t {
        DWORD exitCode = 1;
        if (!RunHiddenProcessAndWait(toolCommand, &exitCode)) {
            return 0xFFFFFFFFu;
        }
        return static_cast<std::uint32_t>(exitCode);
    });
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
    int drawnCount = 0;
    bool truncated = false;
    ImGui::TextDisabled("横スクロールできます。パスやメモにマウスを乗せると全文を確認できます。");

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("HeavyAssetTable", 6, tableFlags, ImVec2(0, 420), 1040.0f)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("種別", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthFixed, 380.0f);
        ImGui::TableSetupColumn("容量", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn("詳細", ImGuiTableColumnFlags_WidthFixed, 126.0f);
        ImGui::TableSetupColumn("メモ", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("確認", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        for (const auto& item : heavyAssets) {
            if (!item.is_object() || !MatchesSearch(item)) continue;
            if (drawnCount >= maxRowsToDraw_) {
                truncated = true;
                break;
            }
            ++drawnCount;

            const bool warning = JsonString(item, "severity") == "warning";
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (warning) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s", GetCategoryLabel(JsonString(item, "category")).c_str());
            } else {
                ImGui::TextUnformatted(GetCategoryLabel(JsonString(item, "category")).c_str());
            }
            ImGui::TableSetColumnIndex(1); DrawSingleLineCellText(JsonString(item, "path"));
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(JsonString(item, "sizeText").c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(BuildHeavyAssetDetailText(item).c_str());
            ImGui::TableSetColumnIndex(4); DrawSingleLineCellText(JoinStringArray(item.value("notes", nlohmann::json::array())));
            ImGui::TableSetColumnIndex(5); DrawAssetPreview(item, 54.0f);
        }

        ImGui::EndTable();
    }
    if (truncated) {
        ImGui::TextDisabled("表示上限により %d 件まで表示中です。必要なら表示上限を上げてください。", drawnCount);
    }
#endif
}

void AssetAuditWindow::DrawUnusedAssets() {
#ifdef USE_IMGUI
    const auto& unusedAssets = ArrayOrEmpty(latestReport_, "unusedAssets");
    int drawnCount = 0;
    bool truncated = false;
    ImGui::TextDisabled("JSONとコードから直接参照を見つけられなかった候補です。削除ボタンは Resources/.trash/asset_audit/ へ退避します。");

    if (ImGui::BeginTable("UnusedAssetTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 360))) {
        ImGui::TableSetupColumn("種別", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("容量", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("理由", ImGuiTableColumnFlags_WidthFixed, 230.0f);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn("確認", ImGuiTableColumnFlags_WidthFixed, 118.0f);
        ImGui::TableHeadersRow();

        for (const auto& item : unusedAssets) {
            if (!item.is_object() || !MatchesSearch(item)) continue;
            if (drawnCount >= maxRowsToDraw_) {
                truncated = true;
                break;
            }
            ++drawnCount;

            const std::string path = JsonString(item, "path");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(GetCategoryLabel(JsonString(item, "category")).c_str());
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
            ImGui::TableSetColumnIndex(5); DrawAssetPreview(item, 54.0f);
        }

        ImGui::EndTable();
    }
    if (truncated) {
        ImGui::TextDisabled("表示上限により %d 件まで表示中です。必要なら表示上限を上げてください。", drawnCount);
    }
#endif
}

void AssetAuditWindow::DrawMissingReferences() {
#ifdef USE_IMGUI
    const auto& missingReferences = ArrayOrEmpty(latestReport_, "missingReferences");
    int drawnCount = 0;
    bool truncated = false;
    ImGui::TextDisabled("Resources/ から始まる参照のうち、実ファイルやディレクトリが見つからなかったものです。");

    if (ImGui::BeginTable("MissingReferenceTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 360))) {
        ImGui::TableSetupColumn("参照元", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("参照値", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("候補", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& item : missingReferences) {
            if (!item.is_object() || !MatchesSearch(item)) continue;
            if (drawnCount >= maxRowsToDraw_) {
                truncated = true;
                break;
            }
            ++drawnCount;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("%s", JsonString(item, "source").c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.45f, 1.0f), "%s", JsonString(item, "value").c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextWrapped("%s", JoinStringArray(item.value("expectedCandidates", nlohmann::json::array())).c_str());
        }

        ImGui::EndTable();
    }
    if (truncated) {
        ImGui::TextDisabled("表示上限により %d 件まで表示中です。必要なら表示上限を上げてください。", drawnCount);
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

uint32_t AssetAuditWindow::GetPreviewTextureHandle(const std::string& relativePath) {
    const std::string normalizedPath = NormalizeSlash(relativePath);
    auto it = previewTextureHandles_.find(normalizedPath);
    if (it != previewTextureHandles_.end()) {
        return it->second;
    }

    const fs::path path = (fs::current_path() / fs::path(normalizedPath)).lexically_normal();
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        return 0;
    }

    uint32_t handle = TextureManager::GetInstance()->Load(normalizedPath);
    previewTextureHandles_[normalizedPath] = handle;
    return handle;
}

bool AssetAuditWindow::OpenExternalPath(const std::string& relativePath) {
    const std::string normalizedPath = NormalizeSlash(relativePath);
    const fs::path fullPath = (fs::current_path() / fs::path(normalizedPath)).lexically_normal();
    if (!fs::exists(fullPath)) {
        lastStatus_ = "外部で開く対象が見つかりません: " + normalizedPath;
        return false;
    }

    if (!OpenShellPath(fullPath)) {
        lastStatus_ = "外部アプリで開けませんでした: " + normalizedPath;
        return false;
    }

    lastStatus_ = "外部で開きました: " + normalizedPath;
    return true;
}

bool AssetAuditWindow::PlayAudioPreview(const std::string& relativePath, bool isBgm) {
    const std::string normalizedPath = NormalizeSlash(relativePath);
    const fs::path path = (fs::current_path() / fs::path(normalizedPath)).lexically_normal();
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        lastStatus_ = "音声ファイルが見つかりません: " + normalizedPath;
        return false;
    }

    uint32_t handle = AudioPlayer::kInvalidAudioHandle;
    auto it = previewAudioHandles_.find(normalizedPath);
    if (it != previewAudioHandles_.end()) {
        handle = it->second;
    } else {
        handle = AudioPlayer::GetInstance()->LoadSoundFile(normalizedPath);
        previewAudioHandles_[normalizedPath] = handle;
    }

    if (handle == AudioPlayer::kInvalidAudioHandle) {
        lastStatus_ = "音声の読み込みに失敗しました: " + normalizedPath;
        return false;
    }

    if (isBgm) {
        AudioPlayer::GetInstance()->StopBGM();
        AudioPlayer::GetInstance()->PlayBGM(handle, false, 0.75f);
    } else {
        AudioPlayer::GetInstance()->PlaySE(handle, false, 0.85f);
    }

    lastStatus_ = "音声を試聴しました: " + normalizedPath;
    return true;
}

int AssetAuditWindow::CountModelPreviews() const {
    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        return 0;
    }

    int count = 0;
    for (const auto& object : editor_->GetSceneManager()->GetCurrentScene()->GetObjects()) {
        if (IsAssetAuditPreviewObject(object.get())) {
            ++count;
        }
    }
    return count;
}

void AssetAuditWindow::CreateModelPreview(const std::string& relativePath) {
    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        lastStatus_ = "モデルを確認できるシーンがありません。";
        return;
    }

    const std::string normalizedPath = NormalizeSlash(relativePath);
    const fs::path path = (fs::current_path() / fs::path(normalizedPath)).lexically_normal();
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        lastStatus_ = "モデルファイルが見つかりません: " + normalizedPath;
        return;
    }

    BaseScene* scene = editor_->GetSceneManager()->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) {
        lastStatus_ = "Object3dCommonが取得できません。";
        return;
    }

    Vector3 origin = { 0.0f, 2.0f, 0.0f };
    EffectPreviewStage* stage = EffectPreviewStage::GetInstance();
    if (stage) {
        stage->EnableForToolPreview();
        stage->RequestCameraRecenter();
        origin = stage->GetPreviewPosition();
    }

    const int previewIndex = CountModelPreviews();
    const std::string modelName = GetModelNameFromAssetPath(normalizedPath);
    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    object->SetName(std::string(kAssetAuditPreviewPrefix) + std::to_string(previewIndex) + "_" + modelName);
    object->SetClassName("EditorOnly_AssetAuditPreview");
    object->SetSaveCategory("Object");
    object->SetIsLocked(true);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);
    object->SetModel(modelName);
    object->SetTranslate({
        origin.x + static_cast<float>(previewIndex) * 3.0f,
        origin.y,
        origin.z
    });
    object->SetScale({ 1.0f, 1.0f, 1.0f });
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    Object3d* rawObject = object.get();
    scene->GetObjects().push_back(std::move(object));
    editor_->SetSelectedObject(rawObject);
    EditorManager::GetInstance()->SetSelectedObject(editor_);

    lastStatus_ = "モデル確認用Objectを配置しました: " + modelName;
    DebugConsole::GetInstance()->AddLog(lastStatus_);
}

void AssetAuditWindow::RemoveModelPreviews() {
    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        return;
    }

    BaseScene* scene = editor_->GetSceneManager()->GetCurrentScene();
    std::vector<Object3d*> targets;
    for (auto& object : scene->GetObjects()) {
        if (IsAssetAuditPreviewObject(object.get())) {
            targets.push_back(object.get());
        }
    }

    for (Object3d* object : targets) {
        scene->RequestRemoveObject(object);
    }

    if (!targets.empty()) {
        lastStatus_ = "アセット監査のモデル確認用Objectを削除しました。";
    }
}

void AssetAuditWindow::DrawAssetPreview(const nlohmann::json& item, float size) {
#ifdef USE_IMGUI
    const std::string path = JsonString(item, "path");
    const std::string category = JsonString(item, "category");
    if (path.empty()) {
        ImGui::TextDisabled("-");
        return;
    }

    ImGui::PushID(path.c_str());
    if (IsTextureAssetPath(path)) {
        if (!showPreviewThumbnails_) {
            ImGui::TextDisabled("画像");
            if (ImGui::SmallButton("外部確認")) {
                OpenExternalPath(path);
            }
            ImGui::PopID();
            return;
        }

        if (!IsSafeInlineTexturePreview(item)) {
            ImGui::TextDisabled("大きい画像");
            if (ImGui::SmallButton("外部確認")) {
                OpenExternalPath(path);
            }
            ImGui::PopID();
            return;
        }

        uint32_t handle = GetPreviewTextureHandle(path);
        if (handle != 0) {
            const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(handle);
            ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(size, size));
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(path.c_str());
                ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(180.0f, 180.0f));
                ImGui::EndTooltip();
            }
        } else {
            ImGui::TextDisabled("画像なし");
        }
    } else if (IsAudioAssetPath(path)) {
        const bool isBgm = IsBgmCategory(category, path);
        if (ImGui::Button(isBgm ? "BGM試聴" : "SE試聴", ImVec2(-1.0f, 0.0f))) {
            PlayAudioPreview(path, isBgm);
        }
        if (isBgm) {
            if (ImGui::SmallButton("停止")) {
                AudioPlayer::GetInstance()->StopBGM();
            }
        }
    } else if (IsModelAssetPath(path)) {
        if (ImGui::Button("3D確認", ImVec2(-1.0f, 0.0f))) {
            CreateModelPreview(path);
        }
        ImGui::TextDisabled("%s", GetModelNameFromAssetPath(path).c_str());
        if (CountModelPreviews() > 0 && ImGui::SmallButton("確認削除")) {
            RemoveModelPreviews();
        }
    } else {
        ImGui::TextDisabled("-");
    }
    ImGui::PopID();
#else
    (void)item;
    (void)size;
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
