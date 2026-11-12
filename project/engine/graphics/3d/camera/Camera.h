#pragma once
#include "InputManager.h"
#include "CameraVisibilitySettings.h"
#include "Object3D.h"
#include "engine/utility/math/Math.h"
#include <d3d12.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>

// 担当：視点・投影・追従と、一時的なカメラ演出を管理する。
// 関連：CameraManager（選択・更新）、InputManager（操作）、Object3D（追従対象）
// 主な入口：Initialize / Update / SetFollowTarget / StartOverride
class Camera {
public:
    // 自由操作を使うゲームと、構図をゲーム側で決めるゲームを同じカメラで扱う。
    enum class GameplayStyle { Free, Guided };
    enum class ObstacleResponse { ShortenDistance, PreserveComposition };
    void ConfigureGameplayStyle(GameplayStyle style, ObstacleResponse obstacles,
        float yawLimitDegrees, float pitchLimitDegrees, float returnResponse, float fov);
    bool IsGuidedGameplay() const { return gameplayStyle_ == GameplayStyle::Guided; }
    // 誘導カメラで、障害物に対して構図を維持する設定か返す。
    bool PreservesComposition() const { return IsGuidedGameplay() && visibilitySettings_.enabled && obstacleResponse_ == ObstacleResponse::PreserveComposition; }
    engine::Vector2 GetGuidedLookOffset() const { return guidedLookOffset_; }
    // 余白は画面半幅/半高に対する比率、先読みはワールド距離、応答は毎秒。
    void ConfigureGuidedFraming(float lookAhead, float deadZoneWidth, float deadZoneHeight, float response);
    float GetGuidedLookAhead() const { return guidedLookAhead_; }
    float GetGuidedDeadZoneWidth() const { return guidedDeadZoneWidth_; }
    float GetGuidedFramingResponse() const { return guidedFramingResponse_; }
    engine::Vector3 GetGuidedFocusAnchor() const { return guidedFocusAnchor_; }
    engine::Vector3 GetGuidedLeadOffset() const { return guidedLeadOffset_; }

    void SetVisibilitySettings(CameraVisibilitySettings value) { value.Validate(); visibilitySettings_ = value; }
    const CameraVisibilitySettings& GetVisibilitySettings() const { return visibilitySettings_; }
    // 遮蔽物が近づく側は安全距離を即時採用し、離れる側だけ時間補間する。
    static float RecoverCollisionDistance(float previous, float safeDistance, float frameDeltaTime, float seconds);

    /// <summary>追従対象に対して視点をどう動かすかを表す。</summary>
    enum class FollowMode {
        Fixed,       // 対象に固定オフセットで追従する。
        Aimable,     // 三人称視点で、入力により回転できる。
        FirstPerson, // 一人称視点。
        LockOn,      // ロックオン対象も見る視点。
        Orbit,       // 対象の周囲を回る視点。
        FixedPoint,  // 固定位置と角度を使う視点。
    };

    // 演出カメラへ切り替える際の時間補間です。
    enum class OverrideEasing {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
        SmootherStep,
    };

    // EyeとTargetを固定値またはシーンObjectから取得します。
    enum class OverrideEyeSource {
        Fixed,
        SceneObject,
        SceneObjectRelativeToReference,
    };

    /// <summary>演出カメラが注視点をどこから取得するか。</summary>
    enum class OverrideTargetSource {
        Fixed,
        SceneObject,
        EyeObjectForward,
    };

    // 完全追従と、少し遅れて追従するモードを分けます。
    enum class OverrideFollowMode {
        Snap,
        Smooth,
    };

    /// <summary>カットシーンなどで通常カメラを一時的に差し替える設定。</summary>
    // 関連：Camera（この型を使う機能の本体）
    struct CameraOverrideParams {
        float duration = 1.0f; // 演出レイヤーまたは上書きの継続秒数。
        float exitDuration = 0.35f; // 通常カメラへ戻すブレンド秒数。
        OverrideEasing easing = OverrideEasing::SmootherStep;

