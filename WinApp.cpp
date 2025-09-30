#include "WinApp.h"
#include <Windows.h>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void WinApp::Initialize(const wchar_t* title, int width, int height) {
    hInstance_ = GetModuleHandle(nullptr);

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
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wrc.right - wrc.left,
        wrc.bottom - wrc.top,
        nullptr, nullptr,
        hInstance_,
        nullptr);

    ShowWindow(hwnd_, SW_SHOW);
}

void WinApp::Update() {
    MSG msg{};
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}