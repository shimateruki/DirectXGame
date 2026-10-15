#include "MoveStrategy3D.h"
#include "Player.h"
#include "InputManager.h"
#include "CameraManager.h"
#include "engine/utility/math/Math.h"
#include <cmath>

engine::Vector3 MoveStrategy3D::CalculateVelocity(Player* player) {
    engine::Vector3 move = {0.0f, 0.0f, 0.0f};
    float moveSpeed = player->GetMoveSpeed();

    InputManager* input = player->GetInputManager();
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    if (!camera || !input) {
        return move;
    }

    // --- 方向ベクトルの準備 ---
    engine::Vector3 forward, right;

    {
        // ロックオンしても入力方向の基準は変えず、自由に足場を移動できます。
        forward = camera->GetTargetPoint() - camera->GetEye();
        forward.y = 0.0f;
        right = math.Cross({0.0f, 1.0f, 0.0f}, forward);

        if (math.Length(forward) > 0.001f) {
            forward = math.Normalize(forward);
        }
        if (math.Length(right) > 0.001f) {
            right = math.Normalize(right);
        }
    }

    // KeyConfig対応：アクション名で入力をチェック
    if (input->IsActionPressed("Forward")) {
        move += forward;
    }
    if (input->IsActionPressed("Backward")) {
        move += (forward * -1.0f);
    }
    if (input->IsActionPressed("Left")) {
        move += (right * -1.0f);
    }
    if (input->IsActionPressed("Right")) {
        move += right;
    }

    // アナログ入力を優先し、倒し具合を速度に反映します。方向キーの操作は従来通り残します。
    const engine::Vector2 stick = input->GetLeftStick();
    const float stickLength = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    if (stickLength > 0.001f) {
        return (right * stick.x + forward * stick.y) * moveSpeed;
    }

    // 速度を適用
    if (math.Length(move) > 0.001f) {
        move = math.Normalize(move) * moveSpeed;
    }
    return move;
}
