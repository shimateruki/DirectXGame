#include "MoveStrategy3D.h"
#include "Player.h"
#include "InputManager.h"
#include "CameraManager.h"
#include "engine/utility/math/Math.h"
#include <cmath>

engine::Vector3 MoveStrategy3D::CalculateVelocity(Player* player) {
    engine::Vector3 move = engine::Vector3::ZERO;
    float moveSpeed = player->GetMoveSpeed();

    engine::InputManager* input = player->GetInputManager();
    engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    // 視点と入力の両方が揃うまでは、移動の基準を作れないため速度ゼロを返す。
    if (!camera || !input) {
        return move;
    }

    // --- 方向ベクトルの準備 ---
    engine::Vector3 forward, right;

    {
        // ロックオンしても入力方向の基準は変えず、自由に足場を移動できます。
        forward = camera->GetTargetPoint() - camera->GetEye();
        // 視点の上下角を移動速度へ入れないよう、前方を地面の平面へ投影する。
        forward.y = 0.0f;
        // ワールドの上方向と水平前方の外積を取ると、カメラから見た右向きが求まる。
        right = math_.Cross({0.0f, 1.0f, 0.0f}, forward);

        if (math_.Length(forward) > 0.001f) {
            forward = math_.Normalize(forward);
        }

        if (math_.Length(right) > 0.001f) {
            right = math_.Normalize(right);
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

    // スティックのXを右、Yを前へ配分する。正規化せず小さく倒した分だけ速度を落とせる。
    if (stickLength > 0.001f) {
        return (right * stick.x + forward * stick.y) * moveSpeed;
    }

    // 速度を適用
    // デジタル入力の合成結果は正規化し、斜め入力で速度が上がらないようにする。
    if (math_.Length(move) > 0.001f) {
        move = math_.Normalize(move) * moveSpeed;
    }

    return move;
}