        OverrideEyeSource eyeSource = OverrideEyeSource::Fixed;
        std::string eyeObjectName;
        engine::Object3D* eyeObject = nullptr; // CameraEditorが再生開始時に解決する非所有ポインタ。
        engine::Vector3 eyeObjectOffset = engine::Vector3::ZERO;
        std::string eyeReferenceObjectName;
        engine::Object3D* eyeReferenceObject = nullptr; // 相対方向を決める非所有参照。
        OverrideFollowMode eyeFollowMode = OverrideFollowMode::Snap;
        float eyeFollowResponse = 12.0f;

        // Eye位置を通常カメラに追従させる軸と、固定する場合の値。
        bool trackEyeX = false;
        bool trackEyeY = false;
        bool trackEyeZ = false;
        engine::Vector3 fixedEyePos = engine::Vector3::ZERO;

        OverrideTargetSource targetSource = OverrideTargetSource::Fixed;
        std::string targetObjectName;
        engine::Object3D* targetFollowObject = nullptr; // CameraEditorが再生開始時に解決する非所有ポインタ。
        engine::Vector3 targetObjectOffset = engine::Vector3::ZERO;
        float eyeForwardDistance = 10.0f;
        OverrideFollowMode targetFollowMode = OverrideFollowMode::Snap;
        float targetFollowResponse = 14.0f;

        // 注視点を通常カメラに追従させる軸と、固定する場合の値。
        bool trackTargetX = true;
        bool trackTargetY = true;
        bool trackTargetZ = true;
        engine::Vector3 fixedTargetPos = engine::Vector3::ZERO;
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
    void SetInputManager(engine::InputManager* inputManager) { inputManager_ = inputManager; }

    /// <summary>カメラ操作入力の有効・無効を切り替える。</summary>
    void SetInputEnabled(bool enabled) { isInputEnabled_ = enabled; }

    bool IsInputEnabled() const { return isInputEnabled_; }

    /// <summary>追従対象を設定する。対象は所有しない。</summary>
    void SetFollowTarget(engine::Object3D* target) {
        if (followObject_ != target) {
            guidedFocusInitialized_ = false;
            hasFollowHeight_ = false;
            collisionDistance_ = -1.0f;
            followTargetGrounded_ = true; // 追従対象の接地状態。
        }
        followObject_ = target;
    }

    /// <summary>ゲーム側が確定した接地状態を渡す。カメラはキャラクター型に依存しない。</summary>
    void SetFollowTargetGrounded(bool grounded) { followTargetGrounded_ = grounded; }

    /// <summary>接地状態から追従基準を更新して返す。ジャンプ中は上昇分を抑え、着地・落下には追従する。</summary>
    /// <remarks>区画選択と描画で共用する。同一フレームで複数回呼んでも補間を重ねない。</remarks>
    engine::Vector3 UpdateFollowReferencePosition();

    /// <summary>ジャンプ中の上下追従を抑え、通常の横追従と手動回転は維持する。</summary>
    void SetJumpHeightStabilization(bool enabled, float maxRise = 8.0f) {
        stabilizeJumpHeight_ = enabled;
        jumpHeightAllowance_ = std::clamp(maxRise, 1.0f, 30.0f);
    }

    /// <summary>注視・ロックオン対象を設定する。対象は所有しない。</summary>
    void SetLockOnTarget(engine::Object3D* target) { targetObject_ = target; }

    /// <summary>現在の追従対象を返す。未設定ならnullptr。</summary>
    engine::Object3D* GetFollowTarget() const { return followObject_; }

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
    void SetFovY(float fov);

    float GetFovY() const { return fovY_; }

    /// <summary>現在の画角から指定画角へ、カメラ更新時に補間する。</summary>
    void BlendFovY(float targetFov, float duration, OverrideEasing easing = OverrideEasing::SmootherStep);

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

    /// <summary>基準Objectから参照Objectへの水平方向を前方とする相対視点を求める。</summary>
    /// <remarks>offsetは右、上、前の順。親を持たないObjectは未更新の行列に依存せずTranslateを使う。</remarks>
    static engine::Vector3 ResolveRelativeEye(const engine::Object3D& origin, const engine::Object3D& reference,
                                               const engine::Vector3& offset);

