#include "InputManager.h" // 対応するヘッダーファイルをインクルード
#include <cassert>        // assertマクロを使用するためにインクルード
#include "math.h"         // abs()関数などを使用するためにインクルード
#include <algorithm>
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
        // ... (加速度・ジャイロの処理はそのまま) ...
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

        // -------------------------------------------------------------
        // SDLのStick入力を取得し、内部のXInput互換形式へ変換します。
        // -------------------------------------------------------------
        // SDLの軸入力(-32768 ～ 32767)を取得
        int16_t leftX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t leftY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTY);
        int16_t rightX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTX);
        int16_t rightY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTY);

        // 値が入っている場合のみ上書き (XInput側が0の場合などを考慮)
        // ここではDevice切替用の入力有無だけを見るため、SDLとXInputのY軸符号差は補正しません。
        // 必要なら: leftY = -leftY; のように反転
        if (abs(leftX) > 0 || abs(leftY) > 0) {
            gamepadState.Gamepad.sThumbLX = leftX;
            gamepadState.Gamepad.sThumbLY = (short)-leftY; // Y軸反転させておくのが一般的
        }
        if (abs(rightX) > 0 || abs(rightY) > 0) {
            gamepadState.Gamepad.sThumbRX = rightX;
            gamepadState.Gamepad.sThumbRY = (short)-rightY;
        }

        // トリガー (ZLR)
        int16_t trigLeft = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int16_t trigRight = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        // SDLトリガー(0~32767) を XInput(0~255) に合わせるには >> 7 くらい
        if (trigLeft > 0) gamepadState.Gamepad.bLeftTrigger = (BYTE)(trigLeft >> 7);
        if (trigRight > 0) gamepadState.Gamepad.bRightTrigger = (BYTE)(trigRight >> 7);


        // -------------------------------------------------------------
        // B. Joy-Conのボタン入力を XInput形式 に変換して統合 (既存コード)
        // -------------------------------------------------------------
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


    // =================================================================
    // 3. キャリブレーション
    // =================================================================
    bool isPadReset = (sdlController_ && SDL_GameControllerGetButton(sdlController_, SDL_CONTROLLER_BUTTON_BACK));
    if (IsKeyTriggered(DIK_C) || isPadReset) {
        Calibrate();
        DebugConsole::GetInstance()->AddLog("InputManager: Calibrated accelerometer.");
    }


    // =================================================================
    // 入力があったDeviceへ操作表示を自動切り替えします。
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

    // Mouseの微小な揺れでDevice表示が切り替わらないよう閾値を設けます。
    // マウスは触れてなくてもセンサーの誤差で 1〜2 動くことがあるため、
    // 明らかに動かしたと判定できる数値(例えば 5程度)以上で反応させる
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

    // キーボード/マウス入力があればフラグを下ろす
    if (isKeyMouseActive) {
        isGamepadMode_ = false;
    }


    // --- B. ゲームパッドの入力判定 ---
    bool isGamepadActive = false;

    // ボタン入力チェック
    if (gamepadState.Gamepad.wButtons != 0) {
        isGamepadActive = true;
    }

    // Device切替検出では通常操作より小さいStick入力も検出します。
    // デフォルトの XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE (7849) はゲーム操作用には良いが、
    // Device切替検出は小さなStick操作も拾えるよう、通常入力より低い閾値を使います。

    // モード切替検知用の閾値 (少し触れたら反応するように小さくする: 例 2000)
    const short DETECTION_DEADZONE = 2000;

    // 左スティック
    if (abs(gamepadState.Gamepad.sThumbLX) > DETECTION_DEADZONE ||
        abs(gamepadState.Gamepad.sThumbLY) > DETECTION_DEADZONE) {
        isGamepadActive = true;
    }
    // 右スティック
    if (abs(gamepadState.Gamepad.sThumbRX) > DETECTION_DEADZONE ||
        abs(gamepadState.Gamepad.sThumbRY) > DETECTION_DEADZONE) {
        isGamepadActive = true;
    }
    // トリガー入力チェック (閾値を超える入力があるか)
    if (gamepadState.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        gamepadState.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        isGamepadActive = true;
    }

    // ゲームパッド入力があればフラグを立てる (後勝ち判定)
    if (isGamepadActive) {
        isGamepadMode_ = true;
    }
}

