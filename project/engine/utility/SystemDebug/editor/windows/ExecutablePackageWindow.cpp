#define NOMINMAX
#include "ExecutablePackageWindow.h"

#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "json.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::array<const char*, 3> kConfigurations = {
    "Debug",
    "Development",
    "Release"
};

enum class TexturePackageMode {
    CompactPng = 0,
    FastDds,
    Complete
};

constexpr std::array<const char*, 3> kTextureModeLabels = {
    "コンパクト提出 (PNG優先)",
    "高速実行 (DDS優先)",
    "完全コピー (DDS + PNG)"
};

struct ResourceCopyStats {
    uint64_t sourceFiles = 0;
    uint64_t sourceBytes = 0;
    uint64_t copiedFiles = 0;
    uint64_t copiedBytes = 0;
    uint64_t skippedFiles = 0;
    uint64_t skippedBytes = 0;
    uint64_t skippedDirectories = 0;
    uint64_t texturePairs = 0;
    uint64_t omittedTextureFiles = 0;
    uint64_t omittedTextureBytes = 0;
    uint64_t omittedUnusedFiles = 0;
    uint64_t omittedUnusedBytes = 0;
};

struct ProjectCopyStats {
    uint64_t copiedFiles = 0;
    uint64_t copiedBytes = 0;
    uint64_t skippedFiles = 0;
    uint64_t skippedBytes = 0;
    uint64_t skippedDirectories = 0;
};

struct ResourceFile {
    fs::path source;
    fs::path relative;
    uint64_t bytes = 0;
};

class ScopedDirectoryCleanup {
public:
    explicit ScopedDirectoryCleanup(fs::path path) : path_(std::move(path)) {}
    ~ScopedDirectoryCleanup() {
        if (!active_) return;
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    void Release() { active_ = false; }

private:
    fs::path path_;
    bool active_ = true;
};

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        size = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (size <= 0) {
            return {};
        }

        std::wstring wide(size, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
        return wide;
    }

    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string PathToUtf8(const fs::path& path) {
    return WideToUtf8(path.wstring());
}

fs::path PathFromUtf8(const std::string& text) {
    return fs::path(Utf8ToWide(text));
}

std::string TrimAscii(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && static_cast<unsigned char>(text[begin]) <= 0x20) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && static_cast<unsigned char>(text[end - 1]) <= 0x20) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::string ToUpperAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

bool IsReservedWindowsName(const std::string& name) {
    std::string stem = name;
    const size_t dot = stem.find('.');
    if (dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }
    stem = ToUpperAscii(stem);

    static constexpr std::array<const char*, 22> kReserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    return std::find(kReserved.begin(), kReserved.end(), stem) != kReserved.end();
}

std::string SanitizePackageName(const std::string& input) {
    std::string name = TrimAscii(input);
    const std::string exeSuffix = ".exe";
    if (name.size() > exeSuffix.size()) {
        std::string tail = name.substr(name.size() - exeSuffix.size());
        if (ToUpperAscii(tail) == ".EXE") {
            name.resize(name.size() - exeSuffix.size());
        }
    }

    for (char& c : name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || c == '%') {
            c = '_';
        }
    }

    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }

    if (name.empty() || name == "." || name == "..") {
        name = "GE3_Playable";
    }
    if (IsReservedWindowsName(name)) {
        name = "_" + name;
    }
    return name;
}

fs::path GetExecutableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length == 0) {
        return fs::current_path();
    }
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

bool LooksLikeProjectRoot(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path / "DirectXGame.sln", ec) && fs::exists(path / "Resources", ec);
}

fs::path FindProjectRoot() {
    std::vector<fs::path> seeds;
    std::error_code ec;
    seeds.push_back(fs::current_path(ec));
    seeds.push_back(GetExecutableDirectory());

    for (const fs::path& seed : seeds) {
        if (seed.empty()) {
            continue;
        }

        fs::path cursor = seed;
        for (int i = 0; i < 8 && !cursor.empty(); ++i) {
            if (LooksLikeProjectRoot(cursor)) {
                return fs::weakly_canonical(cursor, ec);
            }

            const fs::path projectChild = cursor / "project";
            if (LooksLikeProjectRoot(projectChild)) {
                return fs::weakly_canonical(projectChild, ec);
            }

            if (!cursor.has_parent_path() || cursor == cursor.parent_path()) {
                break;
            }
            cursor = cursor.parent_path();
        }
    }

    return {};
}