    // 演出用の一時上書きと画面揺れ。
    /// <summary>演出カメラへの一時的な切替を開始する。</summary>
    void StartOverride(const CameraOverrideParams& params);

    /// <summary>指定秒数で演出カメラから通常視点へ戻す。終了時点の構図を補間開始値にする。</summary>
    void EndOverride(float duration);

    /// <summary>演出カメラの有効状態と通常視点への混合率を取得する。</summary>
    bool IsOverridden() const { return isOverridden_; }

    float GetOverrideWeight() const { return overrideWeight_; }

    /// 開始番号で範囲カメラと外部演出の所有権を区別する。
    uint64_t GetOverrideRevision() const { return overrideRevision_; }
    /// 範囲カメラは参照先を毎回解決し、Cameraには固定値だけを渡す。
    void UpdateRegionPose(const engine::Vector3& eye, const engine::Vector3& target) {
        overrideParams_.fixedEyePos = eye;
        overrideParams_.fixedTargetPos = target;
    }
    /// 通常カメラの投影設定を書き換えず、範囲の重みで合成する。
    void SetRegionProjection(float fov, float nearClip, float farClip, engine::Object3D* subject) {
        if (!regionProjectionActive_) regionProjectionStart_ = renderedProjection_;
        regionProjection_ = {fov, nearClip, farClip};
        if (regionSafetySubject_ != subject) hasRegionSafeEye_ = false;
        regionSafetySubject_ = subject;
        regionProjectionActive_ = true;
    }
    // 領域による投影設定と安全位置の参照・履歴を解除する。
    void ClearRegionProjection() { regionProjectionActive_ = false; regionSafetySubject_ = nullptr; hasRegionSafeEye_ = false; }
    /// 手動操作へ戻る初回は、見えている視点を追従補間の始点へ引き継ぐ。
    void ResumeManualView() {
        collisionDistance_ = -1.0f;
        SyncRotationToCurrentView(); smoothEye_ = eye_; smoothTarget_ = target_;
        // 手動復帰で画角だけが瞬間的に変わらないよう、見えていた画角から戻す。
        const float followFov = fovY_;
        fovY_ = renderedProjection_.x;
        BlendFovY(followFov, 0.3f, OverrideEasing::SmootherStep);
    }
    // 手動視点入力の有無を返し、その通知を消費する。
    bool ConsumeManualLookInput() { const bool value = manualLookInput_; manualLookInput_ = false; return value; }
    bool IsAimCameraRequested() const {
        return inputManager_ && inputManager_->IsActionPressed("SecondaryAction") &&
            !isAimCameraSuppressed_ && followMode_ != FollowMode::LockOn;
    }

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
    bool IsFollowAimInputActive() const;

    // 追従対象から誘導カメラの注視点を更新する。dtは秒。
    engine::Vector3 UpdateGuidedFocus(const engine::Vector3& subject, float frameDeltaTime);
    bool guidedFocusInitialized_ = false;
    engine::Vector3 guidedFocusAnchor_{}, guidedPreviousSubject_{}, guidedVelocity_{}, guidedLeadOffset_{};
    engine::Vector3 guidedLastDirection_{0,0,1};
    float guidedLookAhead_ = 6.0f;
    float guidedDeadZoneWidth_ = 0.10f, guidedDeadZoneHeight_ = 0.10f;
    float guidedFramingResponse_ = 3.0f;
    GameplayStyle gameplayStyle_ = GameplayStyle::Free;
    ObstacleResponse obstacleResponse_ = ObstacleResponse::ShortenDistance;
    engine::Vector2 guidedLookOffset_{}; // X=左右、Y=上下。保存する基準角度とは分離する。
    float guidedYawLimit_ = 0.174532925f;
    float guidedPitchLimit_ = 0.087266463f;
    float guidedReturnResponse_ = 5.0f;
    float freeStyleFov_ = 0.45f; // 自由操作スタイルの縦画角（ラジアン）。
    bool guidedInputThisFrame_ = false;

