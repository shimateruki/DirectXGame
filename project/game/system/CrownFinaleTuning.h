#pragma once

#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <cmath>

// 最終戦の調整値。ボスInspectorから保存でき、次の実行でも同じ値を使用します。
struct CrownFinaleTuning {
    // 封印攻撃の当たり判定と予告・回避タイミング。
    float sealRadius = 14.0f;
    float sealHitRadius = 3.0f;
    float sealAimTime = 1.25f;
    float sealDodgeTime = 0.85f;
    float sealRecoveryTime = 1.6f;

    // 第2形態の攻撃圧と、プレイヤーが封印を押し返すまでの蓄積量です。
    float sealArmorDamageRate = 0.25f;
    float sealBreakDamage = 48.0f;
    float sealTargetAssistRadius = 5.0f;
    float sealSatelliteSpread = 5.5f;
    float sealJumpHeight = 7.5f;
    float sealRushSpeed = 16.0f;

    // 第二形態の中央詠唱。クリスタルは同時に壊せ、時間切れで全域攻撃になります。
    float sealCenterOffsetZ = 15.0f;
    float sealCastTime = 18.0f;
    float sealCrystalHp = 30.0f;
    float sealBarrierRadius = 6.5f;
    float sealBarrierHeight = 9.0f;
    float sealBarrageRadius = 41.0f;
    float sealBarrageSkyHeight = 11.2f;
    float sealBarrageSkyRadiusRate = 0.88f;
    float sealBarrageSpearScale = 1.0f;
    float sealBarrageMovieDuration = 3.55f;
    float sealBarrageCameraDistance = 1.0f;
    float sealBarrageCameraHeight = 1.0f;
    float sealBarrageDamage = 12.0f;
    float sealBarrageRecovery = 3.5f;

    // 決着演出の長さ、移動量、カメラ揺れ。
    float movieDuration = 18.0f;
    float escapeHeight = 32.0f;
    float movieShake = 1.0f;
    float movieWingScale = 1.0f;
    float destinationX = 160.0f;

    // 中央装飾の破断・着地・吸い込み。時刻はムービー全体に対する割合です。
    float movieFragmentBreakTime = 0.092f;
    float movieFragmentLandTime = 0.20f;
    float movieFragmentPullTime = 0.35f;
    float movieFragmentAbsorbTime = 0.46f;
    float movieFragmentDistance = 3.0f;
    float movieFragmentScale = 1.0f;
    float movieFragmentFallArc = 2.0f;
    float movieFragmentCameraDistance = 1.0f;


    // 見せ場の開始時刻。欠片を見せてから暴走し、身体・王冠・翼を別々に形成します。
    float movieRageTime = 0.27f;
    float movieBodyTime = 0.51f;
    float movieCrownTime = 0.6f;
    float movieWingsTime = 0.68f;
    float movieEscapeTime = 0.76f;
    float movieArriveTime = 0.93f;

    // 決着中の攻撃ダメージと防御解除時間。
    float lanceDamage = 12.0f;
    float waveDamage = 20.0f;
    float dashDamage = 16.0f;
    float armorOpenTime = 5.0f;

    static constexpr const char* Path = "Resources/json/scenario/crown_finale_tuning.json";

