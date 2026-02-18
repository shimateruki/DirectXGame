#pragma once 
#include <dinput.h>   
#include <windows.h>  
#include "engine/utility/math/Math.h"
#include <Xinput.h>

#define SDL_MAIN_HANDLED // SDLにメイン関数を任せない宣言
#include <SDL.h>

#pragma comment(lib, "dinput8.lib") 
#pragma comment(lib, "xinput.lib")  
#pragma comment(lib, "dxguid.lib")  


/// <summary>
/// キーボード、マウス、ゲームパッドからの入力を管理するクラス
/// </summary>
class InputManager {
public:
    // シングルトンインスタンスを取得する関数
    static InputManager* GetInstance();
    /// <summary>
    /// 入力マネージャーを初期化する
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル</param>
    void Initialize(HWND hwnd);

    /// <summary>
    /// 毎フレームの入力状態を更新する
    /// </summary>
    void Update();

    /// <summary>
    /// 指定されたキーが押されているかを取得する
    /// </summary>
    /// <param name="keyCode">キーコード (例: DIK_SPACE)</param>
    /// <returns>押されていればtrue</returns>
    bool IsKeyPressed(BYTE keyCode) const;

    /// <summary>
    /// 指定されたキーがこのフレームで押された瞬間かを取得する (トリガー)
    /// </summary>
    /// <param name="keyCode">キーコード</param>
    /// <returns>押された瞬間ならtrue</returns>
    bool IsKeyTriggered(BYTE keyCode) const;

    /// <summary>
    /// 指定されたマウスボタンが押されているかを取得する
    /// </summary>
    /// <param name="button">ボタン番号 (0:左, 1:右, 2:中)</param>
    /// <returns>押されていればtrue</returns>
    bool IsMouseButtonPressed(int button) const;

    /// <summary>
    /// 指定されたマウスボタンがこのフレームで押された瞬間かを取得する (トリガー)
    /// </summary>
    /// <param name="button">ボタン番号</param>
    /// <returns>押された瞬間ならtrue</returns>
    bool IsMouseButtonTriggered(int button) const;

    /// <summary>
    /// 前フレームからのマウスの移動量を取得する
    /// </summary>
    /// <returns>マウスのX,Y移動量</returns>
    Vector2 GetMouseMoveDelta() const;

    /// <summary>
    /// ゲームパッドの左スティックの入力値を取得する (-1.0f ~ 1.0f の範囲に正規化)
    /// </summary>
    /// <returns>左スティックのXY軸の入力値</returns>
    Vector2 GetGamepadLeftStick() const;

    /// <summary>
    /// ゲームパッドの右スティックの入力値を取得する (-1.0f ~ 1.0f の範囲に正規化)
    /// </summary>
    /// <returns>右スティックのXY軸の入力値</returns>
    Vector2 GetGamepadRightStick() const;

    /// <summary>
    /// 指定されたゲームパッドのボタンが押されているかを取得する
    /// </summary>
    /// <param name="button">ボタンの種類 (例: XINPUT_GAMEPAD_A)</param>
    /// <returns>押されていればtrue</returns>
    bool IsGamepadButtonPressed(WORD button) const;
    float GetMouseWheelDelta() const;
    /// <summary>
    /// マウスカーソルの「絶対座標」（ウィンドウ内）を取得する
    /// </summary>
    Vector2 GetMousePosition() const;

    /// <summary>
    /// 指定されたマウスボタンがこのフレームで離された瞬間か (リリース)
    /// </summary>
    bool IsMouseButtonReleased(int button) const;
    Vector2 GetRightStick() const;

    // SDLの終了処理用
    void Finalize();

    // 加速度（傾き）とジャイロを取得する関数
    Vector3 GetAccelerometer() const { return accelData_; }
    Vector3 GetGyroscope() const { return gyroData_; }
    // 現在の加速度を「0点」として記録する
    void Calibrate() { baseAccel_ = accelData_; }

    // 基準値を取得するゲッター
    Vector3 GetBaseAccelerometer() const { return baseAccel_; }

    bool IsGamepadButtonTriggered(WORD button) const;
    bool IsGamepadMode() const { return isGamepadMode_; }
    Vector2 GetLeftStick() const;
private:
    InputManager() = default;
    ~InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    // --- DirectInput関連 ---
    IDirectInput8* directInput = nullptr; // DirectInputのメインインターフェース
    IDirectInputDevice8* keyboardDevice = nullptr; // キーボード用のデバイス
    IDirectInputDevice8* mouseDevice = nullptr;    // マウス用のデバイス

    // --- キーボードの状態 ---
    BYTE keyState[256]{};     // 現在のフレームのキーボード状態
    BYTE prevKeyState[256]{}; // 前のフレームのキーボード状態 (トリガー判定用)

    // --- マウスの状態 ---
    DIMOUSESTATE mouseState{};     // 現在のフレームのマウス状態
    DIMOUSESTATE prevMouseState{}; // 前のフレームのマウス状態 (トリガー判定用)

    // --- ゲームパッドの状態 ---
    XINPUT_STATE gamepadState{}; // 現在のフレームのゲームパッド状態
    HWND hwnd_ = nullptr;

    // --- SDL2関連 ---
    SDL_GameController* sdlController_ = nullptr;
    Vector3 accelData_ = { 0, 0, 0 }; // 加速度 (重力の向き)
    Vector3 gyroData_ = { 0, 0, 0 };  // ジャイロ (回転速度)
    Vector3 baseAccel_ = { 0.0f, 0.0f, 0.0f }; // 基準となる重力ベクトル
    bool isCalibrated_;
    XINPUT_STATE prevGamepadState{};

    bool isGamepadMode_ = false;
};