bool IsInsideDirectory(const fs::path& root, const fs::path& target) {
    std::error_code ec;
    fs::path canonicalRoot = fs::weakly_canonical(root, ec);
    if (ec) {
        return false;
    }
    fs::path canonicalTarget = fs::weakly_canonical(target, ec);
    if (ec) {
        return false;
    }

    fs::path relative = fs::relative(canonicalTarget, canonicalRoot, ec);
    if (ec || relative.empty()) {
        return canonicalTarget == canonicalRoot;
    }
    for (const fs::path& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool ShouldSkipResourceDirectory(const fs::path& name) {
    std::wstring wide = name.wstring();
    std::transform(wide.begin(), wide.end(), wide.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return wide == L".cache" || wide == L".backup" || wide == L".trash" || wide == L"tools";
}

std::wstring ToLowerWide(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

bool IsEditorGeneratedDirectory(const fs::path& relative) {
    const std::wstring normalized = ToLowerWide(relative.generic_wstring());
    return normalized == L"generated/editor" || normalized.rfind(L"generated/editor/", 0) == 0;
}

bool HasPathPrefix(const std::wstring& normalized, const std::wstring& prefix) {
    return normalized == prefix || normalized.rfind(prefix + L"/", 0) == 0;
}

bool IsProjectDirectoryExcluded(const fs::path& relative) {
    const std::wstring normalized = ToLowerWide(relative.generic_wstring());
    static constexpr std::array<const wchar_t*, 11> kExcludedRoots = {
        L".vs",
        L".git",
        L".agents",
        L".codex",
        L"output",
        L"tmp",
        L"debug",
        L"development",
        L"release",
        L"executablesets",
        L"submissionpackages"
    };
    for (const wchar_t* root : kExcludedRoots) {
        if (HasPathPrefix(normalized, root)) {
            return true;
        }
    }

    if (HasPathPrefix(normalized, L"resources/.cache") ||
        HasPathPrefix(normalized, L"resources/.backup") ||
        HasPathPrefix(normalized, L"resources/.trash") ||
        HasPathPrefix(normalized, L"resources/generated/editor") ||
        HasPathPrefix(normalized, L"externals/.cache")) {
        return true;
    }

    for (const fs::path& part : relative) {
        if (ToLowerWide(part.wstring()) == L"__pycache__") {
            return true;
        }
    }
    return false;
}

bool IsProjectFileExcluded(const fs::path& relative, uint64_t fileBytes, ProjectCopyStats& stats) {
    const std::wstring fileName = ToLowerWide(relative.filename().wstring());
    const std::wstring extension = ToLowerWide(relative.extension().wstring());
    const bool rootLocalState = relative.parent_path().empty() &&
        (fileName == L"imgui.ini" ||
         fileName == L"nodeeditor.json" ||
         fileName == L"shader_compile.log" ||
         fileName == L"system.drawing.drawing2d.graphicspath" ||
         fileName == L"スプリント振り返り_2026-07-10.md");
    const bool generatedFile =
        fileName == L"thumbs.db" ||
        fileName == L".ds_store" ||
        extension == L".user" ||
        extension == L".suo" ||
        extension == L".opendb" ||
        extension == L".ipch" ||
        extension == L".pyc" ||
        extension == L".pyo" ||
        extension == L".log" ||
        extension == L".tmp";
    if (!rootLocalState && !generatedFile) {
        return false;
    }

    ++stats.skippedFiles;
    stats.skippedBytes += fileBytes;
    return true;
}

bool IsReparsePoint(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool CopyProjectFiltered(
    const fs::path& source,
    const fs::path& destination,
    ProjectCopyStats& stats,
    std::string& errorMessage) {
    std::error_code ec;
    if (!fs::exists(source, ec) || !fs::is_directory(source, ec)) {
        errorMessage = "プロジェクトフォルダが見つかりません: " + PathToUtf8(source);
        return false;
    }

    fs::create_directories(destination, ec);
    if (ec) {
        errorMessage = "projectコピー先を作成できません: " + PathToUtf8(destination);
        return false;
    }

    fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        errorMessage = "プロジェクトを走査できません: " + ec.message();
        return false;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            errorMessage = "プロジェクトコピー中に走査エラーが発生しました: " + ec.message();
            return false;
        }

        const fs::path entryPath = it->path();
        const fs::path relative = fs::relative(entryPath, source, ec);
        if (ec) {
            errorMessage = "プロジェクト内の相対パスを作れません: " + ec.message();
            return false;
        }

        if (it->is_directory(ec)) {
            if (IsReparsePoint(entryPath) || IsProjectDirectoryExcluded(relative)) {
                ++stats.skippedDirectories;
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec) || IsReparsePoint(entryPath)) {
            continue;
        }

        const uint64_t bytes = static_cast<uint64_t>(it->file_size(ec));
        if (ec) {
            errorMessage = "プロジェクトファイルのサイズを取得できません: " + PathToUtf8(entryPath);
            return false;
        }
        if (IsProjectFileExcluded(relative, bytes, stats)) {
            continue;
        }

        const fs::path target = destination / relative;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            errorMessage = "project内のコピー先を作成できません: " + PathToUtf8(target.parent_path());
            return false;
        }
        fs::copy_file(entryPath, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            errorMessage = "プロジェクトファイルをコピーできません: " + PathToUtf8(entryPath);
            return false;
        }
        ++stats.copiedFiles;
        stats.copiedBytes += bytes;
    }

    return true;
}

void AccumulateSkippedDirectory(const fs::path& directory, ResourceCopyStats& stats) {
    std::error_code ec;
    fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        return;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        const uint64_t bytes = static_cast<uint64_t>(it->file_size(ec));
        if (ec) {
            ec.clear();
            continue;
        }
        ++stats.sourceFiles;
        stats.sourceBytes += bytes;
        ++stats.skippedFiles;
        stats.skippedBytes += bytes;
    }
}

TexturePackageMode ToTexturePackageMode(int value) {
    return static_cast<TexturePackageMode>(std::clamp(value, 0, static_cast<int>(TexturePackageMode::Complete)));
}

const char* GetTextureModeId(TexturePackageMode mode) {
    switch (mode) {
    case TexturePackageMode::CompactPng: return "compact_png";
    case TexturePackageMode::FastDds: return "fast_dds";
    case TexturePackageMode::Complete: return "complete";
    }
    return "compact_png";
}

std::wstring GetTexturePairKey(const fs::path& relative) {
    fs::path base = relative;
    base.replace_extension();
    return ToLowerWide(base.generic_wstring());
}

bool LoadUnusedResourcePaths(
    const fs::path& projectRoot,
    std::unordered_set<std::wstring>& unusedPaths,
    std::string& errorMessage) {
    const fs::path reportPath = projectRoot / "Resources" / ".cache" / "asset_audit" / "latest_report.json";
    std::ifstream file(reportPath, std::ios::binary);
    if (!file) {
        errorMessage = "Asset Auditの結果が見つかりません: " + PathToUtf8(reportPath);
        return false;
    }

    nlohmann::json report;
    try {
        file >> report;
    } catch (const std::exception& e) {
        errorMessage = std::string("Asset Auditの結果を読み込めません: ") + e.what();
        return false;
    }

    const auto unusedIt = report.find("unusedAssets");
    if (unusedIt == report.end() || !unusedIt->is_array()) {
        errorMessage = "Asset Auditの結果にunusedAssetsがありません。";
        return false;
    }

    auto registerPath = [&unusedPaths](const std::string& utf8Path) {
        fs::path path = PathFromUtf8(utf8Path);
        std::wstring normalized = ToLowerWide(path.generic_wstring());
        constexpr std::wstring_view prefix = L"resources/";
        if (normalized.rfind(prefix, 0) != 0) {
            return;
        }

        normalized.erase(0, prefix.size());
        if (normalized.empty() || normalized.find(L"..") != std::wstring::npos) {
            return;
        }
        unusedPaths.insert(normalized);
        unusedPaths.insert(normalized + L".meta");
    };

    for (const nlohmann::json& item : *unusedIt) {
        if (!item.is_object()) {
            continue;
        }
        const auto pathIt = item.find("path");
        if (pathIt != item.end() && pathIt->is_string()) {
            registerPath(pathIt->get<std::string>());
        }
        const auto pairedIt = item.find("pairedFiles");
        if (pairedIt == item.end() || !pairedIt->is_array()) {
            continue;
        }
        for (const nlohmann::json& paired : *pairedIt) {
            if (paired.is_string()) {
                registerPath(paired.get<std::string>());
            }
        }
    }

    return true;
}

bool CopyDirectoryFiltered(
    const fs::path& source,
    const fs::path& destination,
    TexturePackageMode textureMode,
    const std::unordered_set<std::wstring>& unusedResourcePaths,
    ResourceCopyStats& stats,
    std::string& errorMessage) {
    std::error_code ec;
    if (!fs::exists(source, ec) || !fs::is_directory(source, ec)) {
        errorMessage = "Resourcesフォルダが見つかりません: " + PathToUtf8(source);
        return false;
    }

    fs::create_directories(destination, ec);
    if (ec) {
        errorMessage = "Resourcesコピー先を作成できません: " + PathToUtf8(destination);
        return false;
    }

    std::vector<ResourceFile> files;
    fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        errorMessage = "Resourcesを走査できません: " + ec.message();
        return false;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            errorMessage = "Resourcesコピー中に走査エラーが発生しました: " + ec.message();
            return false;
        }

        const fs::path entryPath = it->path();
        const fs::path relative = fs::relative(entryPath, source, ec);
        if (ec) {
            errorMessage = "Resources内の相対パスを作れません: " + ec.message();
            return false;
        }

        if (it->is_directory(ec)) {
            if (ShouldSkipResourceDirectory(entryPath.filename()) || IsEditorGeneratedDirectory(relative)) {
                ++stats.skippedDirectories;
                AccumulateSkippedDirectory(entryPath, stats);
                it.disable_recursion_pending();
                continue;
            }
            continue;
        }

        if (!it->is_regular_file(ec)) {
            continue;
        }

        const uint64_t bytes = static_cast<uint64_t>(it->file_size(ec));
        if (ec) {
            errorMessage = "ファイルサイズを取得できません: " + PathToUtf8(entryPath);
            return false;
        }
        ++stats.sourceFiles;
        stats.sourceBytes += bytes;
        files.push_back({ entryPath, relative, bytes });
    }

    constexpr uint8_t kHasDds = 1 << 0;
    constexpr uint8_t kHasPng = 1 << 1;
    std::unordered_map<std::wstring, uint8_t> texturePairs;
    for (const ResourceFile& file : files) {
        const std::wstring extension = ToLowerWide(file.relative.extension().wstring());
        uint8_t flag = 0;
        if (extension == L".dds") flag = kHasDds;
        else if (extension == L".png") flag = kHasPng;
        if (flag != 0) {
            texturePairs[GetTexturePairKey(file.relative)] |= flag;
        }
    }
    for (const auto& [key, flags] : texturePairs) {
        (void)key;
        if ((flags & kHasDds) != 0 && (flags & kHasPng) != 0) {
            ++stats.texturePairs;
        }
    }

    for (const ResourceFile& file : files) {
        const std::wstring normalizedRelative = ToLowerWide(file.relative.generic_wstring());
        if (unusedResourcePaths.find(normalizedRelative) != unusedResourcePaths.end()) {
            ++stats.skippedFiles;
            stats.skippedBytes += file.bytes;
            ++stats.omittedUnusedFiles;
            stats.omittedUnusedBytes += file.bytes;
            continue;
        }

        const std::wstring extension = ToLowerWide(file.relative.extension().wstring());
        const auto pairIt = texturePairs.find(GetTexturePairKey(file.relative));
        const uint8_t pairFlags = pairIt == texturePairs.end() ? 0 : pairIt->second;
        const bool hasPair = (pairFlags & kHasDds) != 0 && (pairFlags & kHasPng) != 0;
        const bool omitForCompact = textureMode == TexturePackageMode::CompactPng && extension == L".dds" && hasPair;
        const bool omitForFast = textureMode == TexturePackageMode::FastDds && extension == L".png" && hasPair;
        if (omitForCompact || omitForFast) {
            ++stats.skippedFiles;
            stats.skippedBytes += file.bytes;
            ++stats.omittedTextureFiles;
            stats.omittedTextureBytes += file.bytes;
            continue;
        }

        const fs::path target = destination / file.relative;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            errorMessage = "コピー先フォルダを作成できません: " + PathToUtf8(target.parent_path());
            return false;
        }

        fs::copy_file(file.source, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            errorMessage = "ファイルをコピーできません: " + PathToUtf8(file.source) + " -> " + PathToUtf8(target);
            return false;
        }
        ++stats.copiedFiles;
        stats.copiedBytes += file.bytes;
    }

    return true;
}

