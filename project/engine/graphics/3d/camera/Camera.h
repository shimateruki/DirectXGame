#pragma once
#include "InputManager.h"
#include "Object3d.h"
#include "engine/utility/math/Math.h"
#include <d3d12.h>
#include <algorithm>
#include <map>
#include <string>
#include <wrl.h>

/// <summary>
/// 3Dシーンの視点、投影、追従、演出用カメラ制御を扱う。
/// </summary>
// Cameraは、ビュー行列、射影行列、追従、ロックオン、演出用オーバーライドを管理します。
class Camera {
public:
    /// <summary>
    /// プレイヤー追従や演出で使うカメラモード。
    /// </summary>
        // 追従対象に対してカメラがどの動き方をするかを表します。
enum class FollowMode {
        kFixed,       // 対象に固定オフセットで追従する。
        kAimable,     // 三人称視点で、入力により回転できる。
        kFirstPerson, // 一人称視点。
        kLockOn,      // ロックオン対象も見る視点。
        kOrbit,       // 対象の周囲を回る視点。
        kFixedPoint,  // 固定位置と角度を使う視点。
    };

    // 演出カメラへ切り替える際の時間補間です。
    enum class OverrideEasing {
        kLinear,
        kEaseIn,
        kEaseOut,
        kEaseInOut,
        kSmootherStep,
    };

    // EyeとTargetを固定値またはシーンObjectから取得します。
    enum class OverrideEyeSource {
        kFixed,
        kSceneObject,
    };

    enum class OverrideTargetSource {
        kFixed,
        kSceneObject,
        kEyeObjectForward,
    };

    // 完全追従と、少し遅れて追従するモードを分けます。
    enum class OverrideFollowMode {
        kSnap,
        kSmooth,
    };

    /// <summary>
    /// シネマティック演出などで一時的に視点を上書きするための設定。
    /// </summary>
        // カットシーンや演出中に一時的にカメラ制御を差し替えるための設定です。
struct CameraOverrideParams {
        float duration = 1.0f;
        float exitDuration = 0.35f;
        OverrideEasing easing = OverrideEasing::kSmootherStep;

        OverrideEyeSource eyeSource = OverrideEyeSource::kFixed;
        std::string eyeObjectName;
        Object3d* eyeObject = nullptr; // CameraEditorが再生開始時に解決する非所有ポインタ。
        Vector3 eyeObjectOffset = { 0.0f, 0.0f, 0.0f };
        OverrideFollowMode eyeFollowMode = OverrideFollowMode::kSnap;
        float eyeFollowResponse = 12.0f;

        // Eye位置を通常カメラに追従させる軸と、固定する場合の値。
        bool trackEyeX = false;
        bool trackEyeY = false;
        bool trackEyeZ = false;
        Vector3 fixedEyePos = { 0.0f, 0.0f, 0.0f };

        OverrideTargetSource targetSource = OverrideTargetSource::kFixed;
        std::string targetObjectName;
        Object3d* targetFollowObject = nullptr; // CameraEditorが再生開始時に解決する非所有ポインタ。
        Vector3 targetObjectOffset = { 0.0f, 0.0f, 0.0f };
        float eyeForwardDistance = 10.0f;
        OverrideFollowMode targetFollowMode = OverrideFollowMode::kSnap;
        float targetFollowResponse = 14.0f;

        // 注視点を通常カメラに追従させる軸と、固定する場合の値。
        bool trackTargetX = true;
        bool trackTargetY = true;
        bool trackTargetZ = true;
        Vector3 fixedTargetPos = { 0.0f, 0.0f, 0.0f };
    };

public:
    /// <summary>
    /// カメラ行列とGPU用定数バッファを初期化する。
    /// </summary>
        // カメラの初期座標、行列、追従関連パラメータを設定します。
void Initialize();

    /// <summary>
    /// 入力、追従対象、演出状態を反映してカメラを更新する。
    /// </summary>
        // 入力、追従、ロックオン、演出補間を反映して行列を更新します。
void Update(float deltaTime = 1.0f / 60.0f);

