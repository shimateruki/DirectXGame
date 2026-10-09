#include "WinApp.h"
#include "game/ui/SettingsMenuOverlay.h"
#include <Windows.h>
#ifdef USE_IMGUI
#include "imgui_impl_win32.h"
#endif
#include <DirectXCommon.h>
#include "json.hpp"
#include <fstream>

#pragma comment(lib, "winmm.lib")
#ifdef USE_IMGUI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif

// --- WinApp.cpp ---
// Windowsメッセージを受け取り、ImGui入力、終了要求、リサイズ、カーソル解除を処理する。
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }
#endif

    switch (msg) {
    case WM_CLOSE:
        WinApp::RequestClose();
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) {
            ClipCursor(nullptr); // 解放
        }
        break;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            const int32_t width = LOWORD(lparam);
            const int32_t height = HIWORD(lparam);
            if (DirectXCommon::GetInstance()->GetDevice()) {
                DirectXCommon::GetInstance()->RequestResize(width, height);
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

// Win32ウィンドウを作成し、実際のクライアントサイズをエンジン側へ反映する。

void WinApp::Initialize(const wchar_t* title, int width, int height) {
    hInstance_ = GetModuleHandle(nullptr);
    timeBeginPeriod(1);

    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"CG2WindowClass";
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    RECT wrc = { 0, 0, width, height };
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);


    hwnd_ = CreateWindow(
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW | WS_MAXIMIZE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wrc.right - wrc.left,
        wrc.bottom - wrc.top,
        nullptr, nullptr,
        hInstance_,
        nullptr);


    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    kClientWidth = clientRect.right - clientRect.left;
    kClientHeight = clientRect.bottom - clientRect.top;

    ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
    // 保存済み表示設定のみ起動時に反映。破損した設定では従来の起動を維持します。
    try {
        std::ifstream file("Resources/json/user_config.json");
        nlohmann::json config;
        if (file >> config; config.contains("display")) {
            const auto& d = config.at("display");
            SetDisplayMode(d.value("width", 1280), d.value("height", 720), d.value("borderless", false));
        }
    } catch (...) {}
    GetClientRect(hwnd_, &clientRect);
    kClientWidth = clientRect.right;
    kClientHeight = clientRect.bottom;
}

WinApp::DisplayState WinApp::CaptureDisplayState() const {
    DisplayState state;
    state.style = GetWindowLongPtr(hwnd_, GWL_STYLE);
    GetWindowPlacement(hwnd_, &state.placement);
    return state;
}

bool WinApp::SetDisplayMode(int width, int height, bool borderless) {
    if (!hwnd_ || width < 960 || height < 540 || width > 7680 || height > 4320) return false;
    MONITORINFO monitor{ sizeof(MONITORINFO) };
    if (!GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    RECT rect{0, 0, width, height};
    const LONG style = borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    if (borderless) rect = monitor.rcMonitor;
    else {
        AdjustWindowRect(&rect, style, FALSE);
        width = rect.right - rect.left; height = rect.bottom - rect.top;
        const auto& work = monitor.rcWork;
        if (width > work.right-work.left || height > work.bottom-work.top) return false;
        rect = {work.left + (work.right-work.left-width)/2, work.top + (work.bottom-work.top-height)/2,
                work.left + (work.right-work.left+width)/2, work.top + (work.bottom-work.top+height)/2};
    }
    ShowWindow(hwnd_, SW_RESTORE);
    SetWindowLongPtr(hwnd_, GWL_STYLE, style | WS_VISIBLE);
    return SetWindowPos(hwnd_, nullptr, rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
}

void WinApp::RestoreDisplayState(const DisplayState& state) {
    SetWindowLongPtr(hwnd_, GWL_STYLE, state.style);
    SetWindowPlacement(hwnd_, &state.placement);
    SetWindowPos(hwnd_, nullptr, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}

// Windowsメッセージを処理し、終了要求が来ていればtrueを返す。
bool WinApp::Update() {
    MSG msg{};

    // 保留中の入力通知をまとめて配送し、ImGuiの入力遅延を防ぐ。
    // 終了通知はDispatchせず、その場でメインループへ返す。
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return true;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (isCursorLocked_ && !SettingsMenuOverlay::IsAnyActive()) {
        // 他のアプリ（ブラウザ等）を操作している時にマウスが奪われないよう、
        // 自分のゲーム画面がアクティブ（最前面）の時だけ固定する！
        if (GetActiveWindow() == hwnd_) {
            RECT clientRect;
            GetClientRect(hwnd_, &clientRect);

            // クライアント領域（描画エリア）のド真ん中を計算
            POINT center = {
                (clientRect.right - clientRect.left) / 2,
                (clientRect.bottom - clientRect.top) / 2
            };

            // モニター全体の座標系に変換して、そこにマウスを強制テレポート！
            ClientToScreen(hwnd_, &center);
            SetCursorPos(center.x, center.y);
        }
    }
    // 続ける場合はfalseを返す
    return false;
}

// 即終了せず、ゲーム側で未保存確認などを挟めるよう終了要求だけを立てる。

void WinApp::RequestClose() {
    closeRequested_ = true;
}

// 保留中の終了要求を一度だけ取り出し、処理済みに戻す。

bool WinApp::ConsumeCloseRequest() {
    if (!closeRequested_) {
        return false;
    }
    closeRequested_ = false;
    return true;
}

// 確認なしでWM_QUITを発行し、アプリケーション終了へ進める。

void WinApp::CloseNow() {
    closeRequested_ = false;
    PostQuitMessage(0);
}

// ShowCursorの内部カウンタを調整し、カーソル表示状態を確実に切り替える。

void WinApp::SetCursorVisibility(bool isVisible) {
    if (isVisible) {
        // 確実に見えるようになる(0以上になる)まで TRUE を呼ぶ
        while (ShowCursor(TRUE) < 0);
    }
    else {
        // 確実に消える(0未満になる)まで FALSE を呼ぶ
        while (ShowCursor(FALSE) >= 0);
    }
}

// カーソルをウィンドウのクライアント領域内へ制限するか解除する。


void WinApp::SetCursorClipping(bool isClipping) {
    isCursorClipping_ = isClipping;
    if (isClipping) {
        // ウィンドウのクライアント領域（描画エリア）を取得
        RECT rect;
        GetClientRect(hwnd_, &rect);

        // 画面全体（モニター）の座標系に変換
        POINT topLeft = { rect.left, rect.top };
        POINT bottomRight = { rect.right, rect.bottom };
        ClientToScreen(hwnd_, &topLeft);
        ClientToScreen(hwnd_, &bottomRight);

        rect.left = topLeft.x;
        rect.top = topLeft.y;
        rect.right = bottomRight.x;
        rect.bottom = bottomRight.y;

        // カーソルを指定範囲に監禁！
        ClipCursor(&rect);
    }
    else {
        // 監禁を解除！
        ClipCursor(nullptr);
    }
}

// 毎フレームカーソルを画面中央へ戻すロック状態を切り替える。

void WinApp::SetCursorLocked(bool isLocked) {
    isCursorLocked_ = isLocked;
}
