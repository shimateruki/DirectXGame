#pragma once

#include <algorithm>
#include <cmath>

namespace engine {

// 担当：HPの割合と、遅れて追従するゲージ表示の状態を計算する。
// 関連：Spriteなどの呼出側が結果を表示。Playerや描画資源は参照しない。
// 主な入口：Reset / Update / Rate
struct HealthBarState {
    // 被弾判定の前回値と遅延ゲージの残量率を別々に保持する。
    float previousHp = 0.0f, previousRate = 0.0f;
    float damagePulseTimer = 0.0f, hurtIconTimer = 0.0f, damageHoldTimer = 0.0f;
    float delayedRate = 0.0f, animationTimer = 0.0f;

    // 最大HPは1以上として除算し、表示用の残量率を0〜1へ収める。
    static float Rate(float hp, float maxHp) {
        return std::clamp(hp / (std::max)(1.0f, maxHp), 0.0f, 1.0f);
    }

    // バインド先が変わったときは演出時計を消し、現在HPから遅延ゲージも揃える。
    void Reset(float hp, float maxHp) {
        *this = {};
        // 非表示中も前回値を更新し、再表示した瞬間に過去の減少を被弾として検出しない。
        previousHp = hp;
        previousRate = delayedRate = Rate(hp, maxHp);
    }

    // deltaTimeは秒。visibleは被弾検出、animatedはHP実値での検出と被弾演出の有無に使う。
    void Update(float hp, float maxHp, float deltaTime, bool visible, bool animated) {
        const float frameDeltaTime = (std::max)(0.0f, deltaTime);
        const float rate = Rate(hp, maxHp);
        // 既存の本編はHP値、簡素表示は残量率で被弾を検出する。
        const bool damaged = visible && (animated ? hp < previousHp - 0.01f
                                                    : rate < previousRate - 0.001f);
        if (damaged) {
            damageHoldTimer = 0.18f;
            if (animated) {
                damagePulseTimer = 0.42f;
                hurtIconTimer = 0.48f;
                delayedRate = (std::max)(delayedRate, Rate(previousHp, maxHp));
            }
        }
        previousHp = hp;
        previousRate = rate;
        damagePulseTimer = (std::max)(0.0f, damagePulseTimer - frameDeltaTime);
        hurtIconTimer = (std::max)(0.0f, hurtIconTimer - frameDeltaTime);
        if (!visible && animated) {
            delayedRate = 0.0f;
            damageHoldTimer = 0.0f;
        // 回復は即座に追従し、減少時だけ待機してから遅延ゲージを下げる。
        } else if (rate >= delayedRate) {
            delayedRate = rate;
            damageHoldTimer = 0.0f;
        } else if (damageHoldTimer > 0.0f) {
            damageHoldTimer = (std::max)(0.0f, damageHoldTimer - frameDeltaTime);
        } else {
            delayedRate = (std::max)(rate, delayedRate - frameDeltaTime * 0.62f);
        }
        animationTimer = std::fmod(animationTimer + frameDeltaTime, 1000.0f);
    }
};

} // namespace engine
