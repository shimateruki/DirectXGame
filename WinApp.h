#pragma once
#include <Windows.h>
#include <string>

class WinApp {
public:
    static const int32_t kClientWidth = 1280;
    static const int32_t  kClientHeight = 720;
    // 初期化（ウィンドウを作成）
    void Initialize(const wchar_t* title = L"CG2", int width = kClientWidth, int height = kClientHeight);

    bool Update();

    // アクセサ
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return hInstance_; }
    ;

private:
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
};
