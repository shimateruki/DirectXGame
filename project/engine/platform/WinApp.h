#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

/// <summary>Win32ウィンドウの生成、メッセージ処理、表示とカーソルを管理する。</summary>
class WinApp {
public:
    // ゲームとSpriteの基準座標。実ウィンドウのサイズやDPIから独立させます。
    inline static constexpr int32_t kClientWidth = 1920;
    inline static constexpr int32_t kClientHeight = 1080;
    static constexpr int32_t GetClientWidth() { return kClientWidth; }
    static constexpr int32_t GetClientHeight() { return kClientHeight; }
    static int32_t GetWindowWidth() { return kWindowWidth; }
    static int32_t GetWindowHeight() { return kWindowHeight; }
    /// <summary>論理解像度を変えず、実ウィンドウの幅・高さを一緒に更新する。</summary>
    static void SetWindowSize(int32_t width, int32_t height) { kWindowWidth = width; kWindowHeight = height; }

    struct GameViewRect {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    };

    // 実ピクセルのクライアント内へ16:9の映像を中央配置します。
    static GameViewRect CalculateGameViewRect(int32_t width, int32_t height) {
        if (width <= 0 || height <= 0)
            return {};
        const float scaleX = static_cast<float>(width) / kClientWidth;
        const float scaleY = static_cast<float>(height) / kClientHeight;
        const float scale = scaleX < scaleY ? scaleX : scaleY;
        const float viewWidth = kClientWidth * scale;
        const float viewHeight = kClientHeight * scale;
        return {(width - viewWidth) * 0.5f, (height - viewHeight) * 0.5f, viewWidth, viewHeight};
    }

    // 表示矩形を逆変換します。黒帯・無効領域はfalseを返し、UIの操作対象にしません。
    static bool ClientToGamePosition(float x, float y, const GameViewRect& view, float& gameX, float& gameY) {
        if (view.width <= 0.0f || view.height <= 0.0f ||
            x < view.x || y < view.y || x >= view.x + view.width || y >= view.y + view.height) {
            gameX = gameY = -1.0f;
            return false;
        }
        gameX = (x - view.x) * kClientWidth / view.width;
        gameY = (y - view.y) * kClientHeight / view.height;
        return true;
    }

    /// <summary>指定したタイトルとサイズでゲームウィンドウを作成する。</summary>
    void Initialize(const wchar_t* title = L"CG2", int width = 1280, int height = 720);

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
    inline static int32_t kWindowWidth = 1280;
    inline static int32_t kWindowHeight = 720;
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    bool isCursorClipping_ = false;
    bool isCursorLocked_ = false;
    bool cursorRecenteringSuppressed_ = false;

    inline static bool closeRequested_ = false;
};
