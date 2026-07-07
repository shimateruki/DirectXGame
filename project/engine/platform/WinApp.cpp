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
}
// Windowsメッセージを処理し、終了要求が来ていればtrueを返す。
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