    // 行列取得。
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
    const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
    const Matrix4x4& GetInverseViewProjectionMatrix() const { return inverseViewProjectionMatrix_; }

        // FOVやアスペクト比から射影行列を再計算します。
void UpdateProjectionMatrix();
    void SetAspectRatio(float ratio) { aspectRatio_ = ratio; }
    // 演出用カメラPreviewなど、通常Updateを通さず指定Eye/Targetで即時に描画用行列を作ります。
    void SetLookAtPreviewView(const Vector3& eye, const Vector3& target, float aspectRatio);
    // デバッグリプレイの記録値を通常の追従計算を通さず描画行列へ反映します。
    void ApplyReplayView(const Vector3& eye, const Vector3& target, const Vector3& rotation, float fovY, float nearClip, float farClip);

    // 入力と追従対象。
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }
        // プレイ状態や演出に応じてカメラ入力の有効無効を切り替えます。
void SetInputEnabled(bool enabled) { isInputEnabled_ = enabled; }
        // 追従するObject3dを設定します。
void SetFollowTarget(Object3d* target) { followObject_ = target; }
        // 注視やロックオンに使う対象Object3dを設定します。
void SetLockOnTarget(Object3d* target) { targetObject_ = target; }
    Object3d* GetFollowTarget() const { return followObject_; }

    // 現在のカメラ状態。
    const Vector3& GetEye() const { return eye_; }
    const Vector3& GetTargetPoint() const { return target_; }
    const Vector3& GetRotation() const { return rotation_; }

    // 任意制御用の設定。
    void SetEye(const Vector3& eye) { eye_ = eye; }
    void SetTarget(const Vector3& target) { target_ = target; }
    void SetRotation(const Vector3& rotation) { rotation_ = rotation; }
        // カメラ位置だけを固定し、ターゲット計算と切り離します。
void SetFreezeEye(bool freeze);
    void SetAimCameraSuppressed(bool suppressed) { isAimCameraSuppressed_ = suppressed; }

    void SetFovY(float fov) { fovY_ = fov; }
    float GetFovY() const { return fovY_; }
    void SetClipRange(float nearClip, float farClip) {
        nearClip_ = (std::max)(nearClip, 0.001f);
        farClip_ = (std::max)(farClip, nearClip_ + 0.01f);
    }
    float GetNearClip() const { return nearClip_; }
    float GetFarClip() const { return farClip_; }

    // モードとモード別パラメータ。
    void SetFollowMode(FollowMode mode) { followMode_ = mode; }
    FollowMode GetFollowMode() const { return followMode_; }

    void ConfigFixed(const Vector3& offset);
    void ConfigAimable(float distance, float height, const Vector3& angle);
    void ConfigFirstPerson(const Vector3& eyeOffset);
    void ConfigFixedPoint(const Vector3& position, const Vector3& angle);
    void SnapToThirdPerson(float distance, float height, float pitch);

    void SetOrbitParams(float radius, float height, float speed) {
        orbitRadius_ = radius;
        orbitHeight_ = height;
        orbitSpeed_ = speed;
    }

    void SetOrbitCenterOffset(const Vector3& offset) { orbitCenterOffset_ = offset; }
    void SetOrbitCenterHeight(float height) { orbitCenterHeight_ = height; }
    void SetOrbitAngle(float angle) { orbitAngle_ = angle; }
    float GetOrbitAngle() const { return orbitAngle_; }
    void ResetFollowSmoothing() { isCameraInitialized_ = false; }
    void SetLockOnOffset(const Vector3& offset) { lockOnOffset_ = offset; }

    // 入力操作と現在視点への同期。
    void AddRotation(const Vector2& mouseDelta);
    void SetRotationSensitivity(float sensitivity);
    float GetRotationSensitivity() const { return rotationSensitivity_; }
    void SyncRotationToCurrentView();

    // 演出用の一時上書きと画面揺れ。
    void StartOverride(const CameraOverrideParams& params);
    void EndOverride(float duration);
    bool IsOverridden() const { return isOverridden_; }
    float GetOverrideWeight() const { return overrideWeight_; }
    const Frustum& GetFrustum() const { return frustum_; }
    void StartShake(float duration, float amplitude, float frequency = 24.0f, const Vector3& axisWeight = { 1.0f, 1.0f, 0.5f });
    bool IsShaking() const { return shakeTimer_ > 0.0f; }

    ID3D12Resource* GetConstantBuffer() const { return constBuffer_.Get(); }