fs::path FindMSBuildPath() {
    const std::array<fs::path, 4> candidates = {
        fs::path(L"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe"),
        fs::path(L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe"),
        fs::path(L"C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\MSBuild\\Current\\Bin\\MSBuild.exe"),
        fs::path(L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\MSBuild\\Current\\Bin\\MSBuild.exe")
    };

    std::error_code ec;
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    return fs::path(L"MSBuild.exe");
}

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

bool RunAssetAudit(const fs::path& projectRoot, std::string& errorMessage) {
    const fs::path scriptPath = projectRoot / "tools" / "asset_audit" / "asset_audit.ps1";
    std::error_code ec;
    if (!fs::exists(scriptPath, ec) || !fs::is_regular_file(scriptPath, ec)) {
        errorMessage = "Asset Auditスクリプトが見つかりません: " + PathToUtf8(scriptPath);
        return false;
    }

    const std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        Quote(scriptPath.wstring()) + L" -Root Resources -Top 200";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const std::wstring workingDirectory = projectRoot.wstring();
    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.c_str(),
        &startupInfo,
        &processInfo);
    if (!created) {
        errorMessage = "Asset Auditを起動できませんでした。";
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (exitCode != 0) {
        errorMessage = "Asset Auditが失敗しました。ExitCode: " + std::to_string(exitCode);
        return false;
    }
    return true;
}

bool RunMSBuild(const fs::path& projectRoot, const std::string& configuration, std::string& errorMessage) {
    const fs::path solution = projectRoot / "DirectXGame.sln";
    std::error_code ec;
    if (!fs::exists(solution, ec)) {
        errorMessage = "DirectXGame.slnが見つかりません: " + PathToUtf8(solution);
        return false;
    }

    const fs::path msbuild = FindMSBuildPath();
    const std::wstring configWide = Utf8ToWide(configuration);
    const std::wstring command =
        Quote(msbuild.wstring()) + L" " +
        Quote(solution.wstring()) +
        L" /p:Configuration=" + configWide +
        L" /p:Platform=x64 /m:1";

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const std::wstring workingDir = projectRoot.wstring();

    BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDir.c_str(),
        &startupInfo,
        &processInfo);

    if (!created) {
        errorMessage = "MSBuildを起動できませんでした。Visual StudioのMSBuildが見つからない可能性があります。";
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (exitCode != 0) {
        errorMessage = "MSBuildが失敗しました。ExitCode: " + std::to_string(exitCode);
        return false;
    }
    return true;
}

bool WriteLaunchBatch(const fs::path& destination, const std::string& exeName, std::string& errorMessage) {
    const fs::path batchPath = destination / "launch.bat";
    std::ofstream file(batchPath, std::ios::binary);
    if (!file) {
        errorMessage = "launch.batを作成できません: " + PathToUtf8(batchPath);
        return false;
    }

    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    file << "@echo off\r\n";
    file << "chcp 65001 >nul\r\n";
    file << "cd /d \"%~dp0\"\r\n";
    file << "start \"\" \"" << exeName << "\"\r\n";
    return true;
}

std::string FormatMiB(uint64_t bytes) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2)
        << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MiB";
    return stream.str();
}

