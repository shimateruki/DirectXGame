#pragma once
#include "InputManager.h"
#include "Object3d.h"
#include "engine/utility/math/Math.h"
#include <d3d12.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>

/// <summary>
/// 3Dシーンの視点、投影、追従、演出用の一時上書きを管理する。
/// </summary>
class Camera {
public:
    /// <summary>追従対象に対して視点をどう動かすかを表す。</summary>
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

    /// <summary>演出カメラが注視点をどこから取得するか。</summary>
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

    /// <summary>カットシーンなどで通常カメラを一時的に差し替える設定。</summary>
    struct CameraOverrideParams {
        float duration = 1.0f;
        float exitDuration = 0.35f;
        OverrideEasing easing = OverrideEasing::kSmootherStep;

        OverrideEyeSource eyeSource = OverrideEyeSource::kFixed;
        std::string eyeObjectName;
        engine::Object3d* eyeObject = nullptr; // CameraEditorが再生開始時に解決する非所有ポインタ。
        engine::Vector3 eyeObjectOffset = { 0.0f, 0.0f, 0.0f };
        OverrideFollowMode eyeFollowMode = OverrideFollowMode::kSnap;
        float eyeFollowResponse = 12.0f;

        // Eye位置を通常カメラに追従させる軸と、固定する場合の値。
        bool trackEyeX = false;
        bool trackEyeY = false;
        bool trackEyeZ = false;
        engine::Vector3 fixedEyePos = { 0.0f, 0.0f, 0.0f };

        OverrideTargetSource targetSource = OverrideTargetSource::kFixed;
        std::string targetObjectName;
        engine::Object3d* targetFollowObject = nullptr; // CameraEditorが再生開始時に解決する非所有ポインタ。
        engine::Vector3 targetObjectOffset = { 0.0f, 0.0f, 0.0f };
        float eyeForwardDistance = 10.0f;
        OverrideFollowMode targetFollowMode = OverrideFollowMode::kSnap;
        float targetFollowResponse = 14.0f;

        // 注視点を通常カメラに追従させる軸と、固定する場合の値。
        bool trackTargetX = true;
        bool trackTargetY = true;
        bool trackTargetZ = true;
        engine::Vector3 fixedTargetPos = { 0.0f, 0.0f, 0.0f };
    };

public:
    /// <summary>初期座標、行列、追従状態とGPU用定数バッファを初期化する。</summary>
    void Initialize();

    /// <summary>入力、追従、ロックオン、演出補間を反映して行列を更新する。</summary>
    /// <param name="deltaTime">前回更新からの経過秒数。</param>
    void Update(float deltaTime = 1.0f / 60.0f);

    /// <summary>ワールド座標から視点座標への変換行列を返す。</summary>
    const engine::Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    /// <summary>現在の画角・アスペクト比・クリップ範囲に対応する射影行列を返す。</summary>
    const engine::Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
    /// <summary>視点行列と射影行列を合成した行列を返す。</summary>
    const engine::Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
    /// <summary>画面座標をワールド空間へ戻す際に使う逆行列を返す。</summary>
    const engine::Matrix4x4& GetInverseViewProjectionMatrix() const { return inverseViewProjectionMatrix_; }

    /// <summary>視野角とアスペクト比から射影行列を再計算する。</summary>
    void UpdateProjectionMatrix();
    /// <summary>射影に使う画面の横縦比を設定する。行列の再計算は別途行う。</summary>
    void SetAspectRatio(float ratio) { aspectRatio_ = ratio; }

    /// <summary>演出プレビュー用に追従対象を外し、指定視点から描画行列を即時に作る。</summary>
    /// <param name="eye">カメラ位置。</param>
    /// <param name="target">注視点。eyeと一致した場合は前方へ補正する。</param>
    /// <param name="aspectRatio">プレビュー画面の横縦比。</param>
    void SetLookAtPreviewView(const engine::Vector3& eye, const engine::Vector3& target, float aspectRatio);

