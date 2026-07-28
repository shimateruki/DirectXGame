#include "EnemyAttackProfile.h"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace {
constexpr int kCurrentVersion = 2;
constexpr const char* kProfileDirectory = "Resources/json/enemy_attack/";
std::mutex gProfileCacheMutex;
std::unordered_map<std::string, EnemyAttackProfile> gProfileCache;

float ReadFloat(const json& object, const char* key, float fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<float>();
}

std::string ReadString(const json& object, const char* key, const std::string& fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

void MergeAttackJson(const json& source, EnemyAttackDefinition& attack) {
    attack.displayName = ReadString(source, "displayName", attack.displayName);

    if (source.contains("targeting") && source["targeting"].is_object()) {
        const json& targeting = source["targeting"];
        attack.minRange = ReadFloat(targeting, "minRange", attack.minRange);
        attack.maxRange = ReadFloat(targeting, "maxRange", attack.maxRange);
        attack.radius = ReadFloat(targeting, "radius", attack.radius);
    }

    if (source.contains("timing") && source["timing"].is_object()) {
        const json& timing = source["timing"];
        attack.windupDuration = ReadFloat(timing, "windup", attack.windupDuration);
        attack.activeDuration = ReadFloat(timing, "active", attack.activeDuration);
        attack.recoveryDuration = ReadFloat(timing, "recovery", attack.recoveryDuration);
        attack.cooldown = ReadFloat(timing, "cooldown", attack.cooldown);
        attack.warningLeadTime = ReadFloat(timing, "warningLead", attack.warningLeadTime);
    }

    if (source.contains("combat") && source["combat"].is_object()) {
        const json& combat = source["combat"];
        attack.damage = ReadFloat(combat, "damage", attack.damage);
        if (combat.contains("statusEffect") && combat["statusEffect"].is_object()) {
            const json& status = combat["statusEffect"];
            attack.statusEffectType = ReadString(status, "type", attack.statusEffectType);
            attack.statusDuration = ReadFloat(status, "duration", attack.statusDuration);
            attack.statusTickInterval = ReadFloat(status, "tickInterval", attack.statusTickInterval);
            attack.statusTickDamage = ReadFloat(status, "tickDamage", attack.statusTickDamage);
            attack.statusVfx = ReadString(status, "vfx", attack.statusVfx);
        }
    }

    if (source.contains("movement") && source["movement"].is_object()) {
        const json& movement = source["movement"];
        attack.minSpeed = ReadFloat(movement, "minSpeed", attack.minSpeed);
        attack.maxSpeed = ReadFloat(movement, "maxSpeed", attack.maxSpeed);
        attack.lifetime = ReadFloat(movement, "lifetime", attack.lifetime);
    }

    if (source.contains("preview") && source["preview"].is_object()) {
        const json& preview = source["preview"];
        attack.recommendedTargetDistance = ReadFloat(preview, "targetDistance", attack.recommendedTargetDistance);
        attack.previewDuration = ReadFloat(preview, "duration", attack.previewDuration);
    }

    if (source.contains("presentation") && source["presentation"].is_object()) {
        const json& presentation = source["presentation"];
        attack.animation = ReadString(presentation, "animation", attack.animation);
        attack.windupVfx = ReadString(presentation, "windupVfx", attack.windupVfx);
        attack.activeVfx = ReadString(presentation, "activeVfx", attack.activeVfx);
        attack.impactVfx = ReadString(presentation, "impactVfx", attack.impactVfx);
        attack.audioCue = ReadString(presentation, "audioCue", attack.audioCue);
    }
}

json WriteAttackJson(const EnemyAttackDefinition& attack) {
    return {
        { "id", attack.id },
        { "displayName", attack.displayName },
        { "targeting", {
            { "minRange", attack.minRange },
            { "maxRange", attack.maxRange },
            { "radius", attack.radius }
        } },
        { "timing", {
            { "windup", attack.windupDuration },
            { "active", attack.activeDuration },
            { "recovery", attack.recoveryDuration },
            { "cooldown", attack.cooldown },
            { "warningLead", attack.warningLeadTime }
        } },
        { "combat", {
            { "damage", attack.damage },
            { "statusEffect", {
                { "type", attack.statusEffectType },
                { "duration", attack.statusDuration },
                { "tickInterval", attack.statusTickInterval },
                { "tickDamage", attack.statusTickDamage },
                { "vfx", attack.statusVfx }
            } }
        } },
        { "movement", {
            { "minSpeed", attack.minSpeed },
            { "maxSpeed", attack.maxSpeed },
            { "lifetime", attack.lifetime }
        } },
        { "preview", {
            { "targetDistance", attack.recommendedTargetDistance },
            { "duration", attack.previewDuration }
        } },
        { "presentation", {
            { "animation", attack.animation },
            { "windupVfx", attack.windupVfx },
            { "activeVfx", attack.activeVfx },
            { "impactVfx", attack.impactVfx },
            { "audioCue", attack.audioCue }
        } }
    };
}

EnemyAttackDefinition MakeAttack(
    const char* id,
    const char* displayName,
    float minRange,
    float maxRange,
    float radius,
    float windup,
    float active,
    float recovery,
    float cooldown,
    float warningLead,
    float damage,
    float minSpeed,
    float maxSpeed,
    float lifetime,
    float previewDistance,
    float previewDuration) {
    EnemyAttackDefinition attack;
    attack.id = id;
    attack.displayName = displayName;
    attack.minRange = minRange;
    attack.maxRange = maxRange;
    attack.radius = radius;
    attack.windupDuration = windup;
    attack.activeDuration = active;
    attack.recoveryDuration = recovery;
    attack.cooldown = cooldown;
    attack.warningLeadTime = warningLead;
    attack.damage = damage;
    attack.minSpeed = minSpeed;
    attack.maxSpeed = maxSpeed;
    attack.lifetime = lifetime;
    attack.recommendedTargetDistance = previewDistance;
    attack.previewDuration = previewDuration;
    return attack;
}
}