std::string MakeLocalTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%dT%H:%M:%S%z");
    return stream.str();
}

bool WritePackageManifest(
    const fs::path& destination,
    const std::string& packageName,
    const std::string& configuration,
    bool builtBeforePackage,
    TexturePackageMode textureMode,
    bool excludeUnusedResources,
    bool zipRequested,
    const std::string& exeName,
    int copiedDlls,
    const ResourceCopyStats& stats,
    std::string& errorMessage) {
    nlohmann::json manifest;
    manifest["formatVersion"] = 1;
    manifest["packageName"] = packageName;
    manifest["configuration"] = configuration;
    manifest["createdAt"] = MakeLocalTimestamp();
    manifest["builtBeforePackage"] = builtBeforePackage;
    manifest["executable"] = exeName;
    manifest["dllCount"] = copiedDlls;
    manifest["zipRequested"] = zipRequested;
    manifest["textureMode"] = GetTextureModeId(textureMode);
    manifest["excludeUnusedResources"] = excludeUnusedResources;
    manifest["resources"] = {
        { "sourceFiles", stats.sourceFiles },
        { "sourceBytes", stats.sourceBytes },
        { "copiedFiles", stats.copiedFiles },
        { "copiedBytes", stats.copiedBytes },
        { "skippedFiles", stats.skippedFiles },
        { "skippedBytes", stats.skippedBytes },
        { "skippedDirectories", stats.skippedDirectories },
        { "texturePairs", stats.texturePairs },
        { "omittedTextureFiles", stats.omittedTextureFiles },
        { "omittedTextureBytes", stats.omittedTextureBytes },
        { "omittedUnusedFiles", stats.omittedUnusedFiles },
        { "omittedUnusedBytes", stats.omittedUnusedBytes }
    };
    manifest["resourceExclusions"] = {
        "Resources/.cache",
        "Resources/.backup",
        "Resources/.trash",
        "Resources/tools",
        "Resources/generated/editor"
    };
    manifest["notes"] = {
        "元のResourcesは変更していません。",
        "DDSとPNGは同一フォルダ・同一ファイル名のペアだけを整理しています。",
        "片方しか存在しないテクスチャは保持しています。",
        "未使用候補の除外を選んだ場合は、作成直前のAsset Audit結果を使用しています。"
    };

    const fs::path manifestPath = destination / "package_manifest.json";
    std::ofstream file(manifestPath, std::ios::binary);
    if (!file) {
        errorMessage = "package_manifest.jsonを作成できません: " + PathToUtf8(manifestPath);
        return false;
    }
    file << manifest.dump(2);
    if (!file) {
        errorMessage = "package_manifest.jsonを書き込めません: " + PathToUtf8(manifestPath);
        return false;
    }
    return true;
}

