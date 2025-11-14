
#pragma once
#include "engine/utility/math/Math.h"
#include "InputManager.h"

/// <summary>
/// 3Dシーンの視点を管理するカメラクラス
/// </summary>
class Camera {
public:
    //  リリースビルド（追従時）のカメラモード
    enum class FollowMode {
        kFixed,         // 従来の固定オフセット追従
        kAimable,       // プレイヤーの後ろからマウスで視点操作・ズーム可能
        kFirstPerson,   // 一人称視点
    };


public:
    void Initialize();
    void Update();

    // --- セッター (変更なし) ---
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }

    // --- ゲッター (変更なし) ---
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

    /// <summary>
    /// カメラの入力（マウス操作）を有効/無効にする
    /// </summary>
    void SetInputEnabled(bool enabled) { isInputEnabled_ = enabled; }

    /// <summary>
    /// 追従対象の座標を設定する (デバッグビルド中は無効化)
    /// </summary>
    void SetTarget(const Vector3* target);
    const Vector3* GetTarget() const { return targetPosition_; }

    /// <summary>
    /// [Release用] 追従モードを切り替える (例: kFixed, kAimable)
    /// </summary>
    void SetFollowMode(FollowMode mode);

    /// <summary>
    /// [Release用] kFixed モードのオフセットを設定する
    /// </summary>
    void ConfigFixed(const Vector3& offset);

    /// <summary>
    /// [Release用] kAimable モードの距離や範囲を設定する
    /// </summary>
    void ConfigAimable(float distance, float minDistance, float maxDistance);

    /// <summary>
    /// [Release用] kFirstPerson モードの視点オフセットを設定する
    /// </summary>
    void ConfigFirstPerson(const Vector3& eyeOffset);

    /// <summary>
    /// [Release用] kAimable, kFirstPerson モードで視点回転を加える
    /// </summary>
    void AddRotation(const Vector2& mouseDelta);

    /// <summary>
    /// [Release用] kAimable モードでズーム距離を加える
    /// </summary>
    void AddZoom(float wheelDelta);
    /// <summary>
    /// [Release用] 現在の追従モードを取得する
    /// </summary>
    FollowMode GetFollowMode() const { return followMode_; }





private:
    // --- カメラの三要素 ---
    Vector3 eye_ = { 0.0f, 0.0f, -10.0f };
    Vector3 target_ = { 0.0f, 0.0f, 0.0f };
    Vector3 up_ = { 0.0f, 1.0f, 0.0f };

    // --- プロジェクション行列のパラメータ---
    float fovY_ = 0.45f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearClip_ = 0.1f;
    float farClip_ = 1000.0f;

    // --- 行列 ---
    Matrix4x4 viewMatrix_ = {};
    Matrix4x4 projectionMatrix_ = {};

    // --- ポインタ ---
    InputManager* inputManager_ = nullptr;



    // 追従対象の座標
    // (デバッグ中は nullptr, リリース中は Player の座標)
    const Vector3* targetPosition_ = nullptr;

    // ★ デバッグカメラ / Releaseカメラ(Aimable/FPS) の両方で使う回転
    Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };

    // --- Release用モードとパラメータ ---
    FollowMode followMode_ = FollowMode::kFixed; // Release時のデフォルトモード

    // kFixed 用
    Vector3 fixedOffset_ = { 0.0f, 5.0f, -20.0f }; // (Initializeの値で上書き)

    // kAimable 用
    float distance_ = 10.0f;      // ターゲットからの距離
    float minDistance_ = 2.0f;     // 最小ズーム距離
    float maxDistance_ = 20.0f;    // 最大ズーム距離

    // kFirstPerson 用
    Vector3 firstPersonOffset_ = { 0.0f, 0.5f, 0.0f }; // ターゲットの座標からの視点のズレ
    bool isInputEnabled_ = true;
};