    /// <summary>記録された視点・投影条件を追従計算を通さず描画行列へ反映する。</summary>
    /// <param name="eye">記録時のカメラ位置。</param>
    /// <param name="target">記録時の注視点。</param>
    /// <param name="rotation">記録時の回転角。</param>
    /// <param name="fovY">垂直視野角（ラジアン）。</param>
    /// <param name="nearClip">近クリップ面の距離。</param>
    /// <param name="farClip">遠クリップ面の距離。</param>
    void ApplyReplayView(const engine::Vector3& eye, const engine::Vector3& target, const engine::Vector3& rotation, float fovY, float nearClip, float farClip);

    /// <summary>カメラ操作に使う入力の非所有参照を設定する。</summary>
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }

    /// <summary>カメラ操作入力の有効・無効を切り替える。</summary>
    void SetInputEnabled(bool enabled) { isInputEnabled_ = enabled; }

    /// <summary>追従対象を設定する。対象は所有しない。</summary>
    void SetFollowTarget(engine::Object3d* target) { followObject_ = target; }

    /// <summary>注視・ロックオン対象を設定する。対象は所有しない。</summary>
    void SetLockOnTarget(engine::Object3d* target) { targetObject_ = target; }
    /// <summary>現在の追従対象を返す。未設定ならnullptr。</summary>
    engine::Object3d* GetFollowTarget() const { return followObject_; }

    /// <summary>現在のカメラ位置を返す。</summary>
    const engine::Vector3& GetEye() const { return eye_; }
    /// <summary>現在の注視点を返す。</summary>
    const engine::Vector3& GetTargetPoint() const { return target_; }
    /// <summary>入力や演出で更新された回転角を返す。</summary>
    const engine::Vector3& GetRotation() const { return rotation_; }

    /// <summary>次の行列更新に使うカメラ位置を設定する。</summary>
    void SetEye(const engine::Vector3& eye) { eye_ = eye; }
    /// <summary>次の行列更新に使う注視点を設定する。</summary>
    void SetTarget(const engine::Vector3& target) { target_ = target; }
    /// <summary>次の追従計算に使う回転角を設定する。</summary>
    void SetRotation(const engine::Vector3& rotation) { rotation_ = rotation; }

    /// <summary>カメラ位置だけを固定し、ターゲット計算と切り離す。</summary>
    void SetFreezeEye(bool freeze);

    /// <summary>通常の照準カメラへの切り替えを一時的に抑制する。</summary>
    void SetAimCameraSuppressed(bool suppressed);

    /// <summary>垂直視野角をラジアンで設定・取得する。行列の再計算は別途行う。</summary>
    void SetFovY(float fov) { fovY_ = fov; }
    float GetFovY() const { return fovY_; }
    /// <summary>近遠クリップ面を設定する。近距離は0.001以上、遠距離は近距離より0.01以上に制限する。</summary>
    void SetClipRange(float nearClip, float farClip) {
        nearClip_ = (std::max)(nearClip, 0.001f);
        farClip_ = (std::max)(farClip, nearClip_ + 0.01f);
    }
    /// <summary>現在の近遠クリップ面の距離を取得する。</summary>
    float GetNearClip() const { return nearClip_; }
    float GetFarClip() const { return farClip_; }

    /// <summary>追従モードを設定・取得する。モードごとの配置値はConfig系関数で指定する。</summary>
    void SetFollowMode(FollowMode mode) { followMode_ = mode; }
    FollowMode GetFollowMode() const { return followMode_; }

    // 追従モードごとの配置パラメータを設定する。
    /// <summary>追従対象からの固定オフセットを設定する。</summary>
    void ConfigFixed(const engine::Vector3& offset);

    /// <summary>照準可能な三人称カメラの距離、高さ、初期角度を設定する。</summary>
    void ConfigAimable(float distance, float height, const engine::Vector3& angle);

    /// <summary>一人称視点での追従対象からの位置差を設定する。</summary>
    void ConfigFirstPerson(const engine::Vector3& eyeOffset);

    /// <summary>位置と角度を固定するカメラを設定する。</summary>
    void ConfigFixedPoint(const engine::Vector3& position, const engine::Vector3& angle);

    /// <summary>補間を待たず三人称視点の位置と角度へ合わせる。</summary>
    void SnapToThirdPerson(float distance, float height, float pitch);

    /// <summary>周回モードの半径、高さ、回転速度を設定する。</summary>
    void SetOrbitParams(float radius, float height, float speed) {
        orbitRadius_ = radius;
        orbitHeight_ = height;
        orbitSpeed_ = speed;
    }

    /// <summary>周回中心に加える3Dオフセットを設定する。</summary>
    void SetOrbitCenterOffset(const engine::Vector3& offset) { orbitCenterOffset_ = offset; }
    /// <summary>周回中心の高さ補正を設定する。</summary>
    void SetOrbitCenterHeight(float height) { orbitCenterHeight_ = height; }
    /// <summary>周回モードの現在角度を設定・取得する。</summary>
    void SetOrbitAngle(float angle) { orbitAngle_ = angle; }
    float GetOrbitAngle() const { return orbitAngle_; }
    /// <summary>追従補間の初期化状態を戻し、次の更新で視点を合わせ直す。</summary>
    void ResetFollowSmoothing() { isCameraInitialized_ = false; }
    /// <summary>ロックオン対象を映す際の注視点オフセットを設定する。</summary>
    void SetLockOnOffset(const engine::Vector3& offset) { lockOnOffset_ = offset; }

    // 入力操作と現在視点への同期。
    /// <summary>入力差分から現在のカメラ回転を進める。</summary>
    void AddRotation(const engine::Vector2& mouseDelta);

    /// <summary>入力による回転感度を設定する。</summary>
    void SetRotationSensitivity(float sensitivity);
    /// <summary>現在の回転感度を返す。</summary>
    float GetRotationSensitivity() const { return rotationSensitivity_; }

    /// <summary>現在の視点方向と回転操作用の角度を同期する。</summary>
    void SyncRotationToCurrentView();

    // 演出用の一時上書きと画面揺れ。
    /// <summary>演出カメラへの一時的な切替を開始する。</summary>
    void StartOverride(const CameraOverrideParams& params);

    /// <summary>指定秒数で演出カメラから通常視点へ戻す。終了時点の構図を補間開始値にする。</summary>
    void EndOverride(float duration);
    /// <summary>演出カメラの有効状態と通常視点への混合率を取得する。</summary>
    bool IsOverridden() const { return isOverridden_; }
    float GetOverrideWeight() const { return overrideWeight_; }
    /// <summary>現在の視点行列から抽出した視錐台を返す。</summary>
    const engine::Frustum& GetFrustum() const { return frustum_; }

    /// <summary>指定時間の画面揺れを追加する。axisWeightで各軸の揺れ幅を調整する。</summary>
    /// <param name="duration">揺れの継続時間（秒）。</param>
    /// <param name="amplitude">揺れの基本振幅。</param>
    /// <param name="frequency">揺れの周波数。</param>
    /// <param name="axisWeight">各軸の振幅倍率。</param>
    void StartShake(float duration, float amplitude, float frequency = 24.0f, const engine::Vector3& axisWeight = { 1.0f, 1.0f, 0.5f });

    /// <summary>画角を一時的に変化させる演出を追加する。</summary>
    /// <param name="duration">演出の継続時間（秒）。</param>
    /// <param name="amountRadians">通常の垂直視野角へ加える角度（ラジアン）。</param>
    /// <param name="attackRatio">継続時間のうち変化を立ち上げる割合。</param>
    void StartFovPulse(float duration, float amountRadians, float attackRatio = 0.12f);

    /// <summary>画面揺れと画角変化の演出を消去する。</summary>
    void ClearPresentationLayers();
    /// <summary>継続中の画面揺れが一つでもあるか返す。</summary>
    bool IsShaking() const { return !shakeLayers_.empty(); }

    /// <summary>描画へ渡すカメラ行列の定数バッファを返す。所有権は渡さない。</summary>
    ID3D12Resource* GetConstantBuffer() const { return constBuffer_.Get(); }

