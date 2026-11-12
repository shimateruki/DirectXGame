#pragma once
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cmath>

// 攻撃中だけ通常移動へ渡す制限。重力・ジャンプ・旋回の操作権は奪いません。
struct PlayerAttackMovement {
    bool enabled = false;
    bool overrideHorizontalVelocity = false; // 能力が方向を制御する間だけ水平速度を置き換える。
    engine::Vector3 horizontalVelocity{}; // ワールド座標・毎秒。Yは使用しない。
    float duration = 0.38f; // 攻撃開始から制限を掛ける秒数。
    float groundSpeedScale = 0.45f;
    float airSpeedScale = 0.85f;
    float airResponse = 6.0f; // 空中の方向変更・減速への追従率（毎秒）。

    float maxFallSpeedOverride = 0.0f; // 能力中の落下速度上限。0はプレイヤーの設定を使う。

    bool Validate() const {
        // 保存前に非有限値と範囲外を拒否し、移動の逆転や補間の発散を防ぐ。
        return std::isfinite(horizontalVelocity.x) && std::isfinite(horizontalVelocity.z) && std::isfinite(duration) && duration >= 0 && duration <= 30 &&
            std::isfinite(groundSpeedScale) && groundSpeedScale >= 0 && groundSpeedScale <= 1 &&
            std::isfinite(airSpeedScale) && airSpeedScale >= 0 && airSpeedScale <= 1 &&
            std::isfinite(airResponse) && airResponse >= 0.1f && airResponse <= 60;
    }

    engine::Vector3 Apply(const engine::Vector3& current, const engine::Vector3& desired, bool grounded, float deltaTime) const {
        // 空中では瞬時に速度を奪わず入力へ近づけます。鉛直速度は常に呼び出し元の値を保ちます。
        auto result = current;
        if (enabled && overrideHorizontalVelocity) {
            result.x = horizontalVelocity.x;
            result.z = horizontalVelocity.z;
            return result;
        }
        const float scale = enabled ? (grounded ? groundSpeedScale : airSpeedScale) : 1.0f;
        const float blend = enabled && !grounded ? 1.0f - std::exp(-airResponse * (std::max)(0.0f, deltaTime)) : 1.0f;
        result.x += (desired.x * scale - result.x) * blend;
        result.z += (desired.z * scale - result.z) * blend;
        return result;
    }
};
