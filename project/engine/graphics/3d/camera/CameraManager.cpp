#include "CameraManager.h"



CameraManager* CameraManager::GetInstance() {
    static CameraManager instance;
    return &instance;
}

void CameraManager::Initialize() {
    mainCamera_ = std::make_unique<Camera>();
    mainCamera_->Initialize();
}

void CameraManager::Update(float deltaTime) {
    if (mainCamera_) {
        mainCamera_->Update(deltaTime);
    }
}

void CameraManager::SetInputManager(InputManager* inputManager) {
    if (mainCamera_) {
        mainCamera_->SetInputManager(inputManager);
    }
}
