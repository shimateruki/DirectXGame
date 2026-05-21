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

    Vector3 forward = camera->GetTargetPoint() - camera->GetEye();
    forward.y = 0.0f;

    if (math.Length(forward) > 0.001f) {
        forward = math.Normalize(forward);
    }
    else {
        forward = { 0.0f, 0.0f, 1.0f };
    }

    Vector3 right = math.Cross({ 0.0f, 1.0f, 0.0f }, forward);
    if (math.Length(right) > 0.001f) {
        right = math.Normalize(right);
    }

    // =========================================================
    // ★ 修正ポイント：スティックとキーボードを完全に独立させる
    // =========================================================
    // 1. スティックの入力を取得
    Vector2 stick = input->GetLeftStick();
    float stickX = stick.x;
    float stickZ = stick.y;

    // 2. キーボードの入力を取得 (別の変数に分ける)
    float keyX = 0.0f;
    float keyZ = 0.0f;
    if (input->IsActionPressed("Forward")) { keyZ += 1.0f; }
    if (input->IsActionPressed("Backward")) { keyZ -= 1.0f; }
    if (input->IsActionPressed("Right")) { keyX += 1.0f; }
    if (input->IsActionPressed("Left")) { keyX -= 1.0f; }

    // 3. スティックとキーボード、どちらか「入力が強い方」を採用する（干渉を完全回避）
    float inputX = (abs(stickX) > abs(keyX)) ? stickX : keyX;
    float inputZ = (abs(stickZ) > abs(keyZ)) ? stickZ : keyZ;

    // ベクトルに適用
    move += forward * inputZ;
    move += right * inputX;

    if (math.Length(move) > 0.001f) {
        move = math.Normalize(move) * moveSpeed;
    }

    return move;
}