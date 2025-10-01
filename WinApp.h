#pragma once
#include <Windows.h>
#include <string>

class WinApp {
public:
    static const int32_t kClientWidth = 1280;
    static const int32_t  kClientHeight = 720;
    // �������i�E�B���h�E���쐬�j
    void Initialize(const wchar_t* title = L"CG2", int width = kClientWidth, int height = kClientHeight);

    bool Update();

    // �A�N�Z�T
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return hInstance_; }
    ;

private:
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
};
