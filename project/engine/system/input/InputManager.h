#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <windows.h>
#include <Xinput.h>
#include <cstdint>
#include <dinput.h>
#include <string>
#include <vector>

#include "KeyConfig.h"
#include "engine/utility/math/Math.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "dxguid.lib")

/// <summary>
/// キーボード、マウス、ゲームパッド、SDLセンサー入力をまとめて管理する。
/// </summary>
class InputManager {
public:
    /// <summary>
    /// シングルトンインスタンスを取得する。
    /// </summary>
    static InputManager* GetInstance();

    /// <summary>
    /// 入力デバイスを初期化する。
    /// </summary>
    void Initialize(HWND hwnd);

    /// <summary>
    /// 毎フレームの入力状態を更新する。
    /// </summary>
    void Update();

    // キーボード入力。
    bool IsKeyPressed(BYTE keyCode) const;
    bool IsKeyTriggered(BYTE keyCode) const;
    std::vector<uint8_t> GetPressedKeys() const;

    // マウス入力。
    bool IsMouseButtonPressed(int button) const;
    bool IsMouseButtonTriggered(int button) const;
    bool IsMouseButtonReleased(int button) const;
    Vector2 GetMouseMoveDelta() const;
    Vector2 GetMousePosition() const;
    float GetMouseWheelDelta() const;
    int GetPressedMouseButton() const;

    // ゲームパッド入力。
    Vector2 GetGamepadLeftStick() const;
    Vector2 GetGamepadRightStick() const;
    bool IsGamepadButtonPressed(WORD button) const;
    bool IsGamepadButtonTriggered(WORD button) const;
    WORD GetPressedGamepadButton() const;
    bool IsGamepadMode() const { return isGamepadMode_; }
    Vector2 GetLeftStick() const;
    Vector2 GetRightStick() const;

    // SDLセンサー入力。
    void Finalize();
    Vector3 GetAccelerometer() const { return accelData_; }
    Vector3 GetGyroscope() const { return gyroData_; }
    void Calibrate() { baseAccel_ = accelData_; }
    Vector3 GetBaseAccelerometer() const { return baseAccel_; }

    // アクション名ベースの入力判定。
    bool IsActionPressed(const std::string& actionName) const;
    bool IsActionTriggered(const std::string& actionName) const;
    bool IsActionReleased(const std::string& actionName) const;

private:
    InputManager() = default;
    ~InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // DirectInput関連。
    IDirectInput8* directInput = nullptr;
    IDirectInputDevice8* keyboardDevice = nullptr;
    IDirectInputDevice8* mouseDevice = nullptr;

    // キーボード状態。
    BYTE keyState[256]{};
    BYTE prevKeyState[256]{};

    // マウス状態。
    DIMOUSESTATE mouseState{};
    DIMOUSESTATE prevMouseState{};

    // ゲームパッド状態。
    XINPUT_STATE gamepadState{};
    XINPUT_STATE prevGamepadState{};
    HWND hwnd_ = nullptr;

    // SDL2センサー状態。
    SDL_GameController* sdlController_ = nullptr;
    Vector3 accelData_ = { 0, 0, 0 };
    Vector3 gyroData_ = { 0, 0, 0 };
    Vector3 baseAccel_ = { 0.0f, 0.0f, 0.0f };
    bool isCalibrated_ = false;

    bool isGamepadMode_ = false;
};
