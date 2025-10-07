#include "InputManager.h" // 対応するヘッダーファイルをインクルード
#include <cassert>        // assertマクロを使用するためにインクルード
#include "math.h"         // abs()関数などを使用するためにインクルード

// InputManagerの初期化処理
void InputManager::Initialize(HWND hwnd)
{
    HRESULT result;

    // DirectInputのインターフェースを作成
    result = DirectInput8Create(
        GetModuleHandle(nullptr),       // アプリケーションのインスタンスハンドル
        DIRECTINPUT_VERSION,            // DirectInputのバージョン
        IID_IDirectInput8,              // 作成するインターフェースのID
        reinterpret_cast<void**>(&directInput), // 作成されたインターフェースを格納するポインタ
        nullptr);
    assert(SUCCEEDED(result)); // 成功したかチェック

    // --- キーボードデバイスの初期化 ---
    // デバイスの作成
    result = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
    assert(SUCCEEDED(result));
    // データフォーマットの設定
    result = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));
    // 協調レベルの設定 (フォアグラウンドかつ非排他的)
    // DISCL_FOREGROUND: ウィンドウがアクティブな時だけ入力を受け取る
    // DISCL_NONEXCLUSIVE: 他のアプリケーションもデバイスにアクセスできる
    result = keyboardDevice->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
    // デバイスの制御を開始 (入力を受け取れるようにする)
    keyboardDevice->Acquire();

    // --- マウスデバイスの初期化 ---
    // デバイスの作成
    result = directInput->CreateDevice(GUID_SysMouse, &mouseDevice, nullptr);
    assert(SUCCEEDED(result));
    // データフォーマットの設定
    result = mouseDevice->SetDataFormat(&c_dfDIMouse);
    assert(SUCCEEDED(result));
    // 協調レベルの設定
    result = mouseDevice->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
    // デバイスの制御を開始
    mouseDevice->Acquire();
}

// 毎フレームの更新処理
void InputManager::Update()
{
    // トリガー判定のために、現在の状態を前フレームの状態としてコピーする
    memcpy(prevKeyState, keyState, sizeof(keyState));
    prevMouseState = mouseState;

    // --- キーボードの状態取得 ---
    HRESULT result = keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    if (FAILED(result)) { // 状態取得に失敗した場合 (ウィンドウがフォーカスを失ったなど)
        keyboardDevice->Unacquire(); // 一旦デバイスの制御を解放し、
        // 再度制御を取得できるまでループで試みる
        while ((result = keyboardDevice->Acquire()) == DIERR_INPUTLOST) {}
        // 再取得後に、もう一度状態を取得する
        keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    }

    // --- マウスの状態取得 ---
    result = mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    if (FAILED(result)) { // キーボードと同様に、失敗した場合は再取得を試みる
        mouseDevice->Unacquire();
        while ((result = mouseDevice->Acquire()) == DIERR_INPUTLOST) {}
        mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    }

    // --- ゲームパッドの状態取得 ---
    // 状態を格納する構造体をゼロクリア
    ZeroMemory(&gamepadState, sizeof(XINPUT_STATE));
    // 0番目のコントローラーの状態を取得
    DWORD padResult = XInputGetState(0, &gamepadState);
    if (padResult != ERROR_SUCCESS) {
        // コントローラーが接続されていない場合は、状態を再度ゼロクリアしておく
        ZeroMemory(&gamepadState, sizeof(XINPUT_STATE));
    }
}

// 指定されたキーが押されているか
bool InputManager::IsKeyPressed(BYTE keyCode) const {
    // キーの状態の最上位ビットが1であれば、キーは押されている
    return (keyState[keyCode] & 0x80) != 0;
}

// 指定されたキーがこのフレームで押された瞬間か (トリガー)
bool InputManager::IsKeyTriggered(BYTE keyCode) const {
    // (現在押されている) かつ (前フレームでは押されていない) 場合にtrue
    return (keyState[keyCode] & 0x80) && !(prevKeyState[keyCode] & 0x80);
}

// 指定されたマウスボタンが押されているか
bool InputManager::IsMouseButtonPressed(int button) const {
    // マウスボタンの状態の最上位ビットが1であれば、ボタンは押されている
    return (mouseState.rgbButtons[button] & 0x80) != 0;
}

// 指定されたマウスボタンがこのフレームで押された瞬間か (トリガー)
bool InputManager::IsMouseButtonTriggered(int button) const {
    // (現在押されている) かつ (前フレームでは押されていない) 場合にtrue
    return (mouseState.rgbButtons[button] & 0x80) && !(prevMouseState.rgbButtons[button] & 0x80);
}

// マウスの移動量を取得
Vector2 InputManager::GetMouseMoveDelta() const {
    // DIMOUSESTATE構造体から相対移動量を取得する
    return { (float)mouseState.lX, (float)mouseState.lY };
}

// ゲームパッドの左スティックの入力を取得
Vector2 InputManager::GetGamepadLeftStick() const {
    const SHORT deadZone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE; // デッドゾーンの閾値
    SHORT lx = gamepadState.Gamepad.sThumbLX; // 生の入力値 (-32768 ~ 32767)
    SHORT ly = gamepadState.Gamepad.sThumbLY;

    // デッドゾーン内の入力を0として扱う
    // デッドゾーン外なら、値を-1.0f ~ 1.0fの範囲に正規化する
    float x = (abs(lx) > deadZone) ? lx / 32768.0f : 0.0f;
    float y = (abs(ly) > deadZone) ? ly / 32768.0f : 0.0f;
    return { x, y };
}

// ゲームパッドの右スティックの入力を取得
Vector2 InputManager::GetGamepadRightStick() const {
    const SHORT deadZone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE; // デッドゾーンの閾値
    SHORT rx = gamepadState.Gamepad.sThumbRX; // 生の入力値
    SHORT ry = gamepadState.Gamepad.sThumbRY;

    // 左スティックと同様に、デッドゾーン処理と正規化を行う
    float x = (abs(rx) > deadZone) ? rx / 32768.0f : 0.0f;
    float y = (abs(ry) > deadZone) ? ry / 32768.0f : 0.0f;
    return { x, y };
}

// ゲームパッドのボタンが押されているか
bool InputManager::IsGamepadButtonPressed(WORD button) const {
    // wButtonsはビットマスク。指定されたボタンのビットが立っているかを調べる
    return (gamepadState.Gamepad.wButtons & button) != 0;
}