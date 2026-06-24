#define NOMINMAX
#include "ExecutablePackageWindow.h"

#include "IconsFontAwesome5.h"
#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::array<const char*, 3> kConfigurations = {
    "Debug",
    "Development",
    "Release"
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
    const std::wstring wide = name.wstring();
    return wide == L".cache" || wide == L".backup" || wide == L".trash" || wide == L"tools";
}

bool CopyDirectoryFiltered(const fs::path& source, const fs::path& destination, int& copiedFiles, std::string& errorMessage) {
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

    fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
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
            if (ShouldSkipResourceDirectory(entryPath.filename())) {
                it.disable_recursion_pending();
                continue;
            }
            fs::create_directories(destination / relative, ec);
            if (ec) {
                errorMessage = "フォルダを作成できません: " + PathToUtf8(destination / relative);
                return false;
            }
            continue;
        }

        if (!it->is_regular_file(ec)) {
            continue;
        }

        const fs::path target = destination / relative;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            errorMessage = "コピー先フォルダを作成できません: " + PathToUtf8(target.parent_path());
            return false;
        }

        fs::copy_file(entryPath, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            errorMessage = "ファイルをコピーできません: " + PathToUtf8(entryPath) + " -> " + PathToUtf8(target);
            return false;
        }
        ++copiedFiles;
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

    statusText_ = buildBeforePackage ? "ビルドしてから実行ファイルセットを作成しています..." : "既存のビルド出力から実行ファイルセットを作成しています...";
    task_ = std::async(std::launch::async, [request]() {
        return RunPackageTask(request);
    });
}

void ExecutablePackageWindow::DrawImGui() {
#ifdef USE_IMGUI
    PollTask();

    ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_BOX_OPEN " 実行ファイルセット作成");
    ImGui::TextWrapped("指定したビルド構成から、起動に必要な最小構成のフォルダを作成します。既存の同名フォルダは削除して上書きします。");
    ImGui::Separator();

    ImGui::InputTextWithHint("出力名", "例: GE3_Playable", packageNameBuffer_, sizeof(packageNameBuffer_));
    ImGui::Combo("構成", &configurationIndex_, kConfigurations.data(), static_cast<int>(kConfigurations.size()));

    const std::string sanitizedName = SanitizePackageName(packageNameBuffer_);
    const fs::path previewRoot = FindProjectRoot();
    if (!previewRoot.empty()) {
        ImGui::TextWrapped("出力先: %s", PathToUtf8(previewRoot / "ExecutableSets" / PathFromUtf8(sanitizedName)).c_str());
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
    ImGui::BulletText("コピー対象: exe / dll / Resources");
    ImGui::BulletText("除外対象: Resources/.cache / .backup / .trash / tools");
    ImGui::BulletText("同名フォルダは削除してから作成します。");
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

        const fs::path outputRoot = fs::weakly_canonical(projectRoot.parent_path() / "generated" / "outputs" / PathFromUtf8(request.configuration));
        const fs::path sourceExe = outputRoot / "DirectXGame.exe";
        std::error_code ec;
        if (!fs::exists(sourceExe, ec)) {
            return makeResult(false, "実行ファイルが見つかりません: " + PathToUtf8(sourceExe));
        }

        const fs::path packageRoot = projectRoot / "ExecutableSets";
        const fs::path destination = packageRoot / PathFromUtf8(packageName);
        if (!IsInsideDirectory(packageRoot, destination) || fs::weakly_canonical(packageRoot, ec) == fs::weakly_canonical(destination, ec)) {
            return makeResult(false, "安全のため出力先を削除できません: " + PathToUtf8(destination));
        }

        if (fs::exists(destination, ec)) {
            fs::remove_all(destination, ec);
            if (ec) {
                return makeResult(false, "既存の出力先を削除できません: " + PathToUtf8(destination));
            }
        }

        fs::create_directories(destination, ec);
        if (ec) {
            return makeResult(false, "出力先を作成できません: " + PathToUtf8(destination));
        }

        const std::string exeFileName = packageName + ".exe";
        fs::copy_file(sourceExe, destination / PathFromUtf8(exeFileName), fs::copy_options::overwrite_existing, ec);
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

            fs::copy_file(entry.path(), destination / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return makeResult(false, "DLLをコピーできません: " + PathToUtf8(entry.path()));
            }
            ++copiedDlls;
        }

        if (copiedDlls == 0) {
            return makeResult(false, "DLLが見つかりません: " + PathToUtf8(outputRoot));
        }

        int copiedResources = 0;
        std::string copyError;
        if (!CopyDirectoryFiltered(projectRoot / "Resources", destination / "Resources", copiedResources, copyError)) {
            return makeResult(false, copyError);
        }

        std::string batchError;
        if (!WriteLaunchBatch(destination, exeFileName, batchError)) {
            return makeResult(false, batchError);
        }

        std::ostringstream message;
        message << "作成完了: " << PathToUtf8(destination)
            << " / DLL " << copiedDlls
            << "件 / Resources " << copiedResources << "件";
        return makeResult(true, message.str());
    } catch (const std::exception& e) {
        return makeResult(false, std::string("作成中に例外が発生しました: ") + e.what());
    }
}