std::wstring QuotePowerShellLiteral(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'\'');
    for (wchar_t c : value) {
        if (c == L'\'') {
            escaped.push_back(L'\'');
        }
        escaped.push_back(c);
    }
    escaped.push_back(L'\'');
    return escaped;
}

bool CreateZipArchive(
    const fs::path& packageRoot,
    const fs::path& destination,
    const fs::path& zipPath,
    std::string& errorMessage) {
    std::error_code ec;
    if (!IsInsideDirectory(packageRoot, zipPath)) {
        errorMessage = "安全のためZIP出力先を操作できません: " + PathToUtf8(zipPath);
        return false;
    }
    if (fs::exists(zipPath, ec)) {
        fs::remove(zipPath, ec);
        if (ec) {
            errorMessage = "既存ZIPを削除できません: " + PathToUtf8(zipPath);
            return false;
        }
    }

    const std::wstring script =
        L"$ErrorActionPreference='Stop'; Compress-Archive -LiteralPath " + QuotePowerShellLiteral(destination.wstring()) +
        L" -DestinationPath " + QuotePowerShellLiteral(zipPath.wstring()) + L" -Force";
    const std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " + Quote(script);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const std::wstring workingDirectory = packageRoot.wstring();
    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.c_str(),
        &startupInfo,
        &processInfo);
    if (!created) {
        errorMessage = "ZIP作成用PowerShellを起動できませんでした。";
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (exitCode != 0 || !fs::exists(zipPath, ec) || fs::file_size(zipPath, ec) == 0) {
        errorMessage = "ZIP作成に失敗しました。ExitCode: " + std::to_string(exitCode);
        return false;
    }
    return true;
}

}

void ExecutablePackageWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