EnemyAttackProfile EnemyAttackProfile::CreateDefault(const std::string& type) {
    EnemyAttackProfile profile;
    profile.version = kCurrentVersion;
    profile.enemyType = type;

    if (type == "Slime") {
        profile.displayName = "通常スライム";
        auto attack = MakeAttack("dive_slam", "溜めジャンプ急降下", 0.35f, 20.0f, 1.35f, 1.35f, 0.46f, 0.24f, 0.0f, 0.18f, 1.0f, 22.0f, 40.0f, 0.0f, 6.0f, 4.6f);
        attack.windupVfx = "Resources/json/effect/effect_pink_slime_charge_pulse_ring.json";
        attack.activeVfx = "Resources/json/effect/effect_pink_slime_dive_streak.json";
        attack.impactVfx = "Resources/json/effect/effect_pink_slime_landing_burst_ring.json";
        profile.attacks.push_back(std::move(attack));
    } else if (type == "FireSlime") {
        profile.displayName = "炎スライム";
        auto breath = MakeAttack("flame_breath", "近距離炎ブレス", 0.0f, 4.3f, 1.65f, 0.0f, 0.62f, 0.0f, 1.55f, 0.18f, 1.0f, 0.0f, 0.0f, 0.0f, 3.2f, 3.0f);
        breath.activeVfx = "fire_slime_breath";
        breath.statusEffectType = "burning";
        breath.statusDuration = 2.2f;
        breath.statusTickInterval = 0.55f;
        breath.statusTickDamage = 0.12f;
        breath.statusVfx = "status_burning_flame";
        profile.attacks.push_back(std::move(breath));

        auto fireball = MakeAttack("fireball", "中距離火球", 6.2f, 20.0f, 0.5f, 0.42f, 0.0f, 0.38f, 1.45f, 0.18f, 1.0f, 15.0f, 28.0f, 2.65f, 8.0f, 3.35f);
        fireball.activeVfx = "fire_slime_cast";
        fireball.statusEffectType = "burning";
        fireball.statusDuration = 2.8f;
        fireball.statusTickInterval = 0.55f;
        fireball.statusTickDamage = 0.16f;
        fireball.statusVfx = "status_burning_flame";
        profile.attacks.push_back(std::move(fireball));
    } else if (type == "ThunderSlime") {
        profile.displayName = "雷スライム";
        auto attack = MakeAttack("radial_shock", "チャージ放電", 0.0f, 4.1f, 5.2f, 0.52f, 0.0f, 0.34f, 2.35f, 0.18f, 1.0f, 0.0f, 0.0f, 0.0f, 3.2f, 3.2f);
        attack.windupVfx = "thunder_slime_radial_charge";
        attack.activeVfx = "thunder_slime_discharge";
        profile.attacks.push_back(std::move(attack));

        auto lineLightning = MakeAttack("line_lightning", "中距離連続落雷", 5.8f, 15.5f, 1.35f, 0.65f, 0.13f, 0.44f, 3.2f, 0.22f, 1.0f, 0.0f, 0.0f, 0.0f, 9.0f, 4.0f);
        lineLightning.windupVfx = "thunder_slime_charge";
        lineLightning.activeVfx = "player_thunder_strike_impact";
        lineLightning.impactVfx = "Resources/json/effect/effect_thunder_scorch_mark.json";
        profile.attacks.push_back(std::move(lineLightning));
    } else if (type == "WindSlime") {
        profile.displayName = "風スライム";
        auto gust = MakeAttack("gust_breath", "暴風ブレス", 0.0f, 7.2f, 2.4f, 0.38f, 1.55f, 0.34f, 2.05f, 0.18f, 0.0f, 0.0f, 0.0f, 0.0f, 4.2f, 4.4f);
        gust.animation = "wind_breath_squash";
        gust.windupVfx = "wind_slime_charge";
        gust.activeVfx = "wind_slime_breath_stream";
        gust.impactVfx = "Resources/json/effect/effect_wind_gust_ring.json";
        gust.audioCue = "wind_breath_loop";
        profile.attacks.push_back(std::move(gust));

        auto volley = MakeAttack("aerial_wind_volley", "空中三連風弾", 6.2f, 18.0f, 0.58f, 0.62f, 1.60f, 0.46f, 2.45f, 0.20f, 1.0f, 14.0f, 22.0f, 2.8f, 9.5f, 5.0f);
        volley.animation = "aerial_wind_orb_volley";
        volley.windupVfx = "wind_slime_orb_hold";
        volley.activeVfx = "wind_slime_orb_trail";
        volley.impactVfx = "wind_slime_orb_impact";
        volley.audioCue = "wind_orb_volley";
        profile.attacks.push_back(std::move(volley));
    } else if (type == "PrismSlime") {
        profile.displayName = "プリズムスライム";

        auto spikes = MakeAttack("crystal_spikes", "晶槍召喚陣", 2.0f, 12.0f, 3.6f,
            0.88f, 0.82f, 0.62f, 2.9f, 0.28f, 1.0f,
            0.0f, 0.0f, 0.0f, 7.5f, 4.8f);
        spikes.animation = "prism_spike_summon";
        spikes.windupVfx = "prism_spike_warning";
        spikes.activeVfx = "prism_spike_burst";
        spikes.impactVfx = "prism_spike_shatter";
        profile.attacks.push_back(std::move(spikes));

        auto crystalVolley = MakeAttack("crystal_lance_volley", "浮遊晶槍斉射", 4.0f, 22.0f, 0.85f,
            0.95f, 0.85f, 0.55f, 3.8f, 0.24f, 1.0f,
            18.0f, 24.0f, 2.4f, 10.0f, 5.0f);
        crystalVolley.animation = "crystal_lance_volley";
        crystalVolley.windupVfx = "prism_slime_charge";
        crystalVolley.activeVfx = "prism_lance_trail";
        crystalVolley.impactVfx = "prism_spike_shatter";
        profile.attacks.push_back(std::move(crystalVolley));

        auto fireFan = MakeAttack("fire_fan", "炎の扇状連射", 4.0f, 18.0f, 0.48f,
            0.65f, 0.52f, 0.48f, 2.9f, 0.20f, 1.0f,
            13.0f, 28.0f, 2.8f, 9.0f, 4.2f);
        fireFan.animation = "fire_fan_recoil";
        fireFan.windupVfx = "fire_slime_cast";
        fireFan.activeVfx = "fire_slime_breath_embers";
        fireFan.statusEffectType = "burning";
        fireFan.statusDuration = 2.4f;
        fireFan.statusTickInterval = 0.55f;
        fireFan.statusTickDamage = 0.14f;
        fireFan.statusVfx = "status_burning_flame";
        profile.attacks.push_back(std::move(fireFan));

        auto thunder = MakeAttack("thunder_chain", "直線連続落雷", 4.0f, 18.0f, 1.42f,
            0.76f, 0.62f, 0.48f, 3.15f, 0.23f, 1.0f,
            0.0f, 0.0f, 0.0f, 10.0f, 4.4f);
        thunder.animation = "thunder_chain_tremble";
        thunder.windupVfx = "thunder_slime_charge";
        thunder.activeVfx = "player_thunder_strike_impact";
        thunder.impactVfx = "Resources/json/effect/effect_thunder_scorch_mark.json";
        profile.attacks.push_back(std::move(thunder));

        auto wind = MakeAttack("wind_wave", "持続する暴風波", 0.0f, 10.5f, 3.1f,
            0.58f, 1.12f, 0.46f, 2.75f, 0.20f, 0.65f,
            0.0f, 0.0f, 0.0f, 6.8f, 4.3f);
        wind.animation = "wind_wave_stretch";
        wind.windupVfx = "wind_slime_charge";
        wind.activeVfx = "wind_slime_breath_stream";
        wind.impactVfx = "Resources/json/effect/effect_wind_gust_ring.json";
        profile.attacks.push_back(std::move(wind));

        auto summon = MakeAttack("slime_summon", "彩色スライム召喚陣", 0.0f, 30.0f, 6.0f,
            1.25f, 0.72f, 0.55f, 5.5f, 0.28f, 0.0f,
            0.0f, 0.0f, 0.0f, 8.0f, 5.4f);
        summon.animation = "slime_summon_cast";
        summon.windupVfx = "prism_slime_charge";
        summon.activeVfx = "prism_slime_pulse";
        profile.attacks.push_back(std::move(summon));
    } else if (type == "GiantSlime") {
        profile.displayName = "巨大スライム";
        auto attack = MakeAttack("jump_press", "ジャンププレス", 4.0f, 18.0f, 7.0f, 0.78f, 0.0f, 1.15f, 1.15f, 0.18f, 2.0f, 0.0f, 0.0f, 0.0f, 9.0f, 5.0f);
        profile.attacks.push_back(std::move(attack));
    } else {
        profile.displayName = type;
    }

    return profile;
}

