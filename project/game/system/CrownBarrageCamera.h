#pragma once

#include "game/system/CrownFinaleTuning.h"
#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

// 第二形態の封印失敗演出で、実戦と敵攻撃プレビューが同じカットを使います。
struct CrownBarrageCameraPose {
    engine::Vector3 eye{};
    engine::Vector3 target{};
    float fov = 0.7f;
};

inline CrownBarrageCameraPose EvaluateCrownBarrageCamera(float seconds, const engine::Vector3& boss,
                                                          const engine::Vector3& strike, const CrownFinaleTuning& tuning) {
    const engine::Vector3 horizontal{strike.x - boss.x, 0.0f, strike.z - boss.z};
    const float distance = std::sqrt(horizontal.x * horizontal.x + horizontal.z * horizontal.z);
    const engine::Vector3 forward = distance > 0.01f
                                ? engine::Vector3{horizontal.x / distance, 0.0f, horizontal.z / distance}
                                : engine::Vector3{0.0f, 0.0f, 1.0f};
    const engine::Vector3 right{forward.z, 0.0f, -forward.x};
    const auto offset = [&](const engine::Vector3& origin, float along, float side, float height) {
        return origin + forward * (along * tuning.sealBarrageCameraDistance) +
               right * (side * tuning.sealBarrageCameraDistance) +
               engine::Vector3{0.0f, height * tuning.sealBarrageCameraHeight, 0.0f};
    };
    const CrownBarrageCameraPose shots[] = {
        // プレイヤー越しに失敗の舞台と頭上の術式を映す。
        {offset(strike, 11.0f, 7.0f, 4.4f), boss + engine::Vector3{0.0f, 9.2f, 0.0f}, 0.83f},
        // 低い位置から術式の裏面と大槍のシルエットを見上げる。
        {offset(boss, 38.0f, 25.0f, 4.0f), boss + engine::Vector3{0.0f, 8.5f, 0.0f}, 0.83f},
        // 大槍を画面中央に保ちながら落下へつなぐ。
        {offset(strike, 10.0f, 7.0f, 3.0f), strike + engine::Vector3{0.0f, 7.4f, 0.0f}, 0.62f},
        // 着弾の瞬間は低い位置で止め、槍・閃光・地表から跳ねる雷撃を重ねて見せる。
        {offset(strike, 8.0f, 6.0f, 2.4f), strike + engine::Vector3{0.0f, 3.6f, 0.0f}, 0.87f},
        // 放電が全域へ満ちた後に引き、闘技場全体の被害と消滅を見せる。
        {offset(strike, 28.0f, 18.0f, 12.0f), boss + engine::Vector3{0.0f, 2.5f, 0.0f}, 0.90f},
    };
    const float boundaries[] = {0.0f, 0.56f, 1.18f, 2.62f, 3.65f};
    int shot = 0;
    for (int index = 1; index < 5; ++index)
        if (seconds >= boundaries[index])
            shot = index;
    if (shot == 0)
        return shots[0];
    const float blend = std::clamp((seconds - boundaries[shot]) / 0.16f, 0.0f, 1.0f);
    const float eased = blend * blend * (3.0f - 2.0f * blend);
    return {shots[shot - 1].eye + (shots[shot].eye - shots[shot - 1].eye) * eased,
            shots[shot - 1].target + (shots[shot].target - shots[shot - 1].target) * eased,
            shots[shot - 1].fov + (shots[shot].fov - shots[shot - 1].fov) * eased};
}
