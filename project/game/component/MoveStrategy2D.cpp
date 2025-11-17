#include "MoveStrategy2D.h"
#include "Player.h"
#include "InputManager.h"
#include "engine/utility/math/Math.h"

Vector3 MoveStrategy2D::CalculateVelocity(Player* player) {
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    const float moveSpeed = 6.0f; // 秒速

    InputManager* inputManager = player->GetInputManager();
    if (!inputManager) { return move; }

    // --- 2D の移動 (カメラを無視し、X軸 (左右) のみ) ---
    if (inputManager->IsKeyPressed(DIK_A)) {
        move.x = -moveSpeed;
    }
    if (inputManager->IsKeyPressed(DIK_D)) {
        move.x = moveSpeed;
    }
    // Z軸 (奥/手前) は 0 のまま
    return move;
}