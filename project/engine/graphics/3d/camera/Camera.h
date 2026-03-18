#pragma once
#include "engine/utility/math/Math.h"
#include "InputManager.h"
#include "Object3d.h" 


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
        kLockOn,        // ロックオンモード
        kOrbit,         // 周回
        kFixedPoint,

    };


public:
    void Initialize();
    void Update();

    // --- セッター ---
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }

    // --- ゲッター  ---
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

    /// <summary>
    /// カメラの入力（マウス操作）を有効/無効にする
    /// </summary>
    void SetInputEnabled(bool enabled) { isInputEnabled_ = enabled; }

    /// <summary>
    ///  追従対象のオブジェクト (Player) を設定する
    /// </summary>
    void SetFollowTarget(Object3d* target) { followObject_ = target; }

    /// <summary>
    ///  ロックオン対象のオブジェクト (Enemy) を設定する
    /// </summary>
    void SetLockOnTarget(Object3d* target) { targetObject_ = target; }

    /// <summary>
    ///  カメラモードを設定する
    /// </summary>
    void SetFollowMode(FollowMode mode);


    /// <summary>
    /// 現在のカメラモードを取得する 
    /// </summary>
    FollowMode GetFollowMode() const { return followMode_; }

    /// <summary>
    /// 追従対象のオブジェクト (Player) を取得する
    /// </summary>
    Object3d* GetFollowTarget() const { return followObject_; }



    // --- モード別設定  ---
    void ConfigFixed(const Vector3& offset);
    void ConfigAimable(float distance, float height, const Vector3& angle);
    void ConfigFirstPerson(const Vector3& eyeOffset);

    // --- 操作---
    void AddRotation(const Vector2& mouseDelta);


    // --- ゲッター ---

    /// <summary>
    /// カメラの座標（視点）を取得する
    /// </summary>
    const Vector3& GetEye() const { return eye_; }

    /// <summary>
    /// カメラの注視点（見ている先）を取得する
    /// </summary>
    const Vector3& GetTargetPoint() const { return target_; }

    /// <summary>
    /// カメラの回転角度（ラジアン）を取得する (kAimable, kFirstPerson用)
    /// </summary>
    const Vector3& GetRotation() const { return rotation_; }

    /// <summary>
    /// ロックオン時のオフセットを設定する
    /// </summary>
    void SetLockOnOffset(const Vector3& offset) { lockOnOffset_ = offset; }
    /// <summary>
    /// kAimable モードの回転(rotation_)を、
    /// 現在のカメラの向き (Eye -> Target または Eye -> FollowObject) に合わせる
    /// </summary>
    void SyncRotationToCurrentView();

    // --- 自由カメラ用セッター ---
    void SetEye(const Vector3& eye) { eye_ = eye; }
    void SetTarget(const Vector3& target) { target_ = target; }
    void SetRotation(const Vector3& rotation) { rotation_ = rotation; }
    void SetFreezeEye(bool freeze) { isEyeFrozen_ = freeze; }

    void SetOrbitParams(float radius, float height, float speed) {
        orbitRadius_ = radius;
        orbitHeight_ = height;
        orbitSpeed_ = speed;
    }
    void ConfigFixedPoint(const Vector3& position);
    Matrix4x4 GetViewProjectionMatrix() const {
        static Math math; 
        return math.Multiply(viewMatrix_, projectionMatrix_);
    }
    // ★追加: アスペクト比を外部から変更するためのセッター
    void SetAspectRatio(float ratio) { aspectRatio_ = ratio; }

    // ★追加: プロジェクション行列だけを即座に更新する関数
    void UpdateProjectionMatrix();
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



    // 追従対象のオブジェクト (Player)
    Object3d* followObject_ = nullptr;

    // 注視対象のオブジェクト (Enemy)
    Object3d* targetObject_ = nullptr;




    // --- モード/状態 ---
    FollowMode followMode_ = FollowMode::kAimable; // (デフォルトモード)
    bool isInputEnabled_ = true; // デフォルトで入力を有効化

    // --- オフセット/距離 ---

    // (kFixed 用)
    Vector3 fixedOffset_ = { 0.0f, 5.0f, -10.0f };

    // (kAimable 用)
    float distance_ = 10.0f;
    float minDistance_ = 3.0f;
    float maxDistance_ = 20.0f;

    // (kFirstPerson 用)
    Vector3 firstPersonOffset_ = { 0.0f, 1.5f, 0.0f };

    // (kAimable / kFirstPerson 共通の回転)
    Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };

    // (kLockOn 用) プレイヤーのY回転基準のオフセット
    Vector3 lockOnOffset_ = { 0.0f, 3.0f, -8.0f };

    float aimDistance_ = 15.0f;
    float aimHeight_ = 5.0f;
    Vector3 aimAngle_ = { 0.0f, 0.0f, 0.0f };
    bool isEyeFrozen_ = false;

    float orbitRadius_ = 15.0f;
    float orbitHeight_ = 5.0f;
    float orbitSpeed_ = 0.005f;
    float orbitAngle_ = 0.0f;
    Vector3 fixedPointPos_ = { 0.0f, 5.0f, -10.0f };
    Vector3 smoothTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 smoothEye_ = { 0.0f, 0.0f, 0.0f };
    bool isCameraInitialized_ = false;
};