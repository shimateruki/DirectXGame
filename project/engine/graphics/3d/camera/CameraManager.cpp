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

void CameraManager::PlayShake(float duration, float amplitude, float frequency, const Vector3& axisWeight) {
    if (Camera* camera = GetActiveCamera()) {
        camera->StartShake(duration, amplitude, frequency, axisWeight);
    }
}

void CameraManager::PlayFovPulse(float duration, float amountRadians, float attackRatio) {
    if (Camera* camera = GetActiveCamera()) {
        camera->StartFovPulse(duration, amountRadians, attackRatio);
    }
}

void CameraManager::ClearPresentationLayers() {
    if (Camera* camera = GetActiveCamera()) {
        camera->ClearPresentationLayers();
    }
}
