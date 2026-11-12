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
namespace engine {

// 担当：キーボード・マウス・パッド入力、操作判定と振動を管理する。
// 関連：WinApp（入力元）、KeyConfig（割当）、MenuInput（メニュー用の読出し）
// 主な入口：Initialize / Update / IsActionTriggered / PlayRumble / Finalize
class InputManager {
public:
    /// <summary>回帰再生用に、現在と前フレームの物理入力をまとめた状態です。</summary>
    // 関連：InputManager（この型を使う機能の本体）
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

        engine::Vector3 accelerometer{};
        engine::Vector3 gyroscope{};
        engine::Vector3 baseAccelerometer{};
    };

    /// <summary>エンジンで共有する入力管理インスタンスを取得します。</summary>
    static engine::InputManager* GetInstance();

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
    engine::Vector2 GetMouseMoveDelta() const;

    /// <summary>現在のマウス位置を返します。</summary>
    engine::Vector2 GetMousePosition() const;

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
    engine::Vector2 GetGamepadLeftStick() const;

    /// <summary>ゲームパッド右スティックの入力を返します。</summary>
    engine::Vector2 GetGamepadRightStick() const;

    /// <summary>指定ゲームパッドボタンが押され続けているか返します。</summary>
    bool IsGamepadButtonPressed(WORD button) const;

    /// <summary>指定ゲームパッドボタンがこのフレームで押されたか返します。</summary>
    bool IsGamepadButtonTriggered(WORD button) const;

    /// <summary>現在押されているゲームパッドボタンを返します。</summary>
    WORD GetPressedGamepadButton() const;

    /// <summary>直近の操作デバイスがゲームパッドか返します。</summary>
    bool IsGamepadMode() const { return isGamepadMode_; }

    /// <summary>XInput、未接続時は SDL から統合した左スティック値を返します。</summary>
    engine::Vector2 GetLeftStick() const;

    /// <summary>XInput、未接続時は SDL から統合した右スティック値を返します。</summary>
    engine::Vector2 GetRightStick() const;

    /// <summary>マウス移動と右スティックを、Camera::AddRotation 用の相対移動量にまとめます。</summary>
    /// <param name="deltaTime">スティックの角速度をフレーム時間へ換算する秒数。</param>
    engine::Vector2 GetCameraLookDelta(float deltaTime) const;

    /// <summary>メニュー用の左スティック方向入力です。初回と押し続けた後の反復で true を返します。</summary>
    bool IsMenuStickTriggered(WORD dpadDirection) const;

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
    engine::Vector3 GetAccelerometer() const { return accelData_; }

    /// <summary>現在のジャイロセンサー値を返します。</summary>
    engine::Vector3 GetGyroscope() const { return gyroData_; }

    /// <summary>現在の加速度を基準値として記録します。</summary>
    void Calibrate() { baseAccel_ = accelData_; }

    /// <summary>校正時の加速度基準値を返します。</summary>
    engine::Vector3 GetBaseAccelerometer() const { return baseAccel_; }

    /// <summary>KeyConfig のアクションが押され続けているか返します。</summary>
    bool IsActionPressed(const std::string& actionName) const;

    /// <summary>KeyConfig のアクションがこのフレームで押されたか返します。</summary>
    // 各デバイスの押下エッジを OR する。統合アクションの前回状態からエッジを作る方式ではない。
    bool IsActionTriggered(const std::string& actionName) const;

    /// <summary>KeyConfig のアクションがこのフレームで離されたか返します。</summary>
    // 別デバイスで押し続けていても、どれか一つの割当入力を離せば true になり得る。
    bool IsActionReleased(const std::string& actionName) const;

private:
    /// <summary>共有インスタンス以外から入力管理器を生成させない。</summary>
    InputManager() = default;

    /// <summary>共有インスタンスの破棄をクラス内に限定する。</summary>
    ~InputManager() = default;

    InputManager(const InputManager&) = delete;

    InputManager& operator=(const InputManager&) = delete;

    // DirectInput本体と入力デバイス。Initializeで取得し、Finalizeで解放する。
    IDirectInput8* directInput_ = nullptr;
    IDirectInputDevice8* keyboardDevice_ = nullptr;
    IDirectInputDevice8* mouseDevice_ = nullptr;

    // 256キーの現在値と前フレーム値。Triggered判定では両者を比較する。
    BYTE keyState_[256]{};
    BYTE prevKeyState_[256]{};

    // マウスボタンと移動量の現在値・前フレーム値。
    DIMOUSESTATE mouseState_{};
    DIMOUSESTATE prevMouseState_{};

    // XInputの現在値と前フレーム値。ボタンの押下エッジに使用する。
    XINPUT_STATE gamepadState_{};
    XINPUT_STATE prevGamepadState_{};

    // DirectInputの協調レベルを設定する対象ウィンドウ。所有しない。
    HWND hwnd_ = nullptr;

    // SDLから開いたゲームコントローラー。Finalizeで閉じる。
    SDL_GameController* sdlController_ = nullptr;

    // XInput接続の検出結果。SDLコントローラーの有無とは別に保持する。
    bool xinputConnected_ = false;

    // センサーの直近値。baseAccel_を差し引く前の値を保持する。
    engine::Vector3 accelData_ = engine::Vector3::ZERO;
    engine::Vector3 gyroData_ = engine::Vector3::ZERO;

    // 校正時の加速度と、校正済みかどうか。
    engine::Vector3 baseAccel_ = engine::Vector3::ZERO;
    bool isCalibrated_ = false;

    // 直近に操作したデバイス種別。操作案内の表示切替に使う。
    bool isGamepadMode_ = false;

    // メニュー用スティックの各方向について、現在の押下エッジを保持する。
    std::array<bool, 4> menuStickTriggered_{};

    // 押し続けているメニュー方向。0は方向入力なし。
    int menuStickDirection_ = 0;

    // 長押し中に次の方向入力を発生させる時刻（ミリ秒）。
    uint64_t menuStickNextRepeatMs_ = 0;

    /// <summary>利用可能なSDLコントローラーを一つ開き、センサー参照を準備する。</summary>
    void OpenFirstAvailableSdlController();

    /// <summary>メニューの方向入力と押し続けたときの反復タイミングを更新する。</summary>
    void UpdateMenuStickNavigation();

    /// <summary>強弱の振動量と残り時間を持つ一時的な振動要求。</summary>
    // 関連：InputManager（この型を使う機能の本体）
    struct RumbleRequest {
        float lowFrequency = 0.0f;
        float highFrequency = 0.0f;
        float remaining = 0.0f;
    };

    /// <summary>複数の振動要求を合成し、出力値が変わった場合だけデバイスへ送る。</summary>
    void RefreshRumbleOutput();

    // 同時に要求された振動を残り時間つきで保持し、出力時に合成する。
    std::vector<RumbleRequest> rumbleRequests_;

    // デバイスへ最後に適用した振動強度。-1はまだ出力していない状態。
    float appliedLowFrequency_ = -1.0f;
    float appliedHighFrequency_ = -1.0f;
};

} // namespace engine
