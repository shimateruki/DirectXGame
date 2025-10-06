#pragma once
#include <dinput.h>
#include <windows.h>
#include "engine/base/Math.h"
#include <Xinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "dxguid.lib")


class InputManager {
public:
    void Initialize(HWND hwnd);
    void Update();

    bool IsKeyPressed(BYTE keyCode) const;
    bool IsKeyTriggered(BYTE keyCode) const;

    bool IsMouseButtonPressed(int button) const;
    bool IsMouseButtonTriggered(int button) const;
    Vector2 GetMouseMoveDelta() const;

    Vector2 GetGamepadLeftStick() const;
    Vector2 GetGamepadRightStick() const;
    bool IsGamepadButtonPressed(WORD button) const;

private:
    IDirectInput8* directInput = nullptr;
    IDirectInputDevice8* keyboardDevice = nullptr;
    IDirectInputDevice8* mouseDevice = nullptr;

    BYTE keyState[256]{};
    BYTE prevKeyState[256]{};
    DIMOUSESTATE mouseState{};
    DIMOUSESTATE prevMouseState{};

    XINPUT_STATE gamepadState{};
};