#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

// Windows ウィンドウの生成、更新、カーソル制御をまとめるクラス
class WinApp {
public:
    inline static int32_t kClientWidth = 1280;
    inline static int32_t kClientHeight = 720;

    // ウィンドウを生成する
    void Initialize(const wchar_t* title = L"CG2", int width = kClientWidth, int height = kClientHeight);

    // メッセージを処理し、継続可能なら true を返す
    bool Update();

    // アクセサ
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return hInstance_; }

    // 外部から終了要求を出すための静的 API
    static void RequestClose();
    static bool ConsumeCloseRequest();
    static void CloseNow();

    static void SetCursorVisibility(bool isVisible);
    void SetCursorClipping(bool isClipping);
    void SetCursorLocked(bool isLocked);

private:
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    bool isCursorClipping_ = false;
    bool isCursorLocked_ = false;

    inline static bool closeRequested_ = false;
};
