#include "InputManager.h" // 対応するヘッダーファイルをインクルード
#include <cassert>        // assertマクロを使用するためにインクルード
#include "math.h"         // abs()関数などを使用するためにインクルード
#include"DebugConsole.h"



InputManager* InputManager::GetInstance() {
    static InputManager instance;
    return &instance;
}


// InputManagerの初期化処理
void InputManager::Initialize(HWND hwnd)
{
    HRESULT result;
    hwnd_ = hwnd;

    // --- DirectInput初期化 (キーボード・マウス) ---
    result = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
    assert(SUCCEEDED(result));

    // キーボード設定
    result = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
    assert(SUCCEEDED(result));
    result = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));
    result = keyboardDevice->SetCooperativeLevel(hwnd_, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
    keyboardDevice->Acquire();

    // マウス設定
    result = directInput->CreateDevice(GUID_SysMouse, &mouseDevice, nullptr);
    assert(SUCCEEDED(result));
    result = mouseDevice->SetDataFormat(&c_dfDIMouse);
    assert(SUCCEEDED(result));
    result = mouseDevice->SetCooperativeLevel(hwnd_, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
    mouseDevice->Acquire();

    prevGamepadState = gamepadState;

    // 新しい状態を取得
    ZeroMemory(&gamepadState, sizeof(XINPUT_STATE));
    XInputGetState(0, &gamepadState);

    // --- SDL2初期化 (Joy-Con用) ---

    // 入力処理を別スレッド化（ラグ対策）
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR) < 0) {
        assert(false && "SDL Init Failed");
    }

    // 高頻度イベントを無視してCPU負荷を軽減
    SDL_EventState(SDL_CONTROLLERSENSORUPDATE, SDL_IGNORE);
    SDL_EventState(SDL_CONTROLLERAXISMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYAXISMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYBALLMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYHATMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYBUTTONDOWN, SDL_IGNORE);
    SDL_EventState(SDL_JOYBUTTONUP, SDL_IGNORE);

    // コントローラー接続
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            sdlController_ = SDL_GameControllerOpen(i);
            if (sdlController_) {
                // センサー有効化
                SDL_GameControllerSetSensorEnabled(sdlController_, (SDL_SensorType)SDL_SENSOR_ACCEL, SDL_TRUE);
                SDL_GameControllerSetSensorEnabled(sdlController_, (SDL_SensorType)SDL_SENSOR_GYRO, SDL_TRUE);

                // キャリブレーション状態の初期化
                baseAccel_ = { 0, 0, 0 };
                isCalibrated_ = false;
                break;
            }
        }
    }

}


