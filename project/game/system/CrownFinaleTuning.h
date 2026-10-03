#pragma once

#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <cmath>

// 最終戦の調整値。ボスInspectorから保存でき、次の実行でも同じ値を使用します。
struct CrownFinaleTuning {
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
    float movieDuration = 14.0f;
    float escapeHeight = 32.0f;
    float movieShake = 1.0f;
    float movieWingScale = 1.0f;
    float destinationX = 160.0f;
    float lanceDamage = 12.0f;
    float waveDamage = 20.0f;
    float dashDamage = 16.0f;
    float armorOpenTime = 5.0f;
    static constexpr const char* Path = "Resources/json/scenario/crown_finale_tuning.json";

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
        limit(movieDuration, 12.0f, 24.0f, 14.0f);
        limit(escapeHeight, 18.0f, 60.0f, 32.0f);
        limit(movieShake, 0.0f, 2.0f, 1.0f);
        limit(movieWingScale, 0.5f, 2.0f, 1.0f);
        limit(destinationX, 130.0f, 240.0f, 160.0f);
        limit(lanceDamage, 1.0f, 40.0f, 12.0f);
        limit(waveDamage, 1.0f, 60.0f, 20.0f);
        limit(dashDamage, 1.0f, 60.0f, 16.0f);
        limit(armorOpenTime, 2.0f, 12.0f, 5.0f);
    }
    bool Load() {
        std::ifstream stream(Path);
        if (!stream) return false;
        auto data = nlohmann::json::parse(stream, nullptr, false);
        if (!data.is_object()) return false;
        auto read = [&](const char* key, float& value) {
            auto it = data.find(key);
            if (it != data.end() && it->is_number()) value = it->get<float>();
        };
        read("sealRadius", sealRadius); read("sealHitRadius", sealHitRadius);
        read("sealAimTime", sealAimTime); read("movieDuration", movieDuration);
        read("sealDodgeTime", sealDodgeTime); read("sealRecoveryTime", sealRecoveryTime);
        read("sealArmorDamageRate", sealArmorDamageRate); read("sealBreakDamage", sealBreakDamage);
        read("sealTargetAssistRadius", sealTargetAssistRadius); read("sealSatelliteSpread", sealSatelliteSpread);
        read("sealJumpHeight", sealJumpHeight); read("sealRushSpeed", sealRushSpeed);
        read("escapeHeight", escapeHeight); read("destinationX", destinationX);
        read("movieShake", movieShake); read("movieWingScale", movieWingScale);
        read("lanceDamage", lanceDamage); read("waveDamage", waveDamage);
        read("dashDamage", dashDamage); read("armorOpenTime", armorOpenTime);
        Sanitize();
        return true;
    }
    bool Save() const {
        nlohmann::json data = {
            {"sealRadius", sealRadius}, {"sealHitRadius", sealHitRadius},
            {"sealAimTime", sealAimTime}, {"movieDuration", movieDuration},
            {"sealDodgeTime", sealDodgeTime}, {"sealRecoveryTime", sealRecoveryTime},
            {"sealArmorDamageRate", sealArmorDamageRate}, {"sealBreakDamage", sealBreakDamage},
            {"sealTargetAssistRadius", sealTargetAssistRadius}, {"sealSatelliteSpread", sealSatelliteSpread},
            {"sealJumpHeight", sealJumpHeight}, {"sealRushSpeed", sealRushSpeed},
            {"escapeHeight", escapeHeight}, {"destinationX", destinationX},
            {"movieShake", movieShake}, {"movieWingScale", movieWingScale},
            {"lanceDamage", lanceDamage}, {"waveDamage", waveDamage},
            {"dashDamage", dashDamage}, {"armorOpenTime", armorOpenTime}
        };
        std::ofstream stream(Path);
        if (!stream) return false;
        stream << data.dump(2) << '\n';
        return stream.good();
    }
    static CrownFinaleTuning& Get() {
        static CrownFinaleTuning tuning;
        static const bool loaded = tuning.Load();
        (void)loaded;
        return tuning;
    }
};