std::string EnemyAttackProfile::GetDefaultPath(const std::string& type) {
    std::string fileName;
    fileName.reserve(type.size());
    for (char character : type) {
        if (character >= 'A' && character <= 'Z') {
            if (!fileName.empty()) {
                fileName.push_back('_');
            }
            fileName.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            fileName.push_back(character);
        }
    }
    return std::string(kProfileDirectory) + fileName + ".json";
}

bool EnemyAttackProfile::LoadCachedForEnemy(const std::string& type, EnemyAttackProfile& destination, std::string* errorMessage) {
    {
        std::lock_guard<std::mutex> lock(gProfileCacheMutex);
        const auto it = gProfileCache.find(type);
        if (it != gProfileCache.end()) {
            destination = it->second;
            if (errorMessage) {
                errorMessage->clear();
            }
            return true;
        }
    }

    EnemyAttackProfile loadedProfile;
    const bool loadedFromFile = loadedProfile.LoadForEnemy(type, errorMessage);
    {
        std::lock_guard<std::mutex> lock(gProfileCacheMutex);
        gProfileCache[type] = loadedProfile;
    }
    destination = std::move(loadedProfile);
    return loadedFromFile;
}

void EnemyAttackProfile::InvalidateCache(const std::string& type) {
    std::lock_guard<std::mutex> lock(gProfileCacheMutex);
    if (type.empty()) {
        gProfileCache.clear();
    } else {
        gProfileCache.erase(type);
    }
}

