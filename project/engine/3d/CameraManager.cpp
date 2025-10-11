#include "engine/3d/CameraManager.h"

// 静的メンバ変数の実体
// static CameraManager* instance = nullptr; // 仮

CameraManager* CameraManager::GetInstance() {
    static CameraManager instance;
    return &instance;
}

void CameraManager::Initialize() {
    mainCamera_ = std::make_unique<Camera>();
    mainCamera_->Initialize();
}

void CameraManager::Update() {
    if (mainCamera_) {
        mainCamera_->Update();
    }
}

void CameraManager::SetInputManager(InputManager* inputManager) {
    if (mainCamera_) {
        mainCamera_->SetInputManager(inputManager);
    }
}