bool ExecutablePackageWindow::IsTaskRunning() {
    return task_.valid() && task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void ExecutablePackageWindow::PollTask() {
    if (!task_.valid()) {
        return;
    }
    if (task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    PackageResult result = task_.get();
    statusText_ = result.success ? (ICON_FA_CHECK_CIRCLE " " + result.message) : (ICON_FA_EXCLAMATION_TRIANGLE " " + result.message);
}

void ExecutablePackageWindow::StartPackageTask(bool buildBeforePackage) {
    if (IsTaskRunning()) {
        return;
    }

    const int safeIndex = std::clamp(configurationIndex_, 0, static_cast<int>(kConfigurations.size()) - 1);
    PackageRequest request;
    request.packageName = packageNameBuffer_;
    request.configuration = kConfigurations[safeIndex];
    request.buildBeforePackage = buildBeforePackage;
    request.textureMode = std::clamp(textureModeIndex_, 0, static_cast<int>(kTextureModeLabels.size()) - 1);
    request.createZip = createZip_;
    request.includeProject = includeProject_;
    request.includeReadMe = includeProject_ && includeReadMe_;
    request.excludeUnusedResources = excludeUnusedResources_;

    const char* packageKind = includeProject_ ? "提出パッケージ" : "実行ファイルセット";
    statusText_ = buildBeforePackage
        ? std::string("ビルドしてから") + packageKind + "を作成しています..."
        : std::string("既存のビルド出力から") + packageKind + "を作成しています...";
    task_ = std::async(std::launch::async, [request]() {
        return RunPackageTask(request);
    });
}

void ExecutablePackageWindow::DrawImGui() {
#ifdef USE_IMGUI
    PollTask();

    ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_BOX_OPEN " パッケージ作成");
    ImGui::TextWrapped("実行ファイルセットだけ、または提出要件に合わせた完全パッケージをプロジェクト外へ作成します。元のファイルは変更しません。");
    ImGui::Separator();

    ImGui::InputTextWithHint("出力名", "例: LE3A_99_カマタ_タロウ", packageNameBuffer_, sizeof(packageNameBuffer_));
    ImGui::Combo("構成", &configurationIndex_, kConfigurations.data(), static_cast<int>(kConfigurations.size()));
    ImGui::Combo("テクスチャ構成", &textureModeIndex_, kTextureModeLabels.data(), static_cast<int>(kTextureModeLabels.size()));
    if (textureModeIndex_ == static_cast<int>(TexturePackageMode::CompactPng)) {
        ImGui::TextDisabled("同名DDS/PNGペアはPNGだけを残します。DDSしかないSkybox等は保持します。");
    } else if (textureModeIndex_ == static_cast<int>(TexturePackageMode::FastDds)) {
        ImGui::TextDisabled("同名DDS/PNGペアはDDSだけを残し、起動時の画像デコード負荷を抑えます。");
    } else {
        ImGui::TextDisabled("DDSとPNGを両方コピーします。容量は最大ですが、素材確認用に安全です。");
    }
    ImGui::Checkbox("Asset Auditの未使用候補を実行ファイルセットから除外", &excludeUnusedResources_);
    if (excludeUnusedResources_) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.78f, 0.28f, 1.0f),
            "作成直前に監査します。動的なパスは判定できない場合があるため、完成後に全シーンを確認してください。");
    }
    ImGui::Checkbox("project一式も含める（提出形式）", &includeProject_);
    if (includeProject_) {
        ImGui::Indent();
        ImGui::Checkbox("プロジェクト外のReadMe.mdを同梱", &includeReadMe_);
        ImGui::TextDisabled("参照元: projectと同じ階層のReadMe.md（このツールでは内容を生成・変更しません）");
        ImGui::Unindent();
    }
    ImGui::Checkbox("提出用ZIPも作成", &createZip_);

    const std::string sanitizedName = SanitizePackageName(packageNameBuffer_);
    const fs::path detectedProjectRoot = FindProjectRoot();
    const fs::path previewRoot = detectedProjectRoot.empty() ? fs::path{} : detectedProjectRoot.parent_path();
    if (!previewRoot.empty()) {
        const fs::path outputFolder = includeProject_ ? fs::path("SubmissionPackages") : fs::path("ExecutableSets");
        ImGui::TextWrapped("出力先: %s", PathToUtf8(previewRoot / outputFolder / PathFromUtf8(sanitizedName)).c_str());
        if (createZip_) {
            ImGui::TextWrapped("ZIP: %s", PathToUtf8(previewRoot / outputFolder / PathFromUtf8(sanitizedName + ".zip")).c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "プロジェクトルートを検出できません。");
    }

    if (sanitizedName != packageNameBuffer_) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "実際の出力名: %s", sanitizedName.c_str());
    }

    const bool running = IsTaskRunning();
    ImGui::BeginDisabled(running);
    if (ImGui::Button(ICON_FA_ROCKET " ビルドして作成", ImVec2(180.0f, 0.0f))) {
        StartPackageTask(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " 既存出力から作成", ImVec2(180.0f, 0.0f))) {
        StartPackageTask(false);
    }
    ImGui::EndDisabled();

    if (running) {
        ImGui::SameLine();
        ImGui::TextDisabled("処理中...");
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", statusText_.c_str());
    if (includeProject_) {
        ImGui::BulletText("提出構成: 実行ファイルセット / project / ReadMe.md");
        ImGui::BulletText("project除外: .vs / .git / .agents / .codex / キャッシュ / バックアップ / 一時ファイル");
    } else {
        ImGui::BulletText("コピー対象: exe / dll / Runtime Resources / package_manifest.json");
        ImGui::BulletText("除外対象: .cache / .backup / .trash / tools / generated/editor");
    }
    if (excludeUnusedResources_) {
        ImGui::BulletText("追加除外: Asset Auditが未使用候補と判定したRuntime Resources");
    }
    ImGui::BulletText("DDS/PNGは同一フォルダ・同一名のペアだけを安全に整理します。");
    ImGui::BulletText("作成失敗時は既存の完成済みSetを残し、一時フォルダだけを削除します。");
#endif
}

