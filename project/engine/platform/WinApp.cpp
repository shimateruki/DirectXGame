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
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) {
            ClipCursor(nullptr); // 解放
        }
        break;
    case WM_SIZE: // ★ウィンドウサイズが変わった
        if (wparam != SIZE_MINIMIZED) {
            int32_t width = LOWORD(lparam);
            int32_t height = HIWORD(lparam);
            WinApp::kWindowWidth = width;
            WinApp::kWindowHeight = height;
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

    // ==========================================
    // ここからフルスクリーン（ボーダーレス）用の修正
    // ==========================================

    // モニターの画面解像度（縦横のピクセル数）を取得
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // ゲーム内の基準解像度は固定し、ウィンドウだけ実画面サイズに合わせる
    kWindowWidth = screenWidth;
    kWindowHeight = screenHeight;

    // ウィンドウ作成
    // ★ WS_POPUP を指定することで、タイトルバーやサイズ変更枠を完全に消去します
    hwnd_ = CreateWindow(
        wc.lpszClassName,
        title,
        WS_POPUP, // 枠なし・リサイズ不可のスタイル
        0, 0,     // 画面の左上(0, 0)から表示
        screenWidth,
        screenHeight,
        nullptr, nullptr,
        hInstance_,
        nullptr);

    // フルスクリーンで表示（最大化ボタンの挙動ではなく、そのまま表示）
    ShowWindow(hwnd_, SW_SHOW);


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
    if (isCursorLocked_) {
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

void WinApp::SetCursorLocked(bool isLocked) {
    isCursorLocked_ = isLocked;
}
