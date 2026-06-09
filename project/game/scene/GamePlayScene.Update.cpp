#define NOMINMAX
#include "GamePlayScene.h"

#include "BulletManager.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "Fade.h"
#include "GPUParticleManager.h"
#include "GameDataManager.h"
#include "InputManager.h"
#include "LightEditor.h"
#include "LockOnSystem.h"
#include "MeshEffectManager.h"
#include "ObjectManager.h"
#include "ParticleSystem.h"
#include "PostEffect.h"
#include "ProfilerManager.h"
#include "SceneManager.h"
#include "StageManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>

void GamePlayScene::Update(float deltaTime) {
    if (HandleGoalClear(deltaTime)) {
        return;
    }

    UpdatePostEffectState(deltaTime);
    UpdateLockOnAndCamera(deltaTime);
    UpdateSceneSystems(deltaTime);
    UpdateUI(deltaTime);
    UpdateEffectDebugShortcuts();

    if (animatedCube_) {
        animatedCube_->Update(deltaTime);
    }
}

bool GamePlayScene::HandleGoalClear(float& deltaTime) {
    if (!isGoal_) {
        return false;
    }

    int currentStage = StageManager::GetInstance()->GetCurrentStageIndex();
    GameDataManager::GetInstance()->MarkStageCleared(currentStage);

    for (int i = 0; i < 3; i++) {
        if (sessionStarCoins_[i]) {
            GameDataManager::GetInstance()->MarkStarCoinCollected(currentStage, i);
        }
    }

    deltaTime = 0.0f;

    if (inputManager_->IsKeyTriggered(DIK_SPACE)) {
        SceneManager::GetInstance()->ChangeScene("SELECT");
        return true;
    }

    return false;
}

void GamePlayScene::UpdatePostEffectState(float deltaTime) {
    PostEffect::GetInstance()->Update(deltaTime);

    Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
    if (activeCamera) {
        PostEffect::GetInstance()->GetParams()->projectionInverse = Math::Inverse(activeCamera->GetProjectionMatrix());
    }
}

void GamePlayScene::UpdateLockOnAndCamera(float deltaTime) {
    LightEditor::GetInstance()->Update();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        isDrawLockOn_ = false;
        return;
    }

    lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);
    CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());

    Object3d* target = lockOnSystem_->GetTarget();
    if (target && lockOnSystem_->IsLockingOn()) {
        isDrawLockOn_ = true;

        AABB aabb = target->GetAABB();
        Vector3 targetCenter;
        targetCenter.x = (aabb.min.x + aabb.max.x) * 0.5f;
        targetCenter.y = (aabb.min.y + aabb.max.y) * 0.5f;
        targetCenter.z = (aabb.min.z + aabb.max.z) * 0.5f;

        Math math;
        Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
        float w = targetCenter.x * viewProj.m[0][3] + targetCenter.y * viewProj.m[1][3] + targetCenter.z * viewProj.m[2][3] + viewProj.m[3][3];

        if (w > 0.001f) {
            Vector3 ndc;
            ndc.x = (targetCenter.x * viewProj.m[0][0] + targetCenter.y * viewProj.m[1][0] + targetCenter.z * viewProj.m[2][0] + viewProj.m[3][0]) / w;
            ndc.y = (targetCenter.x * viewProj.m[0][1] + targetCenter.y * viewProj.m[1][1] + targetCenter.z * viewProj.m[2][1] + viewProj.m[3][1]) / w;

            float screenWidth = static_cast<float>(::WinApp::kClientWidth);
            float screenHeight = static_cast<float>(::WinApp::kClientHeight);
            float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
            float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;

            lockOnSprite_->SetPosition({ screenX, screenY });

            float objSizeX = aabb.max.x - aabb.min.x;
            float objSizeY = aabb.max.y - aabb.min.y;
            float objSizeZ = aabb.max.z - aabb.min.z;
            float maxObjSize = std::max({ objSizeX, objSizeY, objSizeZ });

            float baseSize = maxObjSize * 25.0f;
            float distanceScale = 20.0f / w;
            float finalSize = baseSize * distanceScale;
            finalSize = std::max(32.0f, std::min(finalSize, 256.0f));

            lockOnSprite_->SetSize({ finalSize, finalSize });
            lockOnSprite_->SetRotation(lockOnSprite_->GetRotation() + 2.0f * deltaTime);
            lockOnSprite_->Update();
        } else {
            isDrawLockOn_ = false;
        }
    } else {
        isDrawLockOn_ = false;
    }

    if (!CameraEditor::GetInstance()->IsEditorMode()) {
        Camera::FollowMode currentMode = camera->GetFollowMode();
        if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
            Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                camera->AddRotation(mouseDelta);
            }
        }
    }
}

void GamePlayScene::UpdateSceneSystems(float deltaTime) {
    ProfilerManager::GetInstance()->SetObjectList(&objectManager_->GetObjects());
    CameraManager::GetInstance()->Update();
    particleSystem_->Update(deltaTime);
    objectManager_->Update(deltaTime);
    GPUParticleManager::GetInstance()->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }

    BulletManager::GetInstance()->Update(deltaTime);
    {
        PROFILE_SCOPE("衝突判定");
        CollisionManager::GetInstance()->Update();
    }
}

void GamePlayScene::UpdateEffectDebugShortcuts() {
    if (inputManager_->IsKeyTriggered(DIK_SPACE)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        particleSystem_->SpawnStarHitEffect(effectPos);
        particleSystem_->SpawnSlashEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_RETURN)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        particleSystem_->SpawnStarHitEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_E)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        particleSystem_->SpawnSlashEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_R)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        MeshEffectManager::GetInstance()->SpawnRingWaveEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_T)) {
        Vector3 effectPos = { 0.0f, 1.5f, 0.0f };
        MeshEffectManager::GetInstance()->SpawnPortalEffect(effectPos, 5.0f);
    }

    if (inputManager_->IsKeyTriggered(DIK_Y)) {
        Vector3 spawnPos = { 0.0f, 0.01f, 10.0f };
        if (player_) {
            Vector3 playerPos = player_->GetWorldPosition();
            Vector3 forward = player_->GetForwardDirection();
            spawnPos = {
                playerPos.x + forward.x * 3.0f,
                playerPos.y,
                playerPos.z + forward.z * 3.0f
            };
        }

        MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/effect_warp_gate_floor.json", spawnPos, { 0.0f, 0.0f, 0.0f });
        MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/effect_warp_gate_pillar.json", spawnPos, { 0.0f, 0.0f, 0.0f });
    }
}