ExecutablePackageWindow::PackageResult ExecutablePackageWindow::RunPackageTask(const PackageRequest& request) {
    auto makeResult = [](bool success, const std::string& message) {
        PackageResult result;
        result.success = success;
        result.message = message;
        return result;
    };

    try {
        const std::string packageName = SanitizePackageName(request.packageName);
        const fs::path projectRoot = FindProjectRoot();
        if (projectRoot.empty()) {
            return makeResult(false, "プロジェクトルートを検出できませんでした。");
        }

        if (request.buildBeforePackage) {
            std::string buildError;
            if (!RunMSBuild(projectRoot, request.configuration, buildError)) {
                return makeResult(false, buildError);
            }
        }

        std::unordered_set<std::wstring> unusedResourcePaths;
        if (request.excludeUnusedResources) {
            std::string auditError;
            if (!RunAssetAudit(projectRoot, auditError)) {
                return makeResult(false, auditError);
            }
            if (!LoadUnusedResourcePaths(projectRoot, unusedResourcePaths, auditError)) {
                return makeResult(false, auditError);
            }
        }

        const fs::path outputRoot = fs::weakly_canonical(projectRoot.parent_path() / "generated" / "outputs" / PathFromUtf8(request.configuration));
        const fs::path sourceExe = outputRoot / "DirectXGame.exe";
        std::error_code ec;
        if (!fs::exists(sourceExe, ec)) {
            return makeResult(false, "実行ファイルが見つかりません: " + PathToUtf8(sourceExe));
        }

        const TexturePackageMode textureMode = ToTexturePackageMode(request.textureMode);
        const fs::path packageRoot = projectRoot.parent_path() /
            (request.includeProject ? fs::path("SubmissionPackages") : fs::path("ExecutableSets"));
        fs::create_directories(packageRoot, ec);
        if (ec) {
            return makeResult(false, "パッケージ出力フォルダを作成できません: " + PathToUtf8(packageRoot));
        }

        const fs::path destination = packageRoot / PathFromUtf8(packageName);
        const fs::path staging = packageRoot / PathFromUtf8(packageName + ".building");
        const fs::path previous = packageRoot / PathFromUtf8(packageName + ".previous");
        const fs::path zipPath = packageRoot / PathFromUtf8(packageName + ".zip");
        if (!IsInsideDirectory(packageRoot, destination) || !IsInsideDirectory(packageRoot, staging) ||
            !IsInsideDirectory(packageRoot, previous) || !IsInsideDirectory(packageRoot, zipPath) ||
            fs::weakly_canonical(packageRoot, ec) == fs::weakly_canonical(destination, ec)) {
            return makeResult(false, "安全のため出力先を削除できません: " + PathToUtf8(destination));
        }

        if (fs::exists(staging, ec)) {
            fs::remove_all(staging, ec);
            if (ec) {
                return makeResult(false, "前回の一時フォルダを削除できません: " + PathToUtf8(staging));
            }
        }
        if (fs::exists(previous, ec)) {
            if (!fs::exists(destination, ec)) {
                fs::rename(previous, destination, ec);
            } else {
                fs::remove_all(previous, ec);
            }
            if (ec) {
                return makeResult(false, "前回の完成済みSetを復旧・整理できません: " + PathToUtf8(previous));
            }
        }

        fs::create_directories(staging, ec);
        if (ec) {
            return makeResult(false, "一時出力先を作成できません: " + PathToUtf8(staging));
        }
        ScopedDirectoryCleanup stagingCleanup(staging);

        const fs::path runtimeDestination = request.includeProject
            ? staging / fs::path(L"実行ファイルセット")
            : staging;
        fs::create_directories(runtimeDestination, ec);
        if (ec) {
            return makeResult(false, "実行ファイルセットの出力先を作成できません: " + PathToUtf8(runtimeDestination));
        }

        const std::string exeFileName = packageName + ".exe";
        fs::copy_file(sourceExe, runtimeDestination / PathFromUtf8(exeFileName), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return makeResult(false, "実行ファイルをコピーできません: " + PathToUtf8(sourceExe));
        }

        int copiedDlls = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(outputRoot, ec)) {
            if (ec) {
                return makeResult(false, "ビルド出力フォルダを走査できません: " + PathToUtf8(outputRoot));
            }
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".dll") {
                continue;
            }

            fs::copy_file(entry.path(), runtimeDestination / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return makeResult(false, "DLLをコピーできません: " + PathToUtf8(entry.path()));
            }
            ++copiedDlls;
        }

        if (copiedDlls == 0) {
            return makeResult(false, "DLLが見つかりません: " + PathToUtf8(outputRoot));
        }

        ResourceCopyStats resourceStats;
        std::string copyError;
        if (!CopyDirectoryFiltered(
                projectRoot / "Resources",
                runtimeDestination / "Resources",
                textureMode,
                unusedResourcePaths,
                resourceStats,
                copyError)) {
            return makeResult(false, copyError);
        }

        std::string batchError;
        if (!WriteLaunchBatch(runtimeDestination, exeFileName, batchError)) {
            return makeResult(false, batchError);
        }

        std::string manifestError;
        if (!WritePackageManifest(
            runtimeDestination,
            packageName,
            request.configuration,
            request.buildBeforePackage,
            textureMode,
            request.excludeUnusedResources,
            request.createZip,
            exeFileName,
            copiedDlls,
            resourceStats,
            manifestError)) {
            return makeResult(false, manifestError);
        }

        ProjectCopyStats projectStats;
        if (request.includeProject) {
            std::string projectCopyError;
            if (!CopyProjectFiltered(projectRoot, staging / "project", projectStats, projectCopyError)) {
                return makeResult(false, projectCopyError);
            }

            if (request.includeReadMe) {
                const fs::path readMeSource = projectRoot.parent_path() / "ReadMe.md";
                if (!fs::exists(readMeSource, ec) || !fs::is_regular_file(readMeSource, ec)) {
                    return makeResult(false, "プロジェクト外のReadMe.mdが見つかりません: " + PathToUtf8(readMeSource));
                }
                fs::copy_file(readMeSource, staging / "ReadMe.md", fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    return makeResult(false, "ReadMe.mdを提出フォルダへコピーできません: " + PathToUtf8(readMeSource));
                }
            }
        }

        const bool hadPreviousSet = fs::exists(destination, ec);
        if (hadPreviousSet) {
            fs::rename(destination, previous, ec);
            if (ec) {
                return makeResult(false, "既存の完成済みSetを退避できません: " + PathToUtf8(destination));
            }
        }
        fs::rename(staging, destination, ec);
        if (ec) {
            if (hadPreviousSet) {
                std::error_code restoreError;
                fs::rename(previous, destination, restoreError);
            }
            return makeResult(false, "完成した一時Setを移動できません: " + PathToUtf8(staging));
        }
        stagingCleanup.Release();
        if (hadPreviousSet) {
            fs::remove_all(previous, ec);
            if (ec) {
                return makeResult(false, "新しいSetは完成しましたが、旧Setの退避フォルダを削除できません: " + PathToUtf8(previous));
            }
        }

        if (request.createZip) {
            std::string zipError;
            if (!CreateZipArchive(packageRoot, destination, zipPath, zipError)) {
                return makeResult(false, zipError + " / Setフォルダ自体は作成済みです。");
            }
        } else if (fs::exists(zipPath, ec)) {
            fs::remove(zipPath, ec);
            if (ec) {
                return makeResult(false, "古い提出用ZIPを削除できません: " + PathToUtf8(zipPath));
            }
        }

        std::ostringstream message;
        message << "作成完了: " << PathToUtf8(destination)
            << " / DLL " << copiedDlls
            << "件 / Resources " << resourceStats.copiedFiles << "件 (" << FormatMiB(resourceStats.copiedBytes) << ")"
            << " / 除外 " << resourceStats.skippedFiles << "件 (" << FormatMiB(resourceStats.skippedBytes) << ")"
            << " / Texture Pair " << resourceStats.texturePairs << "組";
        if (request.excludeUnusedResources) {
            message << " / 未使用候補除外 " << resourceStats.omittedUnusedFiles
                << "件 (" << FormatMiB(resourceStats.omittedUnusedBytes) << ")";
        }
        if (request.includeProject) {
            message << " / project " << projectStats.copiedFiles << "件 (" << FormatMiB(projectStats.copiedBytes) << ")"
                << " / project除外 " << projectStats.skippedFiles << "件・" << projectStats.skippedDirectories << "フォルダ";
        }
        if (request.createZip) {
            const uint64_t zipBytes = static_cast<uint64_t>(fs::file_size(zipPath, ec));
            message << " / ZIP " << FormatMiB(zipBytes);
        }
        return makeResult(true, message.str());
    } catch (const std::exception& e) {
        return makeResult(false, std::string("作成中に例外が発生しました: ") + e.what());
    }
}
