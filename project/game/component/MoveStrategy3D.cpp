#include "MoveStrategy3D.h"
#include "Player.h"         
#include "InputManager.h"  
#include "CameraManager.h"  
#include "engine/utility/math/Math.h"

Vector3 MoveStrategy3D::CalculateVelocity(Player* player) {
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    float moveSpeed = player->GetMoveSpeed();

    InputManager* input = player->GetInputManager();
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    if (!camera || !input) { return move; }

    // --- 方向ベクトルの準備 ---
    Vector3 forward, right;

    if (player->IsLockingOn()) {
        // --- (A) ロックオン中の移動 (自キャラ基準) ---
        forward = { 0, 0, 1 };
        right = { 1, 0, 0 };
        Matrix4x4 rotateMat = math.MakeRotateYMatrix(player->GetRotation().y);
        forward = math.TransformNormal(forward, rotateMat);
        right = math.TransformNormal(right, rotateMat);
    }
    else {
        // --- (B) 通常時の移動 (カメラ基準) ---
        forward = camera->GetTargetPoint() - camera->GetEye();
        forward.y = 0.0f;
        right = math.Cross({ 0.0f, 1.0f, 0.0f }, forward);

        if (math.Length(forward) > 0.001f) { forward = math.Normalize(forward); }
        if (math.Length(right) > 0.001f) { right = math.Normalize(right); }
    }

    // --- ★ KeyConfig対応：アクション名で入力をチェック！ ---
    if (input->IsActionPressed("Forward")) { move += forward; }
    if (input->IsActionPressed("Backward")) { move += (forward * -1.0f); }
    if (input->IsActionPressed("Left")) { move += (right * -1.0f); }
    if (input->IsActionPressed("Right")) { move += right; }

    // 速度を適用
    if (math.Length(move) > 0.001f) {
        move = math.Normalize(move) * moveSpeed;
    }
    return move;
}