bool EnemyAttackProfile::LoadForEnemy(const std::string& type, std::string* errorMessage) {
    *this = CreateDefault(type);
    const std::string path = GetDefaultPath(type);
    if (!std::filesystem::exists(path)) {
        if (errorMessage) {
            *errorMessage = "プロファイルが存在しないため既定値を使用します: " + path;
        }
        return false;
    }

    EnemyAttackProfile loadedProfile = *this;
    if (!loadedProfile.LoadFromFile(path, errorMessage)) {
        return false;
    }
    if (loadedProfile.enemyType != type) {
        if (errorMessage) {
            *errorMessage = "プロファイルのenemyTypeがファイル名と一致しません: " + path;
        }
        return false;
    }
    *this = std::move(loadedProfile);
    return true;
}

bool EnemyAttackProfile::LoadFromFile(const std::string& path, std::string* errorMessage) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            if (errorMessage) {
                *errorMessage = "プロファイルを開けません: " + path;
            }
            return false;
        }

        json root;
        file >> root;
        if (!root.is_object()) {
            if (errorMessage) {
                *errorMessage = "プロファイルのルートがオブジェクトではありません: " + path;
            }
            return false;
        }

        const std::string loadedType = ReadString(root, "enemyType", enemyType);
        EnemyAttackProfile merged = CreateDefault(loadedType.empty() ? enemyType : loadedType);
        merged.version = root.value("version", kCurrentVersion);
        merged.enemyType = loadedType.empty() ? merged.enemyType : loadedType;
        merged.displayName = ReadString(root, "displayName", merged.displayName);

        if (root.contains("attacks") && root["attacks"].is_array()) {
            for (const json& attackJson : root["attacks"]) {
                if (!attackJson.is_object()) {
                    continue;
                }
                const std::string id = ReadString(attackJson, "id", "");
                if (id.empty()) {
                    continue;
                }

                EnemyAttackDefinition* attack = merged.FindAttack(id);
                if (!attack) {
                    EnemyAttackDefinition added;
                    added.id = id;
                    added.displayName = id;
                    merged.attacks.push_back(std::move(added));
                    attack = &merged.attacks.back();
                }
                MergeAttackJson(attackJson, *attack);
            }
        }

        merged.Sanitize();
        *this = std::move(merged);
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        if (errorMessage) {
            *errorMessage = std::string("プロファイルの解析に失敗しました: ") + exception.what();
        }
        return false;
    }
}