    // 外部設定の NaN や範囲外の値を除き、演出と判定を有効な範囲に保ちます。
    void Sanitize() {
        auto limit = [](float& v, float lo, float hi, float fallback) {
            v = std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
        };
        limit(sealRadius, 8.0f, 22.0f, 14.0f);
        limit(sealHitRadius, 1.5f, 5.0f, 3.0f);
        limit(sealAimTime, 0.8f, 3.0f, 1.25f);
        limit(sealDodgeTime, 0.4f, 2.0f, 0.85f);
        limit(sealRecoveryTime, 1.0f, 4.0f, 1.6f);
        limit(sealArmorDamageRate, 0.05f, 1.0f, 0.25f);
        limit(sealBreakDamage, 10.0f, 200.0f, 48.0f);
        limit(sealTargetAssistRadius, 0.0f, 10.0f, 5.0f);
        limit(sealSatelliteSpread, 3.0f, 10.0f, 5.5f);
        limit(sealJumpHeight, 4.0f, 12.0f, 7.5f);
        limit(sealRushSpeed, 8.0f, 24.0f, 16.0f);
        limit(sealCenterOffsetZ, 5.0f, 24.0f, 15.0f);
        limit(sealCastTime, 8.0f, 40.0f, 18.0f);
        limit(sealCrystalHp, 5.0f, 200.0f, 30.0f);
        limit(sealBarrierRadius, 3.0f, 12.0f, 6.5f);
        limit(sealBarrierHeight, 5.0f, 20.0f, 9.0f);
        limit(sealBarrageRadius, 25.0f, 44.0f, 41.0f);
        limit(sealBarrageSkyHeight, 8.0f, 18.0f, 11.2f);
        limit(sealBarrageSkyRadiusRate, 0.45f, 0.95f, 0.88f);
        limit(sealBarrageSpearScale, 0.65f, 1.45f, 1.0f);
        limit(sealBarrageMovieDuration, 3.1f, 5.0f, 3.55f);
        limit(sealBarrageCameraDistance, 0.7f, 1.5f, 1.0f);
        limit(sealBarrageCameraHeight, 0.7f, 1.5f, 1.0f);
        limit(sealBarrageDamage, 1.0f, 40.0f, 12.0f);
        limit(sealBarrageRecovery, 1.0f, 8.0f, 3.5f);
        limit(movieDuration, 12.0f, 24.0f, 14.0f);
        limit(escapeHeight, 18.0f, 60.0f, 32.0f);
        limit(movieShake, 0.0f, 2.0f, 1.0f);
        limit(movieWingScale, 0.5f, 2.0f, 1.0f);
        limit(destinationX, 130.0f, 240.0f, 160.0f);
        limit(movieFragmentBreakTime, 0.03f, 0.1f, 0.065f);
        limit(movieFragmentLandTime, 0.14f, 0.28f, 0.23f);
        limit(movieFragmentPullTime, 0.28f, 0.40f, 0.35f);
        limit(movieFragmentAbsorbTime, 0.40f, 0.49f, 0.46f);
        limit(movieFragmentDistance, 1.5f, 5.5f, 3.0f);
        limit(movieFragmentScale, 0.65f, 1.5f, 1.0f);
        limit(movieFragmentFallArc, 0.0f, 6.0f, 2.0f);
        limit(movieFragmentCameraDistance, 0.7f, 1.5f, 1.0f);
        // 編集や外部JSONで工程が逆転しないよう、各カットの最低時間を保証します。
        movieFragmentLandTime = (std::max)(movieFragmentLandTime, movieFragmentBreakTime + 0.07f);
        movieFragmentPullTime = (std::max)(movieFragmentPullTime, movieFragmentLandTime + 0.025f);
        movieFragmentAbsorbTime = (std::max)(movieFragmentAbsorbTime, movieFragmentPullTime + 0.05f);
        limit(movieRageTime, 0.24f, 0.33f, 0.27f);
        movieRageTime = (std::max)(movieRageTime, movieFragmentLandTime + 0.035f);
        movieFragmentPullTime = (std::max)(movieFragmentPullTime, movieRageTime + 0.035f);
        movieFragmentAbsorbTime = (std::max)(movieFragmentAbsorbTime, movieFragmentPullTime + 0.05f);
        limit(movieBodyTime, 0.475f, 0.545f, 0.510f);
        movieBodyTime = (std::max)(movieBodyTime, movieFragmentAbsorbTime + 0.055f);
        limit(movieCrownTime, 0.565f, 0.635f, 0.600f);
        movieCrownTime = (std::max)(movieCrownTime, movieBodyTime + 0.055f);
        limit(movieWingsTime, 0.645f, 0.715f, 0.680f);
        movieWingsTime = (std::max)(movieWingsTime, movieCrownTime + 0.055f);
        limit(movieEscapeTime, 0.725f, 0.795f, 0.760f);
        movieEscapeTime = (std::max)(movieEscapeTime, movieWingsTime + 0.055f);
        limit(movieArriveTime, 0.895f, 0.965f, 0.930f);
        movieArriveTime = (std::max)(movieArriveTime, movieEscapeTime + 0.055f);
        limit(lanceDamage, 1.0f, 40.0f, 12.0f);
        limit(waveDamage, 1.0f, 60.0f, 20.0f);
        limit(dashDamage, 1.0f, 60.0f, 16.0f);
        limit(armorOpenTime, 2.0f, 12.0f, 5.0f);
    }