// 毎フレームの更新処理
void InputManager::Update()
{
    // =================================================================
    // 1. DirectInput更新 (キーボード・マウス)
    // =================================================================

    // 前フレームの状態を保存
    memcpy(prevKeyState, keyState, sizeof(keyState));
    prevMouseState = mouseState;

    // キーボード情報の取得
    HRESULT result = keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    if (FAILED(result)) {
        keyboardDevice->Unacquire();
        while ((result = keyboardDevice->Acquire()) == DIERR_INPUTLOST) {}
        keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    }

    // マウス情報の取得
    result = mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    if (FAILED(result)) {
        mouseDevice->Unacquire();
        while ((result = mouseDevice->Acquire()) == DIERR_INPUTLOST) {}
        mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    }


    // =================================================================
    // 2. ゲームパッド状態の更新 (XInput & SDL2)
    // =================================================================
    prevGamepadState = gamepadState;

    // --- XInput (Xboxコントローラー) の更新 ---
    ZeroMemory(&gamepadState, sizeof(XINPUT_STATE));
    XInputGetState(0, &gamepadState);


    // --- SDL2 (Joy-Con / Proコン) の更新 ---
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}

    SDL_GameControllerUpdate();

    if (sdlController_) {
        // ... 加速度・ジャイロの処理 ...
        float data[3];
        bool hasData = false;
        if (SDL_GameControllerGetSensorData(sdlController_, (SDL_SensorType)SDL_SENSOR_ACCEL, data, 3) == 0) {
            accelData_ = { data[0], data[1], data[2] };
            hasData = true;
        }
        if (SDL_GameControllerGetSensorData(sdlController_, (SDL_SensorType)SDL_SENSOR_GYRO, data, 3) == 0) {
            gyroData_ = { data[0], data[1], data[2] };
        }
        if (!isCalibrated_ && hasData) {
            if (accelData_.x != 0.0f || accelData_.y != 0.0f || accelData_.z != 0.0f) {
                baseAccel_ = accelData_;
                isCalibrated_ = true;
            }
        }

        // --- SDLのスティック入力を取得して XInput形式 に統合 ---
        int16_t leftX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t leftY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTY);
        int16_t rightX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTX);
        int16_t rightY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTY);

        if (abs(leftX) > 0 || abs(leftY) > 0) {
            gamepadState.Gamepad.sThumbLX = leftX;
            if (leftY <= -32768) leftY = -32767;
            gamepadState.Gamepad.sThumbLY = (short)-leftY; // Y軸反転
        }
        if (abs(rightX) > 0 || abs(rightY) > 0) {
            gamepadState.Gamepad.sThumbRX = rightX;
            gamepadState.Gamepad.sThumbRY = (short)-rightY;
        }

        // トリガー (ZLR)
        int16_t trigLeft = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int16_t trigRight = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (trigLeft > 0) gamepadState.Gamepad.bLeftTrigger = (BYTE)(trigLeft >> 7);
        if (trigRight > 0) gamepadState.Gamepad.bRightTrigger = (BYTE)(trigRight >> 7);


        // --- Joy-Conのボタン入力を XInput形式 に統合 ---
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_B)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_A;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_A)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_B;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_Y)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_X;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_X)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_START)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_START;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_BACK)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
        }
        if (SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
            gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        }
    }

    // --- 仮想トリガーボタンの処理 ---
    const WORD VIRTUAL_BTN_LT = 0x0400;
    const WORD VIRTUAL_BTN_RT = 0x0800;
    const BYTE TRIGGER_THRESHOLD = 30;

    if (gamepadState.Gamepad.bLeftTrigger > TRIGGER_THRESHOLD) {
        gamepadState.Gamepad.wButtons |= VIRTUAL_BTN_LT;
    }
    else {
        gamepadState.Gamepad.wButtons &= ~VIRTUAL_BTN_LT;
    }

    if (gamepadState.Gamepad.bRightTrigger > TRIGGER_THRESHOLD) {
        gamepadState.Gamepad.wButtons |= VIRTUAL_BTN_RT;
    }
    else {
        gamepadState.Gamepad.wButtons &= ~VIRTUAL_BTN_RT;
    }


    SHORT deadzone = 16000; // 倒し込みのしきい値（最大32767）

    // 左スティックのY軸 (上下)
    if (gamepadState.Gamepad.sThumbLY > deadzone) {
        gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
    }
    else if (gamepadState.Gamepad.sThumbLY < -deadzone) {
        gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
    }

    // 左スティックのX軸 (左右)
    if (gamepadState.Gamepad.sThumbLX > deadzone) {
        gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    }
    else if (gamepadState.Gamepad.sThumbLX < -deadzone) {
        gamepadState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
    }


    // =================================================================
    // 3. キャリブレーション
    // =================================================================
    bool isPadReset = (sdlController_ && SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_BACK));
    if (IsKeyTriggered(DIK_C) || isPadReset) {
        Calibrate();
        DebugConsole::GetInstance()->AddLog("InputManager: Calibrated accelerometer.");
    }


    // =================================================================
    // 4. 操作モードの自動切り替え判定
    // =================================================================

    // --- A. キーボード・マウスの入力判定 ---
    bool isKeyMouseActive = false;

    // キーボード入力チェック
    for (int i = 0; i < 256; i++) {
        if (keyState[i] & 0x80) {
            isKeyMouseActive = true;
            break;
        }
    }

    const long MOUSE_MOVE_THRESHOLD = 5;
    if (abs(mouseState.lX) > MOUSE_MOVE_THRESHOLD ||
        abs(mouseState.lY) > MOUSE_MOVE_THRESHOLD ||
        abs(mouseState.lZ) > MOUSE_MOVE_THRESHOLD) {
        isKeyMouseActive = true;
    }

    // マウスボタンチェック
    for (int i = 0; i < 4; i++) {
        if (mouseState.rgbButtons[i] & 0x80) {
            isKeyMouseActive = true;
            break;
        }
    }

    if (isKeyMouseActive) {
        isGamepadMode_ = false;
    }


    // --- B. ゲームパッドの入力判定 ---
    bool isGamepadActive = false;

    if (gamepadState.Gamepad.wButtons != 0) {
        isGamepadActive = true;
    }

    const short DETECTION_DEADZONE = 2000;
    if (abs(gamepadState.Gamepad.sThumbLX) > DETECTION_DEADZONE ||
        abs(gamepadState.Gamepad.sThumbLY) > DETECTION_DEADZONE) {
        isGamepadActive = true;
    }
    if (abs(gamepadState.Gamepad.sThumbRX) > DETECTION_DEADZONE ||
        abs(gamepadState.Gamepad.sThumbRY) > DETECTION_DEADZONE) {
        isGamepadActive = true;
    }
    if (gamepadState.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        gamepadState.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        isGamepadActive = true;
    }

    if (isGamepadActive) {
        isGamepadMode_ = true;
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

// マウスホイールのスクロール量を取得
float InputManager::GetMouseWheelDelta() const {
    // DIMOUSESTATE構造体のlZメンバがホイールの移動量
    return (float)mouseState.lZ;
}
/// <summary>
/// マウスカーソルの「絶対座標」（ウィンドウ内）を取得する
/// </summary>
Vector2 InputManager::GetMousePosition() const {

    // (1) Windows API で「スクリーン全体」のカーソル位置を取得
    POINT screenPos;
    if (!GetCursorPos(&screenPos)) {
        return { 0.0f, 0.0f };
    }

    // (2) 「スクリーン全体」の座標を、「ウィンドウ内」の座標に変換
    // (※ hwnd_ が Initialize で保存されている必要がある)
    if (!ScreenToClient(hwnd_, &screenPos)) {
        return { 0.0f, 0.0f };
    }

    // (3) Vector2 にキャストして返す
    return { (float)screenPos.x, (float)screenPos.y };
}

/// <summary>
/// 指定されたマウスボタンがこのフレームで離された瞬間か (リリース)
/// </summary>
bool InputManager::IsMouseButtonReleased(int button) const {
    // (現在 離されている) かつ (前フレームでは 押されていた) 場合にtrue
    // (※ IsMouseButtonTriggered とロジックが逆)
    return !(mouseState.rgbButtons[button] & 0x80) && (prevMouseState.rgbButtons[button] & 0x80);
}

void InputManager::Finalize() {
    // コントローラーを閉じる
    if (sdlController_) {
        SDL_GameControllerClose(sdlController_);
        sdlController_ = nullptr;
    }

    // DirectInputデバイスの解放
    if (keyboardDevice) {
        keyboardDevice->Unacquire();
        keyboardDevice->Release();
        keyboardDevice = nullptr;
    }
    if (mouseDevice) {
        mouseDevice->Unacquire();
        mouseDevice->Release();
        mouseDevice = nullptr;
    }
    if (directInput) {
        directInput->Release();
        directInput = nullptr;
    }

    // SDL2の終了
    SDL_Quit();
}

Vector2 InputManager::GetRightStick() const {
    // 1. まず XInput (Xboxコントローラー) をチェック
    Vector2 input = GetGamepadRightStick();

    // 入力があればそれを返す
    if (abs(input.x) > 0.0f || abs(input.y) > 0.0f) {
        return input;
    }

    // 2. XInputがない場合、SDL2 (Joy-Con / Proコン) をチェック
    if (sdlController_) {
        // SDLの軸の値は -32768 ～ 32767
        Sint16 axisX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTX);
        Sint16 axisY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTY);

        // デッドゾーン (遊び) の設定
        const int kDeadZone = 3000;

        float x = 0.0f;
        float y = 0.0f;

        if (abs(axisX) > kDeadZone) {
            x = (float)axisX / 32768.0f;
        }
        if (abs(axisY) > kDeadZone) {
            y = ((float)axisY / 32768.0f) * -1.0f;
        }

        return { x, y };
    }

    // どちらも入力がなければゼロを返す
    return { 0.0f, 0.0f };
}
bool InputManager::IsGamepadButtonTriggered(WORD button) const {
    // 「今は押されている」かつ「前は押されていなかった」なら true
    return (gamepadState.Gamepad.wButtons & button) && !(prevGamepadState.Gamepad.wButtons & button);
}