bool EnemyAttackProfile::SaveToFile(const std::string& path, std::string* errorMessage) const {
    try {
        const std::filesystem::path outputPath(path);
        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }

        json root;
        root["version"] = kCurrentVersion;
        root["enemyType"] = enemyType;
        root["displayName"] = displayName;
        root["attacks"] = json::array();
        for (const EnemyAttackDefinition& attack : attacks) {
            root["attacks"].push_back(WriteAttackJson(attack));
        }

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open()) {
            if (errorMessage) {
                *errorMessage = "プロファイルを書き込めません: " + path;
            }
            return false;
        }
        file << root.dump(2) << '\n';
        if (!file.good()) {
            if (errorMessage) {
                *errorMessage = "プロファイルの書き込み中にエラーが発生しました: " + path;
            }
            return false;
        }
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        if (errorMessage) {
            *errorMessage = std::string("プロファイルの保存に失敗しました: ") + exception.what();
        }
        return false;
    }
}

const EnemyAttackDefinition* EnemyAttackProfile::FindAttack(const std::string& attackId) const {
    const auto it = std::find_if(attacks.begin(), attacks.end(), [&](const EnemyAttackDefinition& attack) {
        return attack.id == attackId;
    });
    return it != attacks.end() ? &*it : nullptr;
}

EnemyAttackDefinition* EnemyAttackProfile::FindAttack(const std::string& attackId) {
    const auto it = std::find_if(attacks.begin(), attacks.end(), [&](const EnemyAttackDefinition& attack) {
        return attack.id == attackId;
    });
    return it != attacks.end() ? &*it : nullptr;
}

void EnemyAttackProfile::Sanitize() {
    std::unordered_set<std::string> usedIds;
    for (EnemyAttackDefinition& attack : attacks) {
        if (attack.id.empty() || !usedIds.insert(attack.id).second) {
            continue;
        }
        attack.minRange = (std::max)(0.0f, attack.minRange);
        attack.maxRange = (std::max)(attack.minRange, attack.maxRange);
        attack.radius = (std::max)(0.01f, attack.radius);
        attack.windupDuration = (std::max)(0.0f, attack.windupDuration);
        attack.activeDuration = (std::max)(0.0f, attack.activeDuration);
        attack.recoveryDuration = (std::max)(0.0f, attack.recoveryDuration);
        attack.cooldown = (std::max)(0.0f, attack.cooldown);
        attack.warningLeadTime = std::clamp(attack.warningLeadTime, 0.0f, attack.windupDuration + attack.activeDuration);
        attack.damage = (std::max)(0.0f, attack.damage);
        if (attack.statusEffectType == "none") {
            attack.statusEffectType.clear();
        }
        attack.statusDuration = (std::max)(0.0f, attack.statusDuration);
        attack.statusTickInterval = (std::max)(0.05f, attack.statusTickInterval);
        attack.statusTickDamage = (std::max)(0.0f, attack.statusTickDamage);
        if (attack.statusEffectType.empty() || attack.statusDuration <= 0.0f) {
            attack.statusDuration = 0.0f;
            attack.statusTickDamage = 0.0f;
        }
        attack.minSpeed = (std::max)(0.0f, attack.minSpeed);
        attack.maxSpeed = (std::max)(attack.minSpeed, attack.maxSpeed);
        attack.lifetime = (std::max)(0.0f, attack.lifetime);
        attack.recommendedTargetDistance = (std::max)(0.0f, attack.recommendedTargetDistance);
        attack.previewDuration = (std::max)(0.1f, attack.previewDuration);
    }
}
