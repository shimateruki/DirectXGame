#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <windows.h>
#include <Xinput.h>
#include <array>
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

/// <summary>物理入力、アクション入力、振動、SDL センサーをフレーム単位で管理します。</summary>
class InputManager {
public:
    /// <summary>回帰再生用に、現在と前フレームの物理入力をまとめた状態です。</summary>
    struct ReplayState {
        std::array<uint8_t, 256> keys{};
        std::array<uint8_t, 256> previousKeys{};
        std::array<uint8_t, 4> mouseButtons{};
        std::array<uint8_t, 4> previousMouseButtons{};
        int32_t mouseX = 0;
        int32_t mouseY = 0;
        int32_t mouseWheel = 0;
        int32_t previousMouseX = 0;
        int32_t previousMouseY = 0;
        int32_t previousMouseWheel = 0;
        uint16_t gamepadButtons = 0;
        uint16_t previousGamepadButtons = 0;
        uint8_t leftTrigger = 0;
        uint8_t rightTrigger = 0;
        uint8_t previousLeftTrigger = 0;
        uint8_t previousRightTrigger = 0;
        int16_t leftX = 0;
        int16_t leftY = 0;
        int16_t rightX = 0;
        int16_t rightY = 0;
        int16_t previousLeftX = 0;
        int16_t previousLeftY = 0;
        int16_t previousRightX = 0;
        int16_t previousRightY = 0;
        bool gamepadMode = false;
        Vector3 accelerometer{};
        Vector3 gyroscope{};
        Vector3 baseAccelerometer{};
    };

    /// <summary>エンジンで共有する入力管理インスタンスを取得します。</summary>
    static InputManager* GetInstance();

    /// <summary>DirectInput と SDL ゲームパッドを初期化します。</summary>
    /// <param name="hwnd">マウスとキーボード入力を受け取るウィンドウ。</param>
    void Initialize(HWND hwnd);

    /// <summary>デバイスをポーリングし、前フレームとの差分を更新します。</summary>
    void Update();

    /// <summary>指定キーが押され続けているか返します。</summary>
    bool IsKeyPressed(BYTE keyCode) const;

    /// <summary>指定キーがこのフレームで押されたか返します。</summary>
    bool IsKeyTriggered(BYTE keyCode) const;

    /// <summary>現在押されているキーコードの一覧を返します。</summary>
    std::vector<uint8_t> GetPressedKeys() const;

    /// <summary>指定マウスボタンが押され続けているか返します。</summary>
    bool IsMouseButtonPressed(int button) const;

    /// <summary>指定マウスボタンがこのフレームで押されたか返します。</summary>
    bool IsMouseButtonTriggered(int button) const;

    /// <summary>指定マウスボタンがこのフレームで離されたか返します。</summary>
    bool IsMouseButtonReleased(int button) const;

    /// <summary>前フレームからのマウス移動量を返します。</summary>
    Vector2 GetMouseMoveDelta() const;

    /// <summary>現在のマウス位置を返します。</summary>
    Vector2 GetMousePosition() const;

    /// <summary>このフレームのホイール変化量を返します。</summary>
    float GetMouseWheelDelta() const;

    /// <summary>物理入力を再生用に保存します。</summary>
    ReplayState CaptureReplayState() const;

    /// <summary>回帰再生用の現在・前フレーム入力を復元します。</summary>
    /// <param name="state">復元する入力状態。</param>
    void ApplyReplayState(const ReplayState& state);

    /// <summary>現在押されているマウスボタン番号を返します。</summary>
    int GetPressedMouseButton() const;

    /// <summary>ゲームパッド左スティックの入力を返します。</summary>
    Vector2 GetGamepadLeftStick() const;

    /// <summary>ゲームパッド右スティックの入力を返します。</summary>
    Vector2 GetGamepadRightStick() const;

    /// <summary>指定ゲームパッドボタンが押され続けているか返します。</summary>
    bool IsGamepadButtonPressed(WORD button) const;

    /// <summary>指定ゲームパッドボタンがこのフレームで押されたか返します。</summary>
    bool IsGamepadButtonTriggered(WORD button) const;

    /// <summary>現在押されているゲームパッドボタンを返します。</summary>
    WORD GetPressedGamepadButton() const;

    /// <summary>直近の操作デバイスがゲームパッドか返します。</summary>
    bool IsGamepadMode() const { return isGamepadMode_; }

    /// <summary>XInput を優先し、入力がなければ SDL の左スティック値を返します。</summary>
    Vector2 GetLeftStick() const;

    /// <summary>XInput を優先し、入力がなければ SDL の右スティック値を返します。</summary>
    Vector2 GetRightStick() const;

    /// <summary>低・高周波の振動を指定時間だけ要求します。並行要求は強い方を採用します。</summary>
    void PlayRumble(float lowFrequency, float highFrequency, float duration);

    /// <summary>振動要求の残り時間とデバイス出力を更新します。</summary>
    void UpdateRumble(float unscaledDeltaTime);

    /// <summary>すべての振動要求を破棄して停止します。</summary>
    void StopRumble();

    /// <summary>有効な振動要求が残っているか返します。</summary>
    bool IsRumbling() const { return !rumbleRequests_.empty(); }

    /// <summary>入力デバイスと SDL センサーを解放します。</summary>
    void Finalize();

    /// <summary>現在の加速度センサー値を返します。</summary>
    Vector3 GetAccelerometer() const { return accelData_; }

    /// <summary>現在のジャイロセンサー値を返します。</summary>
    Vector3 GetGyroscope() const { return gyroData_; }

    /// <summary>現在の加速度を基準値として記録します。</summary>
    void Calibrate() { baseAccel_ = accelData_; }

    /// <summary>校正時の加速度基準値を返します。</summary>
    Vector3 GetBaseAccelerometer() const { return baseAccel_; }

    /// <summary>KeyConfig のアクションが押され続けているか返します。</summary>
    bool IsActionPressed(const std::string& actionName) const;

    /// <summary>KeyConfig のアクションがこのフレームで押されたか返します。</summary>
    // 各デバイスの押下エッジを OR する。統合アクションの前回状態からエッジを作る方式ではない。
    bool IsActionTriggered(const std::string& actionName) const;

    /// <summary>KeyConfig のアクションがこのフレームで離されたか返します。</summary>
    // 別デバイスで押し続けていても、どれか一つの割当入力を離せば true になり得る。
    bool IsActionReleased(const std::string& actionName) const;

private:
    InputManager() = default;
    ~InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // DirectInput の所有ポインタと、差分判定に必要な前フレーム状態。
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

    // SDL2 センサー値と校正時の加速度基準値。
    SDL_GameController* sdlController_ = nullptr;
    Vector3 accelData_ = { 0, 0, 0 };
    Vector3 gyroData_ = { 0, 0, 0 };
    Vector3 baseAccel_ = { 0.0f, 0.0f, 0.0f };
    bool isCalibrated_ = false;

    bool isGamepadMode_ = false;

    /// <summary>強弱の振動量と残り時間を持つ一時的な振動要求。</summary>
    struct RumbleRequest {
        float lowFrequency = 0.0f;
        float highFrequency = 0.0f;
        float remaining = 0.0f;
    };

    // 複数の要求を合成し、実際に変化した場合だけデバイスへ送ります。
    void RefreshRumbleOutput();

    std::vector<RumbleRequest> rumbleRequests_;
    float appliedLowFrequency_ = -1.0f;
    float appliedHighFrequency_ = -1.0f;
};
