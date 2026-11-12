#pragma once

// Playerの未設定時の値と、管理ステータスの初期値を同じ基準に揃える。
// 実際に調整されたパラメーターやJSONの値を上書きするものではない。
namespace PlayerDefaults {
/// <summary>プレイヤーのジャンプ初速の既定値。</summary>
inline constexpr float kJumpPower = 24.0f;

/// <summary>プレイヤーの移動速度の既定値。</summary>
inline constexpr float kMoveSpeed = 27.7f;
} // namespace PlayerDefaults