Vector2 InputManager::GetLeftStick() const {
    // 1. まず XInput (Xboxコントローラー) をチェック
    Vector2 input = GetGamepadLeftStick();

    // 入力があればそれを返す
    if (abs(input.x) > 0.0f || abs(input.y) > 0.0f) {
        return input;
    }

    // 2. XInputがない場合、SDL2 (Joy-Con / Proコン) をチェック
    if (sdlController_) {
        // 右スティックのコードをコピーして、AXIS_LEFTX / LEFTY に変更
        Sint16 axisX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 axisY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTY);

        const int kDeadZone = 3000;
        float x = 0.0f;
        float y = 0.0f;

        if (abs(axisX) > kDeadZone) {
            x = (float)axisX / 32768.0f;
        }
        if (abs(axisY) > kDeadZone) {
            // SDLのY軸は上がマイナスなので、反転(-1.0f)させて上をプラスにする
            y = ((float)axisY / 32768.0f) * -1.0f;
        }

        return { x, y };
    }

    return { 0.0f, 0.0f };
}

// 現在押されている全キーのリストを取得する
std::vector<uint8_t> InputManager::GetPressedKeys() const {
    std::vector<uint8_t> pressedKeys;

    // 0〜255の全キーコードを走査
    for (int i = 0; i < 256; ++i) {
        if (keyState[i] & 0x80) { // 押されていたら
            pressedKeys.push_back(static_cast<uint8_t>(i));
        }
    }

    return pressedKeys;
}

