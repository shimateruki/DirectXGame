#include "InputManager.h" // 対応するヘッダーファイルをインクルード
#include <cassert>        // assertマクロを使用するためにインクルード
#include "math.h"         // abs()関数などを使用するためにインクルード
#include <algorithm>
#include <cmath>
#include"DebugConsole.h"

namespace {
// 円形デッドゾーンを除いたスティック入力を長さ0～1のベクトルへ変換します。
engine::Vector2 NormalizeStick(SHORT rawX, SHORT rawY, SHORT deadZone) {
    const float x = static_cast<float>(rawX) / 32768.0f;
    const float y = static_cast<float>(rawY) / 32768.0f;
    const float length = std::hypot(x, y);
    const float threshold = static_cast<float>(deadZone) / 32768.0f;
    if (length <= threshold) return {0.0f, 0.0f};
    // 円形デッドゾーンを差し引き、倒した量が移動速度へ滑らかにつながるようにします。
    const float magnitude = std::clamp((length - threshold) / (1.0f - threshold), 0.0f, 1.0f);
    return {x / length * magnitude, y / length * magnitude};
}
}

InputManager* InputManager::GetInstance() {
    static InputManager instance;
    return &instance;
}

void InputManager::OpenFirstAvailableSdlController() {
    // 接続済みコントローラーを先頭から探し、メニュー入力に使うデバイスを開く。
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        sdlController_ = SDL_GameControllerOpen(i);
        if (!sdlController_) continue;
        SDL_GameControllerSetSensorEnabled(sdlController_, SDL_SENSOR_ACCEL, SDL_TRUE);
        SDL_GameControllerSetSensorEnabled(sdlController_, SDL_SENSOR_GYRO, SDL_TRUE);
        baseAccel_ = {};
        isCalibrated_ = false;
        return;
    }
}

void InputManager::UpdateMenuStickNavigation() {
    // スティックを方向入力へ変換し、押し始めと長押しで異なる間隔のリピートを発生させる。
    menuStickTriggered_.fill(false);
    const engine::Vector2 stick = GetLeftStick();
    int direction = 0;
    if (std::max(std::abs(stick.x), std::abs(stick.y)) >= 0.45f) {
        direction = std::abs(stick.y) >= std::abs(stick.x) ? (stick.y > 0 ? 1 : 2) : (stick.x < 0 ? 3 : 4);
    }
    const uint64_t now = GetTickCount64();
    if (direction != menuStickDirection_) {
        menuStickDirection_ = direction;
        menuStickNextRepeatMs_ = now + 320;
        if (direction) menuStickTriggered_[direction - 1] = true;
    } else if (direction && now >= menuStickNextRepeatMs_) {
        menuStickTriggered_[direction - 1] = true;
        menuStickNextRepeatMs_ = now + 130;
    }
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
    xinputConnected_ = XInputGetState(0, &gamepadState) == ERROR_SUCCESS;

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

    OpenFirstAvailableSdlController();

}


