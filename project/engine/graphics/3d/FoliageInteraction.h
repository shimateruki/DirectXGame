#pragma once
#include <array>
#include <cmath>

// 草を倒す現在位置と短い通過履歴。物理判定には影響しない。
namespace FoliageInteraction {
inline std::array<float, 32> samples{};
inline float sampleTimer = 0.0f;

/// <summary>草の倒れ状態と通過履歴を消去する。</summary>
inline void Reset() { samples.fill(0.0f); sampleTimer = 0.0f; }

/// <summary>現在位置を記録し、古い草の倒れ跡を時間経過で減衰させる。</summary>
inline void Update(float x, float y, float z, float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) return;
    const float dx = x - samples[0], dz = z - samples[2];
    const float distanceSquared = dx * dx + dz * dz;
    if (distanceSquared > 64.0f) Reset(); // ワープ時は途中に跡を作らない。
    const float decay = std::exp(-3.5f * dt);
    sampleTimer += dt;
    for (int i = 1; i < 8; ++i) samples[i * 4 + 3] *= decay;
    if (samples[3] > 0.0f && distanceSquared > 0.0001f && sampleTimer >= 0.06f) {
        sampleTimer = 0.0f;
        for (int i = 7; i > 1; --i)
            for (int j = 0; j < 4; ++j) samples[i * 4 + j] = samples[(i - 1) * 4 + j];
        for (int j = 0; j < 4; ++j) samples[4 + j] = samples[j];
    }
    samples[0] = x; samples[1] = y; samples[2] = z; samples[3] = 1.0f;
}
}