private:
    // View/Projectionから派生する行列、視錐台、GPU定数をまとめて更新します。
    void RefreshDerivedMatrices();

    /// <summary>GPUへ送るビュー行列と射影行列。</summary>
    struct CameraVP {
        engine::Matrix4x4 view;
        engine::Matrix4x4 projection;
    };

private:
    // カメラの基本ベクトル。
    engine::Vector3 eye_ = { 0.0f, 0.0f, -10.0f };
    engine::Vector3 target_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 up_ = { 0.0f, 1.0f, 0.0f };

    // 投影行列のパラメータ。
    float fovY_ = 0.45f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearClip_ = 0.1f;
    float farClip_ = 1000.0f;

    // View/Projection行列。
    engine::Matrix4x4 viewMatrix_ = {};
    engine::Matrix4x4 projectionMatrix_ = {};
    engine::Matrix4x4 viewProjectionMatrix_ = engine::Math::MakeIdentity4x4();
    engine::Matrix4x4 inverseViewProjectionMatrix_ = engine::Math::MakeIdentity4x4();

    // 入力と追従対象への参照。Cameraは所有しない。
    InputManager* inputManager_ = nullptr;
    engine::Object3d* followObject_ = nullptr;
    engine::Object3d* targetObject_ = nullptr;

    // 現在のモードと入力受付状態。
    FollowMode followMode_ = FollowMode::kAimable;
    bool isInputEnabled_ = true;

    // 追従の補間状態。
    engine::Vector3 smoothTarget_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 smoothEye_ = { 0.0f, 0.0f, 0.0f };
    bool isCameraInitialized_ = false;

    // 各モードで使うパラメータ。
    engine::Vector3 fixedOffset_ = { 0.0f, 5.0f, -10.0f };
    float distance_ = 10.0f;
    float minDistance_ = 3.0f;
    float maxDistance_ = 20.0f;
    engine::Vector3 firstPersonOffset_ = { 0.0f, 1.5f, 0.0f };
    engine::Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 lockOnOffset_ = { 0.0f, 3.0f, -8.0f };

    float aimDistance_ = 15.0f;
    float aimHeight_ = 5.0f;
    engine::Vector3 aimAngle_ = { 0.0f, 0.0f, 0.0f };
    bool isEyeFrozen_ = false;
    bool isAimCameraSuppressed_ = false;

    float orbitRadius_ = 15.0f;
    engine::Vector3 orbitCenterOffset_ = { 0.0f, 0.0f, 0.0f };
    float orbitCenterHeight_ = 5.0f;
    float orbitHeight_ = 5.0f;
    float orbitSpeed_ = 0.005f;
    float orbitAngle_ = 0.0f;
    engine::Vector3 fixedPointPos_ = { 0.0f, 5.0f, -10.0f };
    float rotationSensitivity_ = 1.0f;

    // カメラオーバーライドの補間状態。
    bool isOverridden_ = false;
    CameraOverrideParams overrideParams_;
    float overrideTimer_ = 0.0f;
    float overrideDuration_ = 0.0f;
    float overrideWeight_ = 0.0f;
    engine::Vector3 overrideStartEye_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 overrideStartTarget_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 overrideFollowEye_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 overrideFollowTarget_ = { 0.0f, 0.0f, 0.0f };
    bool overrideFollowInitialized_ = false;
    engine::Vector3 fixedPointAngle_ = { 0.0f, 0.0f, 0.0f };
    engine::Frustum frustum_;

    // 複数の演出要求を加算し、同時発火でも後から来た要求で上書きしません。
    struct ShakeLayer {
        float elapsed = 0.0f;
        float duration = 0.01f;
        float amplitude = 0.0f;
        float frequency = 24.0f;
        engine::Vector3 axisWeight = { 1.0f, 1.0f, 0.5f };
    };

    /// <summary>画角変化の経過時間と大きさを保持する演出レイヤー。</summary>
    struct FovPulseLayer {
        float elapsed = 0.0f;
        float duration = 0.01f;
        float amountRadians = 0.0f;
        float attackRatio = 0.12f;
    };
    std::vector<ShakeLayer> shakeLayers_;
    std::vector<FovPulseLayer> fovPulseLayers_;

    // GPUへ送るView/Projection行列。
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    CameraVP* constMap_ = nullptr;
    float aimTransition_ = 0.0f;
};