private:
    // View/Projectionから派生する行列、視錐台、GPU定数をまとめて更新します。
    void RefreshDerivedMatrices();

    struct CameraVP {
        Matrix4x4 view;
        Matrix4x4 projection;
    };

private:
    // カメラの基本ベクトル。
    Vector3 eye_ = { 0.0f, 0.0f, -10.0f };
    Vector3 target_ = { 0.0f, 0.0f, 0.0f };
    Vector3 up_ = { 0.0f, 1.0f, 0.0f };

    // 投影行列のパラメータ。
    float fovY_ = 0.45f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearClip_ = 0.1f;
    float farClip_ = 1000.0f;

    // View/Projection行列。
    Matrix4x4 viewMatrix_ = {};
    Matrix4x4 projectionMatrix_ = {};
    Matrix4x4 viewProjectionMatrix_ = Math::MakeIdentity4x4();
    Matrix4x4 inverseViewProjectionMatrix_ = Math::MakeIdentity4x4();

    // 入力と追従対象への参照。Cameraは所有しない。
    InputManager* inputManager_ = nullptr;
    Object3d* followObject_ = nullptr;
    Object3d* targetObject_ = nullptr;

    // 現在のモードと入力受付状態。
    FollowMode followMode_ = FollowMode::kAimable;
    bool isInputEnabled_ = true;

    // 追従の補間状態。
    Vector3 smoothTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 smoothEye_ = { 0.0f, 0.0f, 0.0f };
    bool isCameraInitialized_ = false;

    // 各モードで使うパラメータ。
    Vector3 fixedOffset_ = { 0.0f, 5.0f, -10.0f };
    float distance_ = 10.0f;
    float minDistance_ = 3.0f;
    float maxDistance_ = 20.0f;
    Vector3 firstPersonOffset_ = { 0.0f, 1.5f, 0.0f };
    Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 lockOnOffset_ = { 0.0f, 3.0f, -8.0f };

    float aimDistance_ = 15.0f;
    float aimHeight_ = 5.0f;
    Vector3 aimAngle_ = { 0.0f, 0.0f, 0.0f };
    bool isEyeFrozen_ = false;
    bool isAimCameraSuppressed_ = false;

    float orbitRadius_ = 15.0f;
    Vector3 orbitCenterOffset_ = { 0.0f, 0.0f, 0.0f };
    float orbitCenterHeight_ = 5.0f;
    float orbitHeight_ = 5.0f;
    float orbitSpeed_ = 0.005f;
    float orbitAngle_ = 0.0f;
    Vector3 fixedPointPos_ = { 0.0f, 5.0f, -10.0f };
    float rotationSensitivity_ = 1.0f;

    // カメラオーバーライドの補間状態。
    bool isOverridden_ = false;
    CameraOverrideParams overrideParams_;
    float overrideTimer_ = 0.0f;
    float overrideDuration_ = 0.0f;
    float overrideWeight_ = 0.0f;
    Vector3 overrideStartEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 overrideStartTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 overrideFollowEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 overrideFollowTarget_ = { 0.0f, 0.0f, 0.0f };
    bool overrideFollowInitialized_ = false;
    Vector3 fixedPointAngle_ = { 0.0f, 0.0f, 0.0f };
    Frustum frustum_;

    // 画面揺れの状態。
    float shakeTimer_ = 0.0f;
    float shakeDuration_ = 0.0f;
    float shakeAmplitude_ = 0.0f;
    float shakeFrequency_ = 24.0f;
    Vector3 shakeAxisWeight_ = { 1.0f, 1.0f, 0.5f };

    // GPUへ送るView/Projection行列。
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    CameraVP* constMap_ = nullptr;
    float aimTransition_ = 0.0f;
};
