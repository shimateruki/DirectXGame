#include "InputManager.h"
#include <cassert>
#include "math.h"

void InputManager::Initialize(HWND hwnd)
{
    HRESULT result;

    // DirectInputインターフェース生成
    result = DirectInput8Create(
        GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void**>(&directInput), nullptr);
    assert(SUCCEEDED(result));

    // キーボードデバイス生成
    result = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
    assert(SUCCEEDED(result));
    result = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));
    result = keyboardDevice->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
    keyboardDevice->Acquire();

    // マウスデバイス生成
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
    // 前の状態を保存
    memcpy(prevKeyState, keyState, sizeof(keyState));
    prevMouseState = mouseState;

    // --- キーボードの状態取得 ---
    HRESULT result = keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    if (FAILED(result)) {
        keyboardDevice->Unacquire();
        result = keyboardDevice->Acquire();
        while (result == DIERR_INPUTLOST) {
            result = keyboardDevice->Acquire();
        }
        keyboardDevice->GetDeviceState(sizeof(keyState), keyState);
    }

    // --- マウスの状態取得 ---
    result = mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    if (FAILED(result)) {
        mouseDevice->Unacquire();
        result = mouseDevice->Acquire();
        while (result == DIERR_INPUTLOST) {
            result = mouseDevice->Acquire();
        }
        mouseDevice->GetDeviceState(sizeof(mouseState), &mouseState);
    }
}

// ---------------- キーボード ----------------

bool InputManager::IsKeyPressed(BYTE keyCode) const
{
    return (keyState[keyCode] & 0x80) != 0;
}

bool InputManager::IsKeyTriggered(BYTE keyCode) const
{
    return (keyState[keyCode] & 0x80) && !(prevKeyState[keyCode] & 0x80);
}

// ---------------- マウス ----------------

bool InputManager::IsMouseButtonPressed(int button) const
{
    return (mouseState.rgbButtons[button] & 0x80) != 0;
}

bool InputManager::IsMouseButtonTriggered(int button) const
{
    return (mouseState.rgbButtons[button] & 0x80) && !(prevMouseState.rgbButtons[button] & 0x80);
}

Vector2 InputManager::GetMouseMoveDelta() const
{
    return { (float)mouseState.lX, (float)mouseState.lY };
}