#include "InputManager.h"
#include <cassert>
#include "math.h"

void InputManager::Initialize(HWND hwnd)
{
    HRESULT result;

    // DirectInput 初期化
    result = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
    assert(SUCCEEDED(result));

    // キーボード
    result = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
    assert(SUCCEEDED(result));
    result = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));
    result = keyboardDevice->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
    keyboardDevice->Acquire();

    // マウス
    result = directInput->CreateDevice(GUID_SysMouse, &mouseDevice, nullptr);
    assert(SUCCEEDED(result));
    result = mouseDevice->SetDataFormat(&c_dfDIMouse);
    assert(SUCCEEDED(result));
    result = mouseDevice->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
    mouseDevice->Acquire();
}

void InputManager::Update()
{
    memcpy(prevKeyState, keyState, sizeof(keyState));
    prevMouseState = mouseState;

    // キーボード
    HRESULT result = keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    if (FAILED(result)) {
        keyboardDevice->Unacquire();
        while ((result = keyboardDevice->Acquire()) == DIERR_INPUTLOST) {}
        keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    }

    // マウス
    result = mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    if (FAILED(result)) {
        mouseDevice->Unacquire();
        while ((result = mouseDevice->Acquire()) == DIERR_INPUTLOST) {}
        mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    }

    // ゲームパッド
    ZeroMemory(&gamepadState, sizeof(XINPUT_STATE));
    DWORD padResult = XInputGetState(0, &gamepadState);
    if (padResult != ERROR_SUCCESS) {
        ZeroMemory(&gamepadState, sizeof(XINPUT_STATE)); // 未接続
    }
}

bool InputManager::IsKeyPressed(BYTE keyCode) const {
    return (keyState[keyCode] & 0x80) != 0;
}

bool InputManager::IsKeyTriggered(BYTE keyCode) const {
    return (keyState[keyCode] & 0x80) && !(prevKeyState[keyCode] & 0x80);
}

bool InputManager::IsMouseButtonPressed(int button) const {
    return (mouseState.rgbButtons[button] & 0x80) != 0;
}

bool InputManager::IsMouseButtonTriggered(int button) const {
    return (mouseState.rgbButtons[button] & 0x80) && !(prevMouseState.rgbButtons[button] & 0x80);
}

Vector2 InputManager::GetMouseMoveDelta() const {
    return { (float)mouseState.lX, (float)mouseState.lY };
}

Vector2 InputManager::GetGamepadLeftStick() const {
    const SHORT deadZone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    SHORT lx = gamepadState.Gamepad.sThumbLX;
    SHORT ly = gamepadState.Gamepad.sThumbLY;

    float x = (abs(lx) > deadZone) ? lx / 32768.0f : 0.0f;
    float y = (abs(ly) > deadZone) ? ly / 32768.0f : 0.0f;
    return { x, y };
}

Vector2 InputManager::GetGamepadRightStick() const {
    const SHORT deadZone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
    SHORT rx = gamepadState.Gamepad.sThumbRX;
    SHORT ry = gamepadState.Gamepad.sThumbRY;

    float x = (abs(rx) > deadZone) ? rx / 32768.0f : 0.0f;
    float y = (abs(ry) > deadZone) ? ry / 32768.0f : 0.0f;
    return { x, y };
}

bool InputManager::IsGamepadButtonPressed(WORD button) const {
    return (gamepadState.Gamepad.wButtons & button) != 0;
}