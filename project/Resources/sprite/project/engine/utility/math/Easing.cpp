#include "Easing.h"

// 数学定数の定義
const float PI = std::numbers::pi_v<float>;

// --- 1. Linear ---
float Easing::Linear(float t) {
    return t;
}

// --- 2. Sine ---
float Easing::InSine(float t) {
    return 1.0f - std::cos((t * PI) / 2.0f);
}
float Easing::OutSine(float t) {
    return std::sin((t * PI) / 2.0f);
}
float Easing::InOutSine(float t) {
    return -(std::cos(PI * t) - 1.0f) / 2.0f;
}

// --- 3. Quad ---
float Easing::InQuad(float t) {
    return t * t;
}
float Easing::OutQuad(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}
float Easing::InOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

// --- 4. Cubic ---
float Easing::InCubic(float t) {
    return t * t * t;
}
float Easing::OutCubic(float t) {
    return 1.0f - std::pow(1.0f - t, 3.0f);
}
float Easing::InOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// --- 5. Quart ---
float Easing::InQuart(float t) {
    return t * t * t * t;
}
float Easing::OutQuart(float t) {
    return 1.0f - std::pow(1.0f - t, 4.0f);
}
float Easing::InOutQuart(float t) {
    return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}

// --- 6. Quint ---
float Easing::InQuint(float t) {
    return t * t * t * t * t;
}
float Easing::OutQuint(float t) {
    return 1.0f - std::pow(1.0f - t, 5.0f);
}
float Easing::InOutQuint(float t) {
    return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

// --- 7. Expo ---
float Easing::InExpo(float t) {
    return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
}
float Easing::OutExpo(float t) {
    return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}
float Easing::InOutExpo(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    if ((t *= 2.0f) < 1.0f) return std::pow(2.0f, 10.0f * (t - 1.0f)) / 2.0f;
    return (2.0f - std::pow(2.0f, -10.0f * (t - 1.0f))) / 2.0f;
}

// --- 8. Circ ---
float Easing::InCirc(float t) {
    return 1.0f - std::sqrt(1.0f - std::pow(t, 2.0f));
}
float Easing::OutCirc(float t) {
    return std::sqrt(1.0f - std::pow(t - 1.0f, 2.0f));
}
float Easing::InOutCirc(float t) {
    return t < 0.5f
        ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f
        : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}

// --- 9. Back ---
float Easing::InBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}
float Easing::OutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}
float Easing::InOutBack(float t) {
    const float c1 = 1.70158f;
    const float c2 = c1 * 1.525f;
    return t < 0.5f
        ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
        : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (2.0f * t - 2.0f) + c2) + 2.0f) / 2.0f;
}

// --- 10. Elastic ---
float Easing::InElastic(float t) {
    const float c4 = (2.0f * PI) / 3.0f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}
float Easing::OutElastic(float t) {
    const float c4 = (2.0f * PI) / 3.0f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
float Easing::InOutElastic(float t) {
    const float c5 = (2.0f * PI) / 4.5f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : t < 0.5f
        ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
        : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
}

// --- 11. Bounce ---
float Easing::OutBounce(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        return n1 * (t -= 1.5f / d1) * t + 0.75f;
    } else if (t < 2.5f / d1) {
        return n1 * (t -= 2.25f / d1) * t + 0.9375f;
    } else {
        return n1 * (t -= 2.625f / d1) * t + 0.984375f;
    }
}
float Easing::InBounce(float t) {
    return 1.0f - OutBounce(1.0f - t);
}
float Easing::InOutBounce(float t) {
    return t < 0.5f
        ? (1.0f - OutBounce(1.0f - 2.0f * t)) / 2.0f
        : (1.0f + OutBounce(2.0f * t - 1.0f)) / 2.0f;
}