// キー、マウス、XInput、SDLの状態を前フレームと比較できる形へ揃える。
void InputManager::Update()
{
    // トリガー判定用の旧状態を保存してから、DirectInputの現在状態を取得する。
    memcpy(prevKeyState, keyState, sizeof(keyState));
    prevMouseState = mouseState;

    // 一時的にフォーカスを失ったデバイスは再取得してから読み直す。
    HRESULT result = keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    if (FAILED(result)) {
        keyboardDevice->Unacquire();
        while ((result = keyboardDevice->Acquire()) == DIERR_INPUTLOST) {}
        keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    }

    result = mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    if (FAILED(result)) {
        mouseDevice->Unacquire();
        while ((result = mouseDevice->Acquire()) == DIERR_INPUTLOST) {}
        mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    }

    // パッドはXInputを先に試し、未接続時だけSDLのボタン・軸を共通状態へ変換する。
    prevGamepadState = gamepadState;

    ZeroMemory(&gamepadState, sizeof(XINPUT_STATE));
    xinputConnected_ = XInputGetState(0, &gamepadState) == ERROR_SUCCESS;

    // SDL側の接続解除通知と実際の接続状態の両方を確認する。
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEREMOVED && sdlController_ &&
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(sdlController_)) == event.cdevice.which) {
            SDL_GameControllerClose(sdlController_);
            sdlController_ = nullptr;
            accelData_ = {};
            gyroData_ = {};
            isCalibrated_ = false;
        }
    }

    // 起動後に接続したパッドも、次のフレームから利用できるようにします。
    if (sdlController_ && !SDL_GameControllerGetAttached(sdlController_)) {
        SDL_GameControllerClose(sdlController_);
        sdlController_ = nullptr;
        accelData_ = {};
        gyroData_ = {};
        isCalibrated_ = false;
    }
    if (!sdlController_) {
        OpenFirstAvailableSdlController();
    }

    SDL_GameControllerUpdate();

    if (sdlController_) {
        // センサーだけはSDLから常に取得し、操作ボタンはXInput接続時に二重計上しない。
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

        if (!xinputConnected_) {
        // SDLの軸とボタンをXInput形式へ写し、以後の操作判定を接続元から独立させる。
        // SDLの軸入力(-32768 ～ 32767)を取得
        int16_t leftX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t leftY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_LEFTY);
        int16_t rightX = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTX);
        int16_t rightY = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_RIGHTY);

        // SDL の Y 軸は下方向が正なので反転します。最小値は符号反転後に飽和させます。
        if (abs(leftX) > 0 || abs(leftY) > 0) {
            gamepadState.Gamepad.sThumbLX = leftX;
            gamepadState.Gamepad.sThumbLY = static_cast<SHORT>(std::clamp(-static_cast<int>(leftY), -32768, 32767));
        }
        if (abs(rightX) > 0 || abs(rightY) > 0) {
            gamepadState.Gamepad.sThumbRX = rightX;
            gamepadState.Gamepad.sThumbRY = static_cast<SHORT>(std::clamp(-static_cast<int>(rightY), -32768, 32767));
        }

        // トリガーはSDLの16bit範囲をXInputの8bit範囲へ縮小する。
        int16_t trigLeft = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int16_t trigRight = SDL_GameControllerGetAxis(sdlController_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (trigLeft > 0) gamepadState.Gamepad.bLeftTrigger = (BYTE)(trigLeft >> 7);
        if (trigRight > 0) gamepadState.Gamepad.bRightTrigger = (BYTE)(trigRight >> 7);


        // ABXYはSDLとXInputでラベル配置が異なるため、ゲーム内の期待する配置へ対応させる。
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
    }


    // センサーの基準値だけは専用キーで再調整する。
    // View/Backは操作ガイド用に使うため、センサー再調整はCキーだけで行います。
    if (IsKeyTriggered(DIK_C)) {
        Calibrate();
        DebugConsole::GetInstance()->AddLog("InputManager: Calibrated accelerometer.");
    }


    // 最後に実際の操作があった入力系を判定し、画面の操作案内を切り替える。
    bool isKeyMouseActive = false;

    // キーボード入力チェック
    for (int i = 0; i < 256; i++) {
        if (keyState[i] & 0x80) {
            isKeyMouseActive = true;
            break;
        }
    }

    // マウスセンサーの微小な揺れでは案内表示を切り替えない。
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

    // パッド側はボタン・トリガー・大きなスティック入力を操作として扱う。
    bool isGamepadActive = false;

    // ボタン入力チェック
    if (gamepadState.Gamepad.wButtons != 0) {
        isGamepadActive = true;
    }

    // モード切替は操作用デッドゾーンより強い入力で行い、静止時の軸の揺れを除きます。
    const short DETECTION_DEADZONE = 10000;

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

    // 同時入力時はキーボード・マウスを優先し、案内表示がフレーム毎に揺れるのを防ぎます。
    if (isKeyMouseActive) {
        isGamepadMode_ = false;
    } else if (isGamepadActive) {
        isGamepadMode_ = true;
    } else if (!xinputConnected_ && !sdlController_) {
        // 抜線後はゲームパッドの案内を残さず、キーボード表示へ戻します。
        isGamepadMode_ = false;
    }
    UpdateMenuStickNavigation();
}