    // 設定ファイルを読み込みます。存在しない場合は既定値を維持します。
    bool Load() {
        // 省略された調整項目は現行の既定値を維持し、読み込み後に安全な範囲へ直す。
        std::ifstream stream(Path);
        if (!stream)
            return false;
        auto data = nlohmann::json::parse(stream, nullptr, false);
        if (!data.is_object())
            return false;
        auto read = [&](const char* key, float& value) {
            auto it = data.find(key);
            if (it != data.end() && it->is_number())
                value = it->get<float>();
        };
        read("sealRadius", sealRadius);
        read("sealHitRadius", sealHitRadius);
        read("sealAimTime", sealAimTime);
        read("movieDuration", movieDuration);
        read("sealDodgeTime", sealDodgeTime);
        read("sealRecoveryTime", sealRecoveryTime);
        read("sealArmorDamageRate", sealArmorDamageRate);
        read("sealBreakDamage", sealBreakDamage);
        read("sealTargetAssistRadius", sealTargetAssistRadius);
        read("sealSatelliteSpread", sealSatelliteSpread);
        read("sealJumpHeight", sealJumpHeight);
        read("sealRushSpeed", sealRushSpeed);
        read("sealCenterOffsetZ", sealCenterOffsetZ);
        read("sealCastTime", sealCastTime);
        read("sealCrystalHp", sealCrystalHp);
        read("sealBarrierRadius", sealBarrierRadius);
        read("sealBarrierHeight", sealBarrierHeight);
        read("sealBarrageRadius", sealBarrageRadius);
        read("sealBarrageSkyHeight", sealBarrageSkyHeight);
        read("sealBarrageSkyRadiusRate", sealBarrageSkyRadiusRate);
        read("sealBarrageSpearScale", sealBarrageSpearScale);
        read("sealBarrageMovieDuration", sealBarrageMovieDuration);
        read("sealBarrageCameraDistance", sealBarrageCameraDistance);
        read("sealBarrageCameraHeight", sealBarrageCameraHeight);
        read("sealBarrageDamage", sealBarrageDamage);
        read("sealBarrageRecovery", sealBarrageRecovery);
        read("escapeHeight", escapeHeight);
        read("destinationX", destinationX);
        read("movieShake", movieShake);
        read("movieWingScale", movieWingScale);
        read("movieFragmentBreakTime", movieFragmentBreakTime);
        read("movieFragmentLandTime", movieFragmentLandTime);
        read("movieFragmentPullTime", movieFragmentPullTime);
        read("movieFragmentAbsorbTime", movieFragmentAbsorbTime);
        read("movieFragmentDistance", movieFragmentDistance);
        read("movieFragmentScale", movieFragmentScale);
        read("movieFragmentFallArc", movieFragmentFallArc);
        read("movieFragmentCameraDistance", movieFragmentCameraDistance);
        read("movieRageTime", movieRageTime);
        read("movieBodyTime", movieBodyTime);
        read("movieCrownTime", movieCrownTime);
        read("movieWingsTime", movieWingsTime);
        read("movieEscapeTime", movieEscapeTime);
        read("movieArriveTime", movieArriveTime);
        read("lanceDamage", lanceDamage);
        read("waveDamage", waveDamage);
        read("dashDamage", dashDamage);
        read("armorOpenTime", armorOpenTime);
        Sanitize();
        return true;
    }