    CameraVisibilitySettings visibilitySettings_;
    float collisionDistance_ = -1.0f;

    // View/Projectionから派生する行列、視錐台、GPU定数をまとめて更新します。
    void RefreshDerivedMatrices();

    /// <summary>GPUへ送るビュー行列と射影行列。</summary>
    // 関連：Camera（この型を使う機能の本体）
    struct CameraVP {
        engine::Matrix4x4 view;
        engine::Matrix4x4 projection;
    };

private:
    uint64_t overrideRevision_ = 0;
    bool regionProjectionActive_ = false;
    engine::Vector3 regionProjection_ = {0.45f, 0.1f, 1000.0f};
    engine::Vector3 regionProjectionStart_ = {0.45f, 0.1f, 1000.0f};
    engine::Vector3 renderedProjection_ = {0.45f, 0.1f, 1000.0f};
    float regionProjectionExitWeight_ = 1.0f;
    bool manualLookInput_ = false;
    engine::Object3D* regionSafetySubject_ = nullptr; // 更新時に現シーンで存在確認する非所有参照。
    engine::Vector3 previousRegionSafeEye_{};
    bool hasRegionSafeEye_ = false;

    // 現在の視点位置、注視点、上方向。View行列の入力値。
    engine::Vector3 eye_ = { 0.0f, 0.0f, -10.0f };
    engine::Vector3 target_ = engine::Vector3::ZERO;
    engine::Vector3 up_ = { 0.0f, 1.0f, 0.0f };

    // 垂直画角・画面比・描画する奥行き範囲。Projection行列の入力値。
    float fovY_ = 0.45f;
    float fovBlendStart_ = 0.45f; // 画角ブレンド開始値（ラジアン）。
    float fovBlendTarget_ = 0.45f; // 画角ブレンドの目標値（ラジアン）。
    float fovBlendElapsed_ = 0.0f; // 画角ブレンドの経過秒数。
    float fovBlendDuration_ = 0.0f; // 画角ブレンド全体の秒数。
    OverrideEasing fovBlendEasing_ = OverrideEasing::SmootherStep;
    bool fovBlendActive_ = false;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearClip_ = 0.1f; // 近クリップ面までの距離。
    float farClip_ = 1000.0f; // 遠クリップ面までの距離。

    // 描画用View/Projection、合成行列、画面座標から逆投影する行列。
    engine::Matrix4x4 viewMatrix_ = {};
    engine::Matrix4x4 projectionMatrix_ = {};
    engine::Matrix4x4 viewProjectionMatrix_ = engine::Math::MakeIdentity4x4();
    engine::Matrix4x4 inverseViewProjectionMatrix_ = engine::Math::MakeIdentity4x4();

    // 入力と追従対象への参照。Cameraは所有しない。
    engine::InputManager* inputManager_ = nullptr;
    engine::Object3D* followObject_ = nullptr;
    engine::Object3D* targetObject_ = nullptr; // 追従対象。借用参照。

    // 追従・一人称などの計算方式と、手動カメラ入力の許可状態。
    FollowMode followMode_ = FollowMode::Aimable;
    bool isInputEnabled_ = true;

    // 追従対象へ滑らかに寄せる途中の視点と注視点。初回だけ即時合わせする。
    engine::Vector3 smoothTarget_ = engine::Vector3::ZERO;
    engine::Vector3 smoothEye_ = engine::Vector3::ZERO;
    bool isCameraInitialized_ = false;
    engine::Vector3 previousLockOnPlayerPosition_ = engine::Vector3::ZERO;
    bool hasLockOnPlayerPosition_ = false;

    // 接地時の高さを空中へ引き継ぐ。大きな落下と上昇は追い、画面外への離脱を防ぐ。
    bool stabilizeJumpHeight_ = true;
    bool followTargetGrounded_ = true; // 追従対象の接地状態。
    bool hasFollowHeight_ = false;
    float followHeight_ = 0.0f;
    float jumpHeightAllowance_ = 8.0f;

