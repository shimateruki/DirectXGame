#pragma once
#include <dinput.h>
#include <windows.h>
#include "math.h"


class InputManager {
public:
    void Initialize(HINSTANCE hInstance, HWND hwnd);
    void Update();

    // キーボード
    bool IsKeyPressed(BYTE keyCode) const;
    bool IsKeyTriggered(BYTE keyCode) const;

    // マウス
    bool IsMouseButtonPressed(int button) const; // 0: 左, 1: 右, 2: 中
    bool IsMouseButtonTriggered(int button) const;
    Vector2 GetMouseMoveDelta() const;

private:
    IDirectInput8* directInput = nullptr;
    IDirectInputDevice8* keyboardDevice = nullptr;
    IDirectInputDevice8* mouseDevice = nullptr;

    BYTE keyState[256]{};
    BYTE prevKeyState[256]{};

    DIMOUSESTATE mouseState{};
    DIMOUSESTATE prevMouseState{};
};