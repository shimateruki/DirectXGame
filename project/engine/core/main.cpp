#include "Game.h"
#include "Framework.h" // Frameworkをインクルード
#include <Windows.h>
#include <filesystem>
#include <memory>

namespace {

bool IsResourceRoot(const std::filesystem::path& root) {
    std::error_code ec;
    const bool hasShaders = std::filesystem::is_directory(root / L"Resources" / L"shader", ec);
    if (ec || !hasShaders) {
        return false;
    }

    ec.clear();
    return std::filesystem::is_directory(root / L"Resources" / L"3DModel", ec) && !ec;
}

bool TrySetResourceRoot(const std::filesystem::path& root) {
    if (root.empty() || !IsResourceRoot(root)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::current_path(root, ec);
    if (ec) {
        return false;
    }

    const std::wstring message = L"[Startup] Resource root: " + root.native() + L"\n";
    OutputDebugStringW(message.c_str());
    return true;
}

std::filesystem::path GetExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }

    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

bool ConfigureResourceWorkingDirectory() {
    std::error_code ec;
    const std::filesystem::path currentDirectory = std::filesystem::current_path(ec);
    if (!ec && TrySetResourceRoot(currentDirectory)) {
        return true;
    }

    std::filesystem::path cursor = GetExecutableDirectory();
    for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
        if (TrySetResourceRoot(cursor) || TrySetResourceRoot(cursor / L"project")) {
            return true;
        }

        const std::filesystem::path parent = cursor.parent_path();
        if (parent == cursor) {
            break;
        }
        cursor = parent;
    }

    MessageBoxW(
        nullptr,
        L"Resourcesフォルダが見つかりません。\n"
        L"提出物にResourcesフォルダが含まれているか確認してください。",
        L"DirectXGame - 起動エラー",
        MB_OK | MB_ICONERROR);
    return false;
}

} // namespace

// Windowsアプリでのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    if (!ConfigureResourceWorkingDirectory()) {
        return 1;
    }

    // Frameworkクラスのポインタで、Gameクラスのインスタンスを生成
    std::unique_ptr<Framework> game = std::make_unique<Game>();

    // Frameworkの初期化
    game->Initialize();

    // Frameworkのメインループ実行
    game->Run();

    // Frameworkの終了処理
    game->Finalize();

    return 0;
}