WORD InputManager::GetPressedGamepadButton() const {
    // 全てのボタン＋仮想トリガーを網羅
    const WORD buttons[] = {
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        0x0400, 0x0800, // LT, RT
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB, // L3, R3
        XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK
    };

    for (WORD btn : buttons) {
        if (gamepadState.Gamepad.wButtons & btn) {
            return btn;
        }
    }
    return 0; // 何も押されていない
}
// 現在押されているマウスボタンを取得する
int InputManager::GetPressedMouseButton() const {
    for (int i = 0; i < 4; ++i) { // 標準的な4ボタンをチェック
        if (mouseState.rgbButtons[i] & 0x80) {
            return i;
        }
    }
    return -1; // 何も押されていない
}
// =================================================================
// ★ サブキー対応版の入力判定関数群
// =================================================================

bool InputManager::IsActionPressed(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // ① キーボード (メインキー または サブキーが押されているか)
    if (data->keyCode != 0 && IsKeyPressed(static_cast<BYTE>(data->keyCode))) return true;
    if (data->keyCodeSub != 0 && IsKeyPressed(static_cast<BYTE>(data->keyCodeSub))) return true;

    // ② マウス
    if (data->mouseButton != -1 && IsMouseButtonPressed(data->mouseButton)) return true;

    // ③ パッド
    if (data->padCode != 0 && IsGamepadButtonPressed(data->padCode)) return true;

    return false;
}

bool InputManager::IsActionTriggered(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // ① キーボード (メインキー または サブキーが押された瞬間か)
    if (data->keyCode != 0 && IsKeyTriggered(static_cast<BYTE>(data->keyCode))) return true;
    if (data->keyCodeSub != 0 && IsKeyTriggered(static_cast<BYTE>(data->keyCodeSub))) return true;

    // ② マウス
    if (data->mouseButton != -1 && IsMouseButtonTriggered(data->mouseButton)) return true;

    // ③ パッド
    if (data->padCode != 0 && IsGamepadButtonTriggered(data->padCode)) return true;

    return false;
}

bool InputManager::IsActionReleased(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // ① キーボード (メインキー または サブキーを離した瞬間か)
    if (data->keyCode != 0 && !(keyState[data->keyCode] & 0x80) && (prevKeyState[data->keyCode] & 0x80)) return true;
    if (data->keyCodeSub != 0 && !(keyState[data->keyCodeSub] & 0x80) && (prevKeyState[data->keyCodeSub] & 0x80)) return true;

    // ② マウス (離した判定)
    if (data->mouseButton != -1 && IsMouseButtonReleased(data->mouseButton)) return true;

    // ③ パッド (離した判定：wButtonsのビットが落ちた瞬間)
    if (data->padCode != 0 && !(gamepadState.Gamepad.wButtons & data->padCode) && (prevGamepadState.Gamepad.wButtons & data->padCode)) return true;

    return false;
}