InputManager::ReplayState InputManager::CaptureReplayState() const {
    ReplayState result;
    // リプレイのトリガー判定を再現するため、現在値と前回値を対で保存する。
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

    // SDLもXInput形式へ統合済みなので、記録するパッド状態は一組でよい。
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
    // 記録済みの前回値を復元し、シーク直後の押下・離上判定が変わらないようにする。
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

    // パッド状態も同じ形式へ戻してから、案内モードとセンサー基準を揃える。
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
engine::Vector2 InputManager::GetMouseMoveDelta() const {
    // DIMOUSESTATE構造体から相対移動量を取得する
    return { (float)mouseState.lX, (float)mouseState.lY };
}

engine::Vector2 InputManager::GetCameraLookDelta(float deltaTime) const {
    const engine::Vector2 mouse = GetMouseMoveDelta();
    const engine::Vector2 stick = GetRightStick();
    // カメラ設定の感度と反転を Camera::AddRotation 側で共通適用します。
    const float step = 360.0f * std::clamp(deltaTime, 0.0f, 0.05f);
    return {mouse.x + stick.x * step, mouse.y - stick.y * step};
}

bool InputManager::IsMenuStickTriggered(WORD dpadDirection) const {
    // 左スティックの反復判定結果を十字キー方向と同じ入口から返す。
    switch (dpadDirection) {
    case XINPUT_GAMEPAD_DPAD_UP: return menuStickTriggered_[0];
    case XINPUT_GAMEPAD_DPAD_DOWN: return menuStickTriggered_[1];
    case XINPUT_GAMEPAD_DPAD_LEFT: return menuStickTriggered_[2];
    case XINPUT_GAMEPAD_DPAD_RIGHT: return menuStickTriggered_[3];
    default: return false;
    }
}

// ゲームパッドの左スティックの入力を取得
engine::Vector2 InputManager::GetGamepadLeftStick() const {
    return NormalizeStick(gamepadState.Gamepad.sThumbLX, gamepadState.Gamepad.sThumbLY,
                          XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
}

// ゲームパッドの右スティックの入力を取得
engine::Vector2 InputManager::GetGamepadRightStick() const {
    return NormalizeStick(gamepadState.Gamepad.sThumbRX, gamepadState.Gamepad.sThumbRY,
                          XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
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
engine::Vector2 InputManager::GetMousePosition() const {

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
    if (button < 0 || button >= static_cast<int>(sizeof(mouseState.rgbButtons))) {
        return false;
    }
    // (現在 離されている) かつ (前フレームでは 押されていた) 場合にtrue
    // (※ IsMouseButtonTriggered とロジックが逆)
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
    // 一時停止中も振動の残り時間を進め、終了時に出力を止める。
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
    // 同時に要求された振動は各モーターの最大強度を採り、最長の残り時間だけSDLへ送る。
    float lowFrequency = 0.0f;
    float highFrequency = 0.0f;
    float longestRemaining = 0.0f;
    for (const RumbleRequest& request : rumbleRequests_) {
        lowFrequency = std::max(lowFrequency, request.lowFrequency);
        highFrequency = std::max(highFrequency, request.highFrequency);
        longestRemaining = std::max(longestRemaining, request.remaining);
    }

    // 強度が前回と同じならデバイスへ再送しない。
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

engine::Vector2 InputManager::GetRightStick() const {
    return GetGamepadRightStick();
}

bool InputManager::IsGamepadButtonTriggered(WORD button) const {
    // 「今は押されている」かつ「前は押されていなかった」なら true
    return (gamepadState.Gamepad.wButtons & button) && !(prevGamepadState.Gamepad.wButtons & button);
}

engine::Vector2 InputManager::GetLeftStick() const {
    return GetGamepadLeftStick();
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

    // ① キーボード判定
    if (data->keyCode != 0 && IsKeyPressed(static_cast<BYTE>(data->keyCode))) return true;

    // ② マウス判定 
    if (data->mouseButton != -1 && IsMouseButtonPressed(data->mouseButton)) return true;

    // ③ パッド判定
    if (data->padCode != 0 && IsGamepadButtonPressed(data->padCode)) return true;

    return false;
}

// アクションが「押された瞬間か」の判定
bool InputManager::IsActionTriggered(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // ① キーボード判定
    if (data->keyCode != 0 && IsKeyTriggered(static_cast<BYTE>(data->keyCode))) return true;

    // ② マウス判定 
    if (data->mouseButton != -1 && IsMouseButtonTriggered(data->mouseButton)) return true;

    // ③ パッド判定
    if (data->padCode != 0 && IsGamepadButtonTriggered(data->padCode)) return true;

    return false;
}

bool InputManager::IsActionReleased(const std::string& actionName) const {
    const BindData* data = KeyConfig::GetInstance()->GetBindData(actionName);
    if (!data) return false;

    // ① キーボード
    if (data->keyCode != 0 && !(keyState[data->keyCode] & 0x80) && (prevKeyState[data->keyCode] & 0x80)) return true;

    // ② マウス (離した判定)
    if (data->mouseButton != -1 && IsMouseButtonReleased(data->mouseButton)) return true;

    // ③ パッド (離した判定：wButtonsのビットが落ちた瞬間)
    if (data->padCode != 0 && !(gamepadState.Gamepad.wButtons & data->padCode) && (prevGamepadState.Gamepad.wButtons & data->padCode)) return true;

    return false;
}
