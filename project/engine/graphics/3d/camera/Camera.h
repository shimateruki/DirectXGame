#pragma once
#include "engine/utility/math/Math.h"
#include "InputManager.h"
#include "Object3d.h" 
#include <map>
/// <summary>
/// 3Dシーンの視点を管理するカメラクラス
/// </summary>
class Camera {
public:
    // ==================================================
    // 列挙型・構造体
    // ==================================================
    // リリースビルド（追従時）のカメラモード
    enum class FollowMode {
        kFixed,         // 従来の固定オフセット追従
        kAimable,       // プレイヤーの後ろからマウスで視点操作・ズーム可能
        kFirstPerson,   // 一人称視点
        kLockOn,        // ロックオンモード
        kOrbit,         // 周回
        kFixedPoint,    // 定点カメラ
    };

    // 空間・シネマティック演出用のカメラパラメータ
    struct CameraOverrideParams {
        float duration = 1.0f; // 移行にかかる時間（秒）

        // --- カメラ位置 (Eye) の設定 ---
        bool trackEyeX = false; // X軸をプレイヤー(通常カメラ)に追従させるか
        bool trackEyeY = false; // Y軸を追従させるか
        bool trackEyeZ = false; // Z軸を追従させるか
        Vector3 fixedEyePos = { 0.0f, 0.0f, 0.0f }; // 追従しない軸はこの値で固定される

        // --- 注視点 (Target) の設定 ---
        bool trackTargetX = true; // デフォルトは対象を完全追従
        bool trackTargetY = true;
        bool trackTargetZ = true;
        Vector3 fixedTargetPos = { 0.0f, 0.0f, 0.0f };
    };

public:
    // ==================================================
    // 初期化・更新
    // ==================================================
    void Initialize();
    void Update();

    // ==================================================
    // ゲッター・セッター (行列)
    // ==================================================
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

    Matrix4x4 GetViewProjectionMatrix() const {
        static Math math;
        return math.Multiply(viewMatrix_, projectionMatrix_);
    }

    void UpdateProjectionMatrix();
    void SetAspectRatio(float ratio) { aspectRatio_ = ratio; }

    // ==================================================
    // ゲッター・セッター (ターゲット・入力)
    // ==================================================
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }
    void SetInputEnabled(bool enabled) { isInputEnabled_ = enabled; }
    void SetFollowTarget(Object3d* target) { followObject_ = target; }
    void SetLockOnTarget(Object3d* target) { targetObject_ = target; }
    Object3d* GetFollowTarget() const { return followObject_; }

    // ==================================================
    // ゲッター・セッター (カメラ状態)
    // ==================================================
    const Vector3& GetEye() const { return eye_; }
    const Vector3& GetTargetPoint() const { return target_; }
    const Vector3& GetRotation() const { return rotation_; } // kAimable, kFirstPerson用

    // --- 自由カメラ用セッター ---
    void SetEye(const Vector3& eye) { eye_ = eye; }
    void SetTarget(const Vector3& target) { target_ = target; }
    void SetRotation(const Vector3& rotation) { rotation_ = rotation; }
    void SetFreezeEye(bool freeze) { isEyeFrozen_ = freeze; }

    // ==================================================
    // モード管理・設定
    // ==================================================
    void SetFollowMode(FollowMode mode) { followMode_ = mode; }
    FollowMode GetFollowMode() const { return followMode_; }

    void ConfigFixed(const Vector3& offset);
    void ConfigAimable(float distance, float height, const Vector3& angle);
    void ConfigFirstPerson(const Vector3& eyeOffset);
    void ConfigFixedPoint(const Vector3& position, const Vector3& angle);

    void SetOrbitParams(float radius, float height, float speed) {
        orbitRadius_ = radius;
        orbitHeight_ = height;
        orbitSpeed_ = speed;
    }
    void SetLockOnOffset(const Vector3& offset) { lockOnOffset_ = offset; }

    // ==================================================
    // 操作・同期
    // ==================================================
    void AddRotation(const Vector2& mouseDelta);
    
    /// <summary>
    /// 感度を設定します (-5 ~ 5)
    /// </summary>
    void SetSensitivity(int level);
    void SyncRotationToCurrentView();

    // ==================================================
    // 動的カメラオーバーライド（シネマティック演出・視点変更用）
    // ==================================================
    void StartOverride(const CameraOverrideParams& params);
    void EndOverride(float duration);
    bool IsOverridden() const { return isOverridden_; }
    float GetOverrideWeight() const { return overrideWeight_; }
    const Frustum& GetFrustum() const { return frustum_; }
private:
    // ==================================================
    // メンバ変数
    // ==================================================

    // --- カメラの三要素 ---
    Vector3 eye_ = { 0.0f, 0.0f, -10.0f };
    Vector3 target_ = { 0.0f, 0.0f, 0.0f };
    Vector3 up_ = { 0.0f, 1.0f, 0.0f };

    // --- プロジェクション行列のパラメータ ---
    float fovY_ = 0.45f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearClip_ = 0.1f;
    float farClip_ = 1000.0f;

    // --- 行列 ---
    Matrix4x4 viewMatrix_ = {};
    Matrix4x4 projectionMatrix_ = {};

    // --- ポインタ ---
    InputManager* inputManager_ = nullptr;
    Object3d* followObject_ = nullptr;
    Object3d* targetObject_ = nullptr;

    // --- モード/状態 ---
    FollowMode followMode_ = FollowMode::kAimable;
    bool isInputEnabled_ = true;

    // --- 補間用・スムージング ---
    Vector3 smoothTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 smoothEye_ = { 0.0f, 0.0f, 0.0f };
    bool isCameraInitialized_ = false;

    // --- 各モード用パラメータ ---
    Vector3 fixedOffset_ = { 0.0f, 5.0f, -10.0f };       // kFixed 用
    float distance_ = 10.0f;                             // kAimable 用
    float minDistance_ = 3.0f;
    float maxDistance_ = 20.0f;
    Vector3 firstPersonOffset_ = { 0.0f, 1.5f, 0.0f };   // kFirstPerson 用
    Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };            // kAimable / kFirstPerson用
    Vector3 lockOnOffset_ = { 0.0f, 3.0f, -8.0f };       // kLockOn 用

    float aimDistance_ = 15.0f;
    float aimHeight_ = 5.0f;
    Vector3 aimAngle_ = { 0.0f, 0.0f, 0.0f };
    bool isEyeFrozen_ = false;
    float sensitivityMultiplier_ = 1.0f; // 感度倍率

    float orbitRadius_ = 15.0f;
    float orbitHeight_ = 5.0f;
    float orbitSpeed_ = 0.005f;
    float orbitAngle_ = 0.0f;
    Vector3 fixedPointPos_ = { 0.0f, 5.0f, -10.0f };

    // --- カメラオーバーライド用 ---
    bool isOverridden_ = false;
    CameraOverrideParams overrideParams_; // 構造体で管理
    float overrideTimer_ = 0.0f;
    float overrideDuration_ = 0.0f;
    float overrideWeight_ = 0.0f;         // 0.0(通常) ～ 1.0(完全オーバーライド)
    Vector3 overrideStartEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 overrideStartTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 fixedPointAngle_ = { 0.0f, 0.0f, 0.0f };
    Frustum frustum_;
};