#pragma once
#include "Camera.h"
#include <memory>

class InputManager;

/// <summary>
/// メインカメラと現在有効なカメラを管理するシングルトン。
/// </summary>
// CameraManagerは、ゲームやエディタで使う複数カメラの登録、選択、更新を管理します。
class CameraManager {
public:
    /// <summary>
    /// シングルトンインスタンスを取得する。
    /// </summary>
        // 共通利用するカメラ管理インスタンスを取得します。
static CameraManager* GetInstance();

    /// <summary>
    /// メインカメラを初期化する。
    /// </summary>
        // デフォルトカメラを準備し、管理状態を初期化します。
void Initialize();

    /// <summary>
    /// 有効カメラを更新する。
    /// </summary>
        // 現在アクティブなカメラを更新します。
void Update(float deltaTime = 1.0f / 60.0f);

    // カメラ操作で参照する入力管理を設定する。
    void SetInputManager(InputManager* inputManager);

    // カメラ参照の取得と切り替え。
    Camera* GetMainCamera() { return mainCamera_.get(); }
    Camera* GetActiveCamera() { return activeCamera_ ? activeCamera_ : mainCamera_.get(); }
    Camera* GetActiveCameraOverride() const { return activeCamera_; }
    void SetActiveCamera(Camera* camera) { activeCamera_ = camera; }

    // Gameplay Feedback Cueからカメラ演出を共通呼び出しします。
    void PlayShake(float duration, float amplitude, float frequency = 24.0f, const Vector3& axisWeight = { 1.0f, 1.0f, 0.5f });
    void PlayFovPulse(float duration, float amountRadians, float attackRatio = 0.12f);
    void ClearPresentationLayers();

private:
    CameraManager() = default;
    ~CameraManager() = default;
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

private:
    std::unique_ptr<Camera> mainCamera_;
    Camera* activeCamera_ = nullptr;
};
