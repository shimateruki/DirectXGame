#pragma once

namespace EnemyAttackPreviewConstants {
// AI確認はこの秒刻みで進め、表示フレームレートによる結果の差を抑える。
constexpr float kFixedPreviewStep = 1.0f / 60.0f;
// ループ再生の長さは秒、対象との距離上限はワールド単位、πはラジアン用。
constexpr float kMinLoopDuration = 0.5f;
constexpr float kMaxLoopDuration = 12.0f;
constexpr float kDefaultTargetDistanceLimit = 18.0f;
constexpr float kPi = std::numbers::pi_v<float>;

} // namespace EnemyAttackPreviewConstants
#include <numbers>
