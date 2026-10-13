#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

/// <summary>Win32ウィンドウの生成、メッセージ処理、表示とカーソルを管理する。</summary>
class WinApp {
public:
    inline static int32_t kClientWidth = 1280;
    inline static int32_t kClientHeight = 720;

    /// <summary>指定したタイトルとサイズでゲームウィンドウを作成する。</summary>
    void Initialize(const wchar_t* title = L"CG2", int width = kClientWidth, int height = kClientHeight);

    /// <summary>Windowsメッセージを処理し、アプリを継続できるか返す。</summary>
    bool Update();

    // アクセサ
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return hInstance_; }

    /// <summary>表示モード変更前のスタイルとウィンドウ位置を保持する。</summary>
    struct DisplayState {
        LONG_PTR style = 0;
        WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
    };

    /// <summary>現在の表示状態を保存用に取得する。</summary>
    DisplayState CaptureDisplayState() const;

    /// <summary>指定解像度と枠なし表示を適用できたか返す。</summary>
    bool SetDisplayMode(int width, int height, bool borderless);

    /// <summary>保存済みのウィンドウスタイルと配置へ戻す。</summary>
    void RestoreDisplayState(const DisplayState& state);

    /// <summary>他システムからウィンドウ終了を要求する。</summary>
    static void RequestClose();

    /// <summary>保留中の終了要求を取得して消費する。</summary>
    static bool ConsumeCloseRequest();

    /// <summary>ウィンドウを直ちに閉じる。</summary>
    static void CloseNow();

    /// <summary>カーソル表示を切り替える。</summary>
    static void SetCursorVisibility(bool isVisible);

    /// <summary>カーソルをウィンドウ内へ制限するか切り替える。</summary>
    void SetCursorClipping(bool isClipping);

    /// <summary>カーソル位置をウィンドウ内に固定するか切り替える。</summary>
    void SetCursorLocked(bool isLocked);

    /// <summary>メニュー操作中など、一時的にカーソルの中央戻しを止める。</summary>
    void SetCursorRecenteringSuppressed(bool suppressed) { cursorRecenteringSuppressed_ = suppressed; }

private:
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    bool isCursorClipping_ = false;
    bool isCursorLocked_ = false;
    bool cursorRecenteringSuppressed_ = false;

    inline static bool closeRequested_ = false;
};
