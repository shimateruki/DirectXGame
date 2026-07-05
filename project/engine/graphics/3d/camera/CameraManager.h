#pragma once
#include "Camera.h"
#include <memory>

class InputManager;

/// <summary>
/// メインカメラと現在有効なカメラを管理するシングルトン。
/// </summary>
class CameraManager {
public:
    /// <summary>
    /// シングルトンインスタンスを取得する。
    /// </summary>
    static CameraManager* GetInstance();

    /// <summary>
    /// メインカメラを初期化する。
    /// </summary>
    void Initialize();

    /// <summary>
    /// 有効カメラを更新する。
    /// </summary>
    void Update();

    // カメラ操作で参照する入力管理を設定する。
    void SetInputManager(InputManager* inputManager);

    // カメラ参照の取得と切り替え。
    Camera* GetMainCamera() { return mainCamera_.get(); }
    Camera* GetActiveCamera() { return activeCamera_ ? activeCamera_ : mainCamera_.get(); }
    Camera* GetActiveCameraOverride() const { return activeCamera_; }
    void SetActiveCamera(Camera* camera) { activeCamera_ = camera; }

private:
    CameraManager() = default;
    ~CameraManager() = default;
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

private:
    std::unique_ptr<Camera> mainCamera_;
    Camera* activeCamera_ = nullptr;
};