    // 現在の調整値をエディターから再利用できる JSON に保存します。
    bool Save() const {
        // 読み込み側と同じキー名で全項目を書き、エディターでの調整値を再現する。
        nlohmann::json data = {{"sealRadius", sealRadius},
                               {"sealHitRadius", sealHitRadius},
                               {"sealAimTime", sealAimTime},
                               {"movieDuration", movieDuration},
                               {"sealDodgeTime", sealDodgeTime},
                               {"sealRecoveryTime", sealRecoveryTime},
                               {"sealArmorDamageRate", sealArmorDamageRate},
                               {"sealBreakDamage", sealBreakDamage},
                               {"sealTargetAssistRadius", sealTargetAssistRadius},
                               {"sealSatelliteSpread", sealSatelliteSpread},
                               {"sealJumpHeight", sealJumpHeight},
                               {"sealRushSpeed", sealRushSpeed},
                               {"sealCenterOffsetZ", sealCenterOffsetZ},
                               {"sealCastTime", sealCastTime},
                               {"sealCrystalHp", sealCrystalHp},
                               {"sealBarrierRadius", sealBarrierRadius},
                               {"sealBarrierHeight", sealBarrierHeight},
                               {"sealBarrageRadius", sealBarrageRadius},
                               {"sealBarrageSkyHeight", sealBarrageSkyHeight},
                               {"sealBarrageSkyRadiusRate", sealBarrageSkyRadiusRate},
                               {"sealBarrageSpearScale", sealBarrageSpearScale},
                               {"sealBarrageMovieDuration", sealBarrageMovieDuration},
                               {"sealBarrageCameraDistance", sealBarrageCameraDistance},
                               {"sealBarrageCameraHeight", sealBarrageCameraHeight},
                               {"sealBarrageDamage", sealBarrageDamage},
                               {"sealBarrageRecovery", sealBarrageRecovery},
                               {"escapeHeight", escapeHeight},
                               {"destinationX", destinationX},
                               {"movieShake", movieShake},
                               {"movieWingScale", movieWingScale},
                               {"movieFragmentBreakTime", movieFragmentBreakTime},
                               {"movieFragmentLandTime", movieFragmentLandTime},
                               {"movieFragmentPullTime", movieFragmentPullTime},
                               {"movieFragmentAbsorbTime", movieFragmentAbsorbTime},
                               {"movieFragmentDistance", movieFragmentDistance},
                               {"movieFragmentScale", movieFragmentScale},
                               {"movieFragmentFallArc", movieFragmentFallArc},
                               {"movieFragmentCameraDistance", movieFragmentCameraDistance},
                               {"movieRageTime", movieRageTime},
                               {"movieBodyTime", movieBodyTime},
                               {"movieCrownTime", movieCrownTime},
                               {"movieWingsTime", movieWingsTime},
                               {"movieEscapeTime", movieEscapeTime},
                               {"movieArriveTime", movieArriveTime},
                               {"lanceDamage", lanceDamage},
                               {"waveDamage", waveDamage},
                               {"dashDamage", dashDamage},
                               {"armorOpenTime", armorOpenTime}};
        std::ofstream stream(Path);
        if (!stream)
            return false;
        stream << data.dump(2) << '\n';
        return stream.good();
    }

    // 初回アクセス時にだけファイルを読み込む共有の調整値です。
    static CrownFinaleTuning& Get() {
        static CrownFinaleTuning tuning;
        static const bool loaded = tuning.Load();
        (void)loaded;
        return tuning;
    }
};
