#pragma once
#include <cmath>
#include <numbers>
#include <algorithm>

class Easing {
public:
    // 1. Linear (等速)
    static float Linear(float t);

    // 2. Sine (サイン波：滑らか)
    static float InSine(float t);
    static float OutSine(float t);
    static float InOutSine(float t);

    // 3. Quad (2乗：基本)
    static float InQuad(float t);
    static float OutQuad(float t);
    static float InOutQuad(float t);

    // 4. Cubic (3乗：強め)
    static float InCubic(float t);
    static float OutCubic(float t);
    static float InOutCubic(float t);

    // 5. Quart (4乗：さらに強め)
    static float InQuart(float t);
    static float OutQuart(float t);
    static float InOutQuart(float t);

    // 6. Quint (5乗：急加速)
    static float InQuint(float t);
    static float OutQuint(float t);
    static float InOutQuint(float t);

    // Expo: 指数関数による急加速・急減速。
    static float InExpo(float t);
    static float OutExpo(float t);
    static float InOutExpo(float t);

    // 8. Circ (円：ゆっくり始まって急に終わる)
    static float InCirc(float t);
    static float OutCirc(float t);
    static float InOutCirc(float t);

    // 9. Back (少し戻る：予備動作や行き過ぎ演出)
    static float InBack(float t);
    static float OutBack(float t);
    static float InOutBack(float t);

    // 10. Elastic (ゴム：ビヨヨンと震える)
    static float InElastic(float t);
    static float OutElastic(float t);
    static float InOutElastic(float t);

    // 11. Bounce (ボールの跳ね返り)
    static float InBounce(float t);
    static float OutBounce(float t);
    static float InOutBounce(float t);
};