InputManager::ReplayState InputManager::CaptureReplayState() const {
    ReplayState result;
    std::copy(std::begin(keyState), std::end(keyState), result.keys.begin());
    std::copy(std::begin(prevKeyState), std::end(prevKeyState), result.previousKeys.begin());
    std::copy(std::begin(mouseState.rgbButtons), std::end(mouseState.rgbButtons), result.mouseButtons.begin());
    std::copy(std::begin(prevMouseState.rgbButtons), std::end(prevMouseState.rgbButtons), result.previousMouseButtons.begin());
    result.mouseX = mouseState.lX;
    result.mouseY = mouseState.lY;
    result.mouseWheel = mouseState.lZ;
    result.previousMouseX = prevMouseState.lX;
    result.previousMouseY = prevMouseState.lY;
    result.previousMouseWheel = prevMouseState.lZ;

    const XINPUT_GAMEPAD& pad = gamepadState.Gamepad;
    const XINPUT_GAMEPAD& previousPad = prevGamepadState.Gamepad;
    result.gamepadButtons = pad.wButtons;
    result.previousGamepadButtons = previousPad.wButtons;
    result.leftTrigger = pad.bLeftTrigger;
    result.rightTrigger = pad.bRightTrigger;
    result.previousLeftTrigger = previousPad.bLeftTrigger;
    result.previousRightTrigger = previousPad.bRightTrigger;
    result.leftX = pad.sThumbLX;
    result.leftY = pad.sThumbLY;
    result.rightX = pad.sThumbRX;
    result.rightY = pad.sThumbRY;
    result.previousLeftX = previousPad.sThumbLX;
    result.previousLeftY = previousPad.sThumbLY;
    result.previousRightX = previousPad.sThumbRX;
    result.previousRightY = previousPad.sThumbRY;
    result.gamepadMode = isGamepadMode_;
    result.accelerometer = accelData_;
    result.gyroscope = gyroData_;
    result.baseAccelerometer = baseAccel_;
    return result;
}

void InputManager::ApplyReplayState(const ReplayState& state) {
    std::copy(state.keys.begin(), state.keys.end(), std::begin(keyState));
    std::copy(state.previousKeys.begin(), state.previousKeys.end(), std::begin(prevKeyState));

    mouseState = {};
    prevMouseState = {};
    std::copy(state.mouseButtons.begin(), state.mouseButtons.end(), std::begin(mouseState.rgbButtons));
    std::copy(state.previousMouseButtons.begin(), state.previousMouseButtons.end(), std::begin(prevMouseState.rgbButtons));
    mouseState.lX = state.mouseX;
    mouseState.lY = state.mouseY;
    mouseState.lZ = state.mouseWheel;
    prevMouseState.lX = state.previousMouseX;
    prevMouseState.lY = state.previousMouseY;
    prevMouseState.lZ = state.previousMouseWheel;

    gamepadState = {};
    prevGamepadState = {};
    XINPUT_GAMEPAD& pad = gamepadState.Gamepad;
    XINPUT_GAMEPAD& previousPad = prevGamepadState.Gamepad;
    pad.wButtons = state.gamepadButtons;
    previousPad.wButtons = state.previousGamepadButtons;
    pad.bLeftTrigger = state.leftTrigger;
    pad.bRightTrigger = state.rightTrigger;
    previousPad.bLeftTrigger = state.previousLeftTrigger;
    previousPad.bRightTrigger = state.previousRightTrigger;
    pad.sThumbLX = state.leftX;
    pad.sThumbLY = state.leftY;
    pad.sThumbRX = state.rightX;
    pad.sThumbRY = state.rightY;
    previousPad.sThumbLX = state.previousLeftX;
    previousPad.sThumbLY = state.previousLeftY;
    previousPad.sThumbRX = state.previousRightX;
    previousPad.sThumbRY = state.previousRightY;
    isGamepadMode_ = state.gamepadMode;
    accelData_ = state.accelerometer;
    gyroData_ = state.gyroscope;
    baseAccel_ = state.baseAccelerometer;
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
    if (button < 0 || button >= static_cast<int>(sizeof(mouseState.rgbButtons))) {
        return false;
    }
    // マウスボタンの状態の最上位ビットが1であれば、ボタンは押されている
    return (mouseState.rgbButtons[button] & 0x80) != 0;
}

// 指定されたマウスボタンがこのフレームで押された瞬間か (トリガー)
bool InputManager::IsMouseButtonTriggered(int button) const {
    if (button < 0 || button >= static_cast<int>(sizeof(mouseState.rgbButtons))) {
        return false;
    }
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
    // hwnd_はInitializeで設定済みであることを前提とします。
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
    if (button < 0 || button >= static_cast<int>(sizeof(mouseState.rgbButtons))) {
        return false;
    }
    // (現在 離されている) かつ (前フレームでは 押されていた) 場合にtrue
    // Triggeredとは逆に、前Frameだけ押されていた場合を検出します。
    return !(mouseState.rgbButtons[button] & 0x80) && (prevMouseState.rgbButtons[button] & 0x80);
}

