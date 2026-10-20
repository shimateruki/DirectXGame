#include "CameraManager.h"
#include <cmath>

CameraManager* CameraManager::GetInstance() {
    static CameraManager instance;
    return &instance;
}

// メインカメラを作り直します。入力先の設定はこの後に行います。
void CameraManager::Initialize() {
    mainCamera_ = std::make_unique<Camera>();
    mainCamera_->Initialize();
    activeCamera_ = nullptr;
    presentationPreviousActiveCamera_ = nullptr;
    mainCameraPresentationActive_ = false;
}

bool CameraManager::BeginMainCameraPresentation() {
    if (!mainCamera_ || mainCameraPresentationActive_) {
        return false;
    }

    presentationPreviousActiveCamera_ = activeCamera_;
    presentationPreviousInputEnabled_ = mainCamera_->IsInputEnabled();
    mainCameraPresentationActive_ = true;
    mainCamera_->SetInputEnabled(false);
    mainCamera_->SetFollowTarget(nullptr);
    mainCamera_->SetLockOnTarget(nullptr);
    mainCamera_->SetFollowMode(Camera::FollowMode::kFixedPoint);
    activeCamera_ = mainCamera_.get();
    return true;
}

void CameraManager::PrepareMainCameraReturn(engine::Object3d* followTarget, const engine::Vector3& eye,
                                            const engine::Vector3& target) {
    if (!mainCameraPresentationActive_ || !mainCamera_) {
        return;
    }

    // 視線方向だけを回転へ反映し、通常カメラの追従距離は変更しない。
    const engine::Vector3 direction = target - eye;
    const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    const engine::Vector3 rotation = { std::atan2(-direction.y, horizontal),
                                       std::atan2(direction.x, direction.z), 0.0f };
    mainCamera_->SetEye(eye);
    mainCamera_->SetTarget(target);
    mainCamera_->SetRotation(rotation);
    mainCamera_->SetFollowTarget(followTarget);
    mainCamera_->SetFollowMode(Camera::FollowMode::kAimable);
}

void CameraManager::SnapMainCameraReturn(float distance, float height, float pitch) {
    if (mainCameraPresentationActive_ && mainCamera_) {
        mainCamera_->SnapToThirdPerson(distance, height, pitch);
    }
}

void CameraManager::EndMainCameraPresentation() {
    if (!mainCameraPresentationActive_) {
        return;
    }
    if (mainCamera_) {
        mainCamera_->SetInputEnabled(presentationPreviousInputEnabled_);
    }
    activeCamera_ = presentationPreviousActiveCamera_;
    presentationPreviousActiveCamera_ = nullptr;
    mainCameraPresentationActive_ = false;
}

void CameraManager::Update(float deltaTime) {
    if (mainCamera_) {
        mainCamera_->Update(deltaTime);
    }
}

void CameraManager::SetInputManager(engine::InputManager* inputManager) {
    if (mainCamera_) {
        mainCamera_->SetInputManager(inputManager);
    }
}

// 演出は現在のアクティブカメラへ送ります。カメラがなければ要求を破棄します。
void CameraManager::PlayShake(float duration, float amplitude, float frequency, const engine::Vector3& axisWeight) {
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
