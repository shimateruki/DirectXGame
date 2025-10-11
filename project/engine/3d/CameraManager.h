#pragma once
#include "engine/3d/Camera.h"
#include <memory>

class InputManager;

/// <summary>
/// カメラを管理するシングルトンクラス
/// </summary>
class CameraManager {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static CameraManager* GetInstance();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新
    /// </summary>
    void Update();

    // --- セッター ---
    void SetInputManager(InputManager* inputManager);

    // --- ゲッター ---
    Camera* GetMainCamera() { return mainCamera_.get(); }

private:
    CameraManager() = default;
    ~CameraManager() = default;
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

private:
    std::unique_ptr<Camera> mainCamera_;
};