    // 固定追従のずれ、ズーム距離とその上下限、一人称の視点ずれ。
    engine::Vector3 fixedOffset_ = { 0.0f, 5.0f, -10.0f };
    float distance_ = 10.0f;
    float minDistance_ = 3.0f;
    float maxDistance_ = 20.0f;
    engine::Vector3 firstPersonOffset_ = { 0.0f, 1.5f, 0.0f }; // 一人称視点の位置補正。
    engine::Vector3 rotation_ = engine::Vector3::ZERO;
    engine::Vector3 lockOnOffset_ = { 0.0f, 3.0f, -8.0f }; // ロックオン時の位置補正。

    // 通常の照準カメラの距離・高さ・向きと、一時的な視点固定状態。
    float aimDistance_ = 15.0f;
    float aimHeight_ = 5.0f;
    engine::Vector3 aimAngle_ = engine::Vector3::ZERO; // 手動照準の回転（ラジアン）。
    bool isEyeFrozen_ = false;
    bool isAimCameraSuppressed_ = false; // 照準カメラへの移行を一時抑止するか。

    // 周回カメラの軌道・角速度と、固定位置カメラの配置。
    float orbitRadius_ = 15.0f;
    engine::Vector3 orbitCenterOffset_ = engine::Vector3::ZERO;
    float orbitCenterHeight_ = 5.0f;
    float orbitHeight_ = 5.0f;
    float orbitSpeed_ = 0.005f;
    float orbitAngle_ = 0.0f; // 周回位置の角度（ラジアン）。
    engine::Vector3 fixedPointPos_ = { 0.0f, 5.0f, -10.0f };
    float rotationSensitivity_ = 1.0f;

    // 演出用カメラ上書きの設定、補間時刻、元の構図と追従構図。
    bool isOverridden_ = false;
    CameraOverrideParams overrideParams_;
    float overrideTimer_ = 0.0f; // カメラ上書きの経過秒数。
    float overrideDuration_ = 0.0f; // 現在の上書き区間の秒数。
    float overrideWeight_ = 0.0f;
    engine::Vector3 overrideStartEye_ = engine::Vector3::ZERO;
    engine::Vector3 overrideStartTarget_ = engine::Vector3::ZERO;
    engine::Vector3 overrideFollowEye_ = engine::Vector3::ZERO;
    engine::Vector3 overrideFollowTarget_ = engine::Vector3::ZERO;
    bool overrideFollowInitialized_ = false;

    // 固定位置カメラの向きと、現在のView/Projectionから作った視錐台。
    engine::Vector3 fixedPointAngle_ = engine::Vector3::ZERO;
    engine::Frustum frustum_;

    // 複数の演出要求を加算し、同時発火でも後から来た要求で上書きしません。
    // 関連：Camera（この型を使う機能の本体）
    struct ShakeLayer {
        float elapsed = 0.0f;
        float duration = 0.01f; // 演出レイヤーまたは上書きの継続秒数。
        float amplitude = 0.0f;
        float frequency = 24.0f;
        engine::Vector3 axisWeight = { 1.0f, 1.0f, 0.5f };
    };

    /// <summary>画角変化の経過時間と大きさを保持する演出レイヤー。</summary>
    // 関連：Camera（この型を使う機能の本体）
    struct FovPulseLayer {
        float elapsed = 0.0f;
        float duration = 0.01f; // 演出レイヤーまたは上書きの継続秒数。
        float amountRadians = 0.0f;
        float attackRatio = 0.12f;
    };

    // 複数要求を同時再生し、完了したレイヤーだけ取り除く。
    std::vector<ShakeLayer> shakeLayers_;
    std::vector<FovPulseLayer> fovPulseLayers_;

    // GPUへ送るView/Projection行列のバッファとマップ済み書込先。
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    CameraVP* constMap_ = nullptr; // GPUカメラ定数バッファのマップ先。

    // 通常視点から照準視点へ移る途中の補間率。
    float aimTransition_ = 0.0f;
};
