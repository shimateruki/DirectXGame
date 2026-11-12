#pragma once
#include <cmath>
#include <numbers>
#include <algorithm>

// 担当：0～1の進行割合を、演出用の加速・減速曲線へ変換する。
// 関連：Camera / EffectObject3D / GhostRecorderなど（補間に利用）
class Easing {
public:
    // 引数tは補間の進行割合。Inは開始側、Outは終了側、InOutは両側を緩急づける。
    // 1. Linear (等速)
    // 入力範囲の制限は呼び出し側で行う。Back・Elastic は出力が 0～1 を越える場合がある。
    static float Linear(float t);

    /// <summary>三次のSmoothStep曲線。入力範囲は呼び出し側で決める。</summary>
    static float SmoothStep(float t);

    // 01付きは入力を0～1へ制限する。Backの出力の行き過ぎは保持する。
    static float SmoothStep01(float t);
    // 二次曲線で終了側を減速する。入力tは0～1へ制限。
    static float OutQuad01(float t);
    // 三次曲線で終了側を減速する。入力tは0～1へ制限。
    static float OutCubic01(float t);
    // 三次曲線で開始側を加速し、終了側を減速する。入力tは0～1へ制限。
    static float InOutCubic01(float t);
    // 行き過ぎを伴う曲線で終了側を減速する。入力tは0～1へ制限。
    static float OutBack01(float t);
    // 五次曲線で終了側を減速する。入力tは0～1へ制限。
    static float OutQuint01(float t);

    // 2. Sine (サイン波：滑らか)
    static float InSine(float t);

    // 正弦曲線で終了側を減速する。
    static float OutSine(float t);

    // 正弦曲線で開始側を加速し、終了側を減速する。
    static float InOutSine(float t);

    // 3. Quad (2乗：基本)
    static float InQuad(float t);

    // 二次曲線で終了側を減速する。
    static float OutQuad(float t);

    // 二次曲線で開始側を加速し、終了側を減速する。
    static float InOutQuad(float t);

    // 4. Cubic (3乗：強め)
    static float InCubic(float t);

    // 三次曲線で終了側を減速する。
    static float OutCubic(float t);

    // 三次曲線で開始側を加速し、終了側を減速する。
    static float InOutCubic(float t);

    // 5. Quart (4乗：さらに強め)
    static float InQuart(float t);

    // 四次曲線で終了側を減速する。
    static float OutQuart(float t);

    // 四次曲線で開始側を加速し、終了側を減速する。
    static float InOutQuart(float t);

    // 6. Quint (5乗：急加速)
    static float InQuint(float t);

    // 五次曲線で終了側を減速する。
    static float OutQuint(float t);

    // 五次曲線で開始側を加速し、終了側を減速する。
    static float InOutQuint(float t);

    // 7. Expo (指数：シュッ！と動く)
    static float InExpo(float t);

    // 指数曲線で終了側を減速する。
    static float OutExpo(float t);

    // 指数曲線で開始側を加速し、終了側を減速する。
    static float InOutExpo(float t);

    // 8. Circ (円：ゆっくり始まって急に終わる)
    static float InCirc(float t);

    // 円弧で終了側を減速する。
    static float OutCirc(float t);

    // 円弧で開始側を加速し、終了側を減速する。
    static float InOutCirc(float t);

    // 9. Back (少し戻る：予備動作や行き過ぎ演出)
    static float InBack(float t);

    // 行き過ぎを伴う曲線で終了側を減速する。
    static float OutBack(float t);

    // 行き過ぎを伴う曲線で開始側を加速し、終了側を減速する。
    static float InOutBack(float t);

    // 10. Elastic (ゴム：ビヨヨンと震える)
    static float InElastic(float t);

    // 弾性振動で終了側を減速する。
    static float OutElastic(float t);

    // 弾性振動で開始側を加速し、終了側を減速する。
    static float InOutElastic(float t);

    // 11. Bounce (ボールの跳ね返り)
    static float InBounce(float t);

    // 跳ね返りで終了側を減速する。
    static float OutBounce(float t);

    // 跳ね返りで開始側を加速し、終了側を減速する。
    static float InOutBounce(float t);
};
