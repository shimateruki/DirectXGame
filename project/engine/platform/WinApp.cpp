#include "WinApp.h"
#include <Windows.h>
#ifdef USE_IMGUI
#include "imgui_impl_win32.h"
#endif
#include <DirectXCommon.h>

#pragma comment(lib, "winmm.lib")
#ifdef USE_IMGUI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif

// --- WinApp.cpp ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }
#endif

    switch (msg) {
    case WM_SIZE: // ★ウィンドウサイズが変わった
        if (wparam != SIZE_MINIMIZED) {
            int32_t width = LOWORD(lparam);
            int32_t height = HIWORD(lparam);
            WinApp::kClientWidth = width;
            WinApp::kClientHeight = height;
            if (DirectXCommon::GetInstance()->GetDevice()) {
                DirectXCommon::GetInstance()->ResizeSwapChain(width, height);
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

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
}
bool WinApp::Update() {
    MSG msg{};

    // メッセージがあるか
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 終了メッセージが来たらtrueを返す
    if (msg.message == WM_QUIT) {
        return true;
    }

    // 続ける場合はfalseを返す
    return false;
}

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