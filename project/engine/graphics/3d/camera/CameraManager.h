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

private:
    CameraManager() = default;
    ~CameraManager() = default;
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

private:
    std::unique_ptr<Camera> mainCamera_;
    Camera* activeCamera_ = nullptr;
};
