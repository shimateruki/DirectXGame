#pragma once
#include "Camera.h"
#include <memory>

namespace engine { class InputManager; }

/// <summary>
/// メインカメラと一時的に有効なカメラを管理する。
/// </summary>
class CameraManager {
public:
    /// <summary>共有のカメラ管理インスタンスを取得する。</summary>
    static CameraManager* GetInstance();

    /// <summary>既定のメインカメラと管理状態を初期化する。</summary>
    void Initialize();

    /// <summary>所有するメインカメラを更新する。一時的なカメラはここでは更新しない。</summary>
    void Update(float deltaTime = 1.0f / 60.0f);

    /// <summary>カメラ操作で参照する入力管理を設定する。</summary>
    void SetInputManager(engine::InputManager* inputManager);

    /// <summary>所有するメインカメラを返す。</summary>
    Camera* GetMainCamera() { return mainCamera_.get(); }

    /// <summary>一時的なカメラがあればそれを、なければメインカメラを返す。</summary>
    Camera* GetActiveCamera() { return activeCamera_ ? activeCamera_ : mainCamera_.get(); }

    /// <summary>一時的なカメラへの参照を返す。未設定時はnullptr。</summary>
    Camera* GetActiveCameraOverride() const { return activeCamera_; }

    /// <summary>一時的に有効にするカメラを設定する。所有しない。</summary>
    void SetActiveCamera(Camera* camera) { activeCamera_ = camera; }

    /// <summary>メインカメラを演出用の固定視点へ切り替える。多重開始時はfalseを返す。</summary>
    bool BeginMainCameraPresentation();

    /// <summary>演出終了前に通常追従へ戻し、Camera ObjectのBlend Out先を設定する。</summary>
    void PrepareMainCameraReturn(engine::Object3d* followTarget, const engine::Vector3& eye,
                                 const engine::Vector3& target);

    /// <summary>プレイヤーを追う距離へ即座に合わせてから入力と有効カメラを復元する。</summary>
    void SnapMainCameraReturn(float distance, float height, float pitch);

    /// <summary>演出開始前のカメラ入力と有効カメラを復元する。</summary>
    void EndMainCameraPresentation();

    /// <summary>有効カメラへ画面揺れを要求する。</summary>
    void PlayShake(float duration, float amplitude, float frequency = 24.0f, const engine::Vector3& axisWeight = { 1.0f, 1.0f, 0.5f });

    /// <summary>有効カメラへ視野角のパルス演出を要求する。</summary>
    void PlayFovPulse(float duration, float amountRadians, float attackRatio = 0.12f);

    /// <summary>有効カメラの一時的な演出レイヤーを消去する。</summary>
    void ClearPresentationLayers();

private:
    CameraManager() = default;

    ~CameraManager() = default;

    CameraManager(const CameraManager&) = delete;

    CameraManager& operator=(const CameraManager&) = delete;

private:
    std::unique_ptr<Camera> mainCamera_;
    Camera* activeCamera_ = nullptr;
    Camera* presentationPreviousActiveCamera_ = nullptr;
    bool presentationPreviousInputEnabled_ = true;
    bool mainCameraPresentationActive_ = false;
};