void InputManager::PlayRumble(float lowFrequency, float highFrequency, float duration) {
    if (duration <= 0.0f) {
        return;
    }

    RumbleRequest request;
    request.lowFrequency = std::clamp(lowFrequency, 0.0f, 1.0f);
    request.highFrequency = std::clamp(highFrequency, 0.0f, 1.0f);
    request.remaining = duration;
    rumbleRequests_.push_back(request);
    // 同じ強さの要求でも継続時間が延びるため、SDL側の終了時刻を更新します。
    appliedLowFrequency_ = -1.0f;
    RefreshRumbleOutput();
}

void InputManager::UpdateRumble(float unscaledDeltaTime) {
    const float timeStep = std::max(unscaledDeltaTime, 0.0f);
    for (RumbleRequest& request : rumbleRequests_) {
        request.remaining -= timeStep;
    }
    rumbleRequests_.erase(
        std::remove_if(rumbleRequests_.begin(), rumbleRequests_.end(), [](const RumbleRequest& request) {
            return request.remaining <= 0.0f;
        }),
        rumbleRequests_.end());
    RefreshRumbleOutput();
}

void InputManager::StopRumble() {
    rumbleRequests_.clear();
    RefreshRumbleOutput();
}

void InputManager::RefreshRumbleOutput() {
    float lowFrequency = 0.0f;
    float highFrequency = 0.0f;
    float longestRemaining = 0.0f;
    for (const RumbleRequest& request : rumbleRequests_) {
        lowFrequency = std::max(lowFrequency, request.lowFrequency);
        highFrequency = std::max(highFrequency, request.highFrequency);
        longestRemaining = std::max(longestRemaining, request.remaining);
    }

    if (lowFrequency == appliedLowFrequency_ && highFrequency == appliedHighFrequency_) {
        return;
    }

    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = static_cast<WORD>(lowFrequency * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(highFrequency * 65535.0f);
    XInputSetState(0, &vibration);

    if (sdlController_) {
        const Uint16 low = static_cast<Uint16>(lowFrequency * 65535.0f);
        const Uint16 high = static_cast<Uint16>(highFrequency * 65535.0f);
        const Uint32 durationMs = static_cast<Uint32>(std::clamp(longestRemaining * 1000.0f, 0.0f, 60000.0f));
        SDL_GameControllerRumble(sdlController_, low, high, durationMs);
    }

    appliedLowFrequency_ = lowFrequency;
    appliedHighFrequency_ = highFrequency;
}

void InputManager::Finalize() {
    StopRumble();

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

// 現在押されているゲームパッドのボタンを取得する
WORD InputManager::GetPressedGamepadButton() const {
    // XInputのボタンはビットマスクなので、代表的なものを順番にチェックする
    const WORD buttons[] = {
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER,
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB
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
bool InputManager::IsActionPressed(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // Keyboard。
    if (data->keyCode != 0 && IsKeyPressed(static_cast<BYTE>(data->keyCode))) return true;

    // Mouse。
    if (data->mouseButton != -1 && IsMouseButtonPressed(data->mouseButton)) return true;

    // Gamepad。
    if (data->padCode != 0 && IsGamepadButtonPressed(data->padCode)) return true;

    return false;
}

// アクションが「押された瞬間か」の判定
bool InputManager::IsActionTriggered(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // Keyboard。
    if (data->keyCode != 0 && IsKeyTriggered(static_cast<BYTE>(data->keyCode))) return true;

    // Mouse。
    if (data->mouseButton != -1 && IsMouseButtonTriggered(data->mouseButton)) return true;

    // Gamepad。
    if (data->padCode != 0 && IsGamepadButtonTriggered(data->padCode)) return true;

    return false;
}

bool InputManager::IsActionReleased(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // Keyboard。
    if (data->keyCode != 0 && !(keyState[data->keyCode] & 0x80) && (prevKeyState[data->keyCode] & 0x80)) return true;

    // MouseのRelease。
    if (data->mouseButton != -1 && IsMouseButtonReleased(data->mouseButton)) return true;

    // GamepadのRelease。wButtonsのBitが落ちたFrameを検出します。
    if (data->padCode != 0 && !(gamepadState.Gamepad.wButtons & data->padCode) && (prevGamepadState.Gamepad.wButtons & data->padCode)) return true;

    return false;
}
