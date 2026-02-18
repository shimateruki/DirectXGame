#include "MoveStrategy3D.h"
#include "Player.h"         
#include "InputManager.h"  
#include "CameraManager.h"  
#include "engine/utility/math/Math.h"

Vector3 MoveStrategy3D::CalculateVelocity(Player* player) {
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    float moveSpeed = player->GetMoveSpeed(); 

    InputManager* inputManager = player->GetInputManager(); 
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    if (!camera || !inputManager) { return move; }

    if (player->IsLockingOn()) { 
        // --- (A) ロックオン中の移動 (ストラフ) ---
        Vector3 playerForward = { 0, 0, 1 };
        Vector3 playerRight = { 1, 0, 0 };

        Matrix4x4 rotateMat = math.MakeRotateYMatrix(player->GetRotation().y); 
        playerForward = math.TransformNormal(playerForward, rotateMat);
        playerRight = math.TransformNormal(playerRight, rotateMat);

        if (inputManager->IsKeyPressed(DIK_W)) { move += playerForward; }
        if (inputManager->IsKeyPressed(DIK_S)) { move += (playerForward * -1.0f); }
        if (inputManager->IsKeyPressed(DIK_A)) { move += (playerRight * -1.0f); }
        if (inputManager->IsKeyPressed(DIK_D)) { move += playerRight; }

    } else {
        // --- (B) 通常時の移動 (カメラ基準) ---
        Vector3 cameraForward = camera->GetTargetPoint() - camera->GetEye();
        Vector3 cameraRight = math.Cross({ 0.0f, 1.0f, 0.0f }, cameraForward);
        cameraForward.y = 0.0f;
        cameraRight.y = 0.0f;

        if (math.Length(cameraForward) > 0.001f) { cameraForward = math.Normalize(cameraForward); }
        if (math.Length(cameraRight) > 0.001f) { cameraRight = math.Normalize(cameraRight); }

        if (inputManager->IsKeyPressed(DIK_W)) { move += cameraForward; }
        if (inputManager->IsKeyPressed(DIK_S)) { move += (cameraForward * -1.0f); }
        if (inputManager->IsKeyPressed(DIK_A)) { move += (cameraRight * -1.0f); }
        if (inputManager->IsKeyPressed(DIK_D)) { move += cameraRight; }
    }


    // 速度を適用
    if (math.Length(move) > 0.001f) {
        move = math.Normalize(move) * moveSpeed;
    }
    return move;
}