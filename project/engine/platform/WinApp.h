#pragma once
#include <Windows.h>
#include <string>
#include <cstdint>
class WinApp {
public:
    inline static int32_t kClientWidth = 1280;
    inline static int32_t kClientHeight = 720;
    // 初期化（ウィンドウを作成）
    void Initialize(const wchar_t* title = L"CG2", int width = kClientWidth, int height = kClientHeight);

    bool Update();

    // アクセサ
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return hInstance_; }
    
    static void SetCursorVisibility(bool isVisible);
     void SetCursorClipping(bool isClipping);
     void SetCursorLocked(bool isLocked);
private:
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    bool isCursorClipping_ = false;
    bool isCursorLocked_ = false;
};
