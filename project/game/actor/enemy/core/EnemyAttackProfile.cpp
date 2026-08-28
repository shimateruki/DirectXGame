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
constexpr int kCurrentVersion = 3;
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

Vector3 ReadVector3(const json& object, const char* key, const Vector3& fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() < 3 ||
        !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) {
        return fallback;
    }
    return { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
}

json WriteVector3(const Vector3& value) {
    return json::array({ value.x, value.y, value.z });
}

void MergeAttackJson(const json& source, EnemyAttackDefinition& attack) {
    attack.displayName = ReadString(source, "displayName", attack.displayName);

    if (source.contains("targeting") && source["targeting"].is_object()) {
        const json& targeting = source["targeting"];
        attack.minRange = ReadFloat(targeting, "minRange", attack.minRange);
        attack.maxRange = ReadFloat(targeting, "maxRange", attack.maxRange);
        attack.radius = ReadFloat(targeting, "radius", attack.radius);
        attack.hitShape = ReadString(targeting, "shape", attack.hitShape);
        attack.hitOffset = ReadVector3(targeting, "offset", attack.hitOffset);
        attack.hitSize = ReadVector3(targeting, "size", attack.hitSize);
    }

    if (source.contains("timing") && source["timing"].is_object()) {
        const json& timing = source["timing"];
        attack.windupDuration = ReadFloat(timing, "windup", attack.windupDuration);
        attack.activeDuration = ReadFloat(timing, "active", attack.activeDuration);
        attack.recoveryDuration = ReadFloat(timing, "recovery", attack.recoveryDuration);
        attack.cooldown = ReadFloat(timing, "cooldown", attack.cooldown);
        attack.warningLeadTime = ReadFloat(timing, "warningLead", attack.warningLeadTime);
        attack.cancelWindowStart = ReadFloat(timing, "cancelStart", attack.cancelWindowStart);
        attack.cancelWindowEnd = ReadFloat(timing, "cancelEnd", attack.cancelWindowEnd);
    }

    if (source.contains("combat") && source["combat"].is_object()) {
        const json& combat = source["combat"];
        attack.damage = ReadFloat(combat, "damage", attack.damage);
        attack.knockbackVelocity = ReadVector3(combat, "knockback", attack.knockbackVelocity);
        attack.invincibilityDuration = ReadFloat(combat, "invincibility", attack.invincibilityDuration);
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
        attack.feedbackCue = ReadString(presentation, "feedbackCue", attack.feedbackCue);
    }
}

json WriteAttackJson(const EnemyAttackDefinition& attack) {
    return {
        { "id", attack.id },
        { "displayName", attack.displayName },
        { "targeting", {
            { "minRange", attack.minRange },
            { "maxRange", attack.maxRange },
            { "radius", attack.radius },
            { "shape", attack.hitShape },
            { "offset", WriteVector3(attack.hitOffset) },
            { "size", WriteVector3(attack.hitSize) }
        } },
        { "timing", {
            { "windup", attack.windupDuration },
            { "active", attack.activeDuration },
            { "recovery", attack.recoveryDuration },
            { "cooldown", attack.cooldown },
            { "warningLead", attack.warningLeadTime },
            { "cancelStart", attack.cancelWindowStart },
            { "cancelEnd", attack.cancelWindowEnd }
        } },
        { "combat", {
            { "damage", attack.damage },
            { "knockback", WriteVector3(attack.knockbackVelocity) },
            { "invincibility", attack.invincibilityDuration },
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
            { "audioCue", attack.audioCue },
            { "feedbackCue", attack.feedbackCue }
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

float EnemyAttackDefinition::GetPhaseDuration(AttackAbilityPhase phase) const {
    switch (phase) {
    case AttackAbilityPhase::Windup:
        return (std::max)(0.0f, windupDuration);
    case AttackAbilityPhase::Active:
        return (std::max)(0.0f, activeDuration);
    case AttackAbilityPhase::Recovery:
        return (std::max)(0.0f, recoveryDuration);
    case AttackAbilityPhase::Cooldown:
        return (std::max)(0.0f, cooldown);
    case AttackAbilityPhase::Ready:
    default:
        return 0.0f;
    }
}

float EnemyAttackDefinition::GetPhaseProgressFromElapsed(
    AttackAbilityPhase phase,
    float elapsed) const {
    const float duration = (std::max)(0.01f, GetPhaseDuration(phase));
    return std::clamp(elapsed / duration, 0.0f, 1.0f);
}

float EnemyAttackDefinition::GetPhaseProgressFromRemaining(
    AttackAbilityPhase phase,
    float remaining) const {
    const float duration = (std::max)(0.01f, GetPhaseDuration(phase));
    return 1.0f - std::clamp(remaining / duration, 0.0f, 1.0f);
}

float EnemyAttackDefinition::GetActionDuration() const {
    return (std::max)(0.0f, windupDuration) +
        (std::max)(0.0f, activeDuration) +
        (std::max)(0.0f, recoveryDuration);
}

float EnemyAttackDefinition::GetTotalDuration() const {
    return GetActionDuration() + (std::max)(0.0f, cooldown);
}

bool EnemyAttackDefinition::IsCancelWindowOpen(float elapsedActionTime) const {
    const float start = (std::max)(0.0f, cancelWindowStart);
    const float end = (std::max)(start, cancelWindowEnd);
    return end > start && elapsedActionTime >= start && elapsedActionTime <= end;
}

bool EnemyAttackDefinition::HasConfiguredKnockback() const {
    return Math::Length(knockbackVelocity) > 0.001f;
}

Vector3 EnemyAttackDefinition::ResolveKnockback(
    const Vector3& forward,
    const Vector3& legacyFallback) const {
    if (!HasConfiguredKnockback()) {
        return legacyFallback;
    }

    Vector3 planarForward = { forward.x, 0.0f, forward.z };
    if (Math::Length(planarForward) <= 0.001f) {
        planarForward = { 0.0f, 0.0f, 1.0f };
    } else {
        planarForward = Math::Normalize(planarForward);
    }
    const Vector3 right = { planarForward.z, 0.0f, -planarForward.x };
    return right * knockbackVelocity.x +
        Vector3{ 0.0f, knockbackVelocity.y, 0.0f } +
        planarForward * knockbackVelocity.z;
}

float EnemyAttackDefinition::ResolveInvincibilityDuration(float legacyFallback) const {
    return invincibilityDuration > 0.0f
        ? invincibilityDuration
        : (std::max)(0.0f, legacyFallback);
}

bool AttackAbilityRuntime::Start(const AttackAbilityDefinition& definition) {
    if (!IsReady()) {
        return false;
    }
    definition_ = definition;
    definition_.windupDuration = (std::max)(0.0f, definition_.windupDuration);
    definition_.activeDuration = (std::max)(0.0f, definition_.activeDuration);
    definition_.recoveryDuration = (std::max)(0.0f, definition_.recoveryDuration);
    definition_.cooldown = (std::max)(0.0f, definition_.cooldown);
    hasDefinition_ = true;
    phase_ = AttackAbilityPhase::Windup;
    phaseTime_ = 0.0f;
    return true;
}

AttackAbilityUpdateResult AttackAbilityRuntime::Update(float deltaTime) {
    AttackAbilityUpdateResult result;
    result.previousPhase = phase_;
    result.currentPhase = phase_;
    if (!hasDefinition_ || phase_ == AttackAbilityPhase::Ready) {
        return result;
    }

    float remainingTime = (std::max)(0.0f, deltaTime);
    for (int transitionCount = 0; transitionCount < 5; ++transitionCount) {
        const float duration = definition_.GetPhaseDuration(phase_);
        const float timeToBoundary = (std::max)(0.0f, duration - phaseTime_);
        if (duration > 0.0f && remainingTime < timeToBoundary) {
            phaseTime_ += remainingTime;
            remainingTime = 0.0f;
            break;
        }

        if (duration > 0.0f) {
            remainingTime = (std::max)(0.0f, remainingTime - timeToBoundary);
        }
        phase_ = NextPhase(phase_);
        phaseTime_ = 0.0f;

        result.enteredActive |= phase_ == AttackAbilityPhase::Active;
        result.enteredRecovery |= phase_ == AttackAbilityPhase::Recovery;
        result.enteredCooldown |= phase_ == AttackAbilityPhase::Cooldown;
        result.becameReady |= phase_ == AttackAbilityPhase::Ready;

        if (phase_ == AttackAbilityPhase::Ready ||
            (remainingTime <= 0.0f && definition_.GetPhaseDuration(phase_) > 0.0f)) {
            break;
        }
    }

    result.currentPhase = phase_;
    return result;
}

void AttackAbilityRuntime::Cancel(bool enterCooldown) {
    if (!hasDefinition_) {
        Reset();
        return;
    }
    phaseTime_ = 0.0f;
    phase_ = enterCooldown && definition_.cooldown > 0.0f
        ? AttackAbilityPhase::Cooldown
        : AttackAbilityPhase::Ready;
}

void AttackAbilityRuntime::Reset() {
    definition_ = {};
    phase_ = AttackAbilityPhase::Ready;
    phaseTime_ = 0.0f;
    hasDefinition_ = false;
}

float AttackAbilityRuntime::GetPhaseProgress() const {
    return hasDefinition_
        ? definition_.GetPhaseProgressFromElapsed(phase_, phaseTime_)
        : 0.0f;
}

float AttackAbilityRuntime::GetElapsedActionTime() const {
    if (!hasDefinition_) {
        return 0.0f;
    }
    switch (phase_) {
    case AttackAbilityPhase::Windup:
        return phaseTime_;
    case AttackAbilityPhase::Active:
        return definition_.windupDuration + phaseTime_;
    case AttackAbilityPhase::Recovery:
        return definition_.windupDuration + definition_.activeDuration + phaseTime_;
    case AttackAbilityPhase::Cooldown:
        return definition_.GetActionDuration();
    case AttackAbilityPhase::Ready:
    default:
        return 0.0f;
    }
}

bool AttackAbilityRuntime::CanCancel() const {
    return hasDefinition_ && definition_.IsCancelWindowOpen(GetElapsedActionTime());
}

AttackAbilityPhase AttackAbilityRuntime::NextPhase(AttackAbilityPhase phase) const {
    switch (phase) {
    case AttackAbilityPhase::Windup: return AttackAbilityPhase::Active;
    case AttackAbilityPhase::Active: return AttackAbilityPhase::Recovery;
    case AttackAbilityPhase::Recovery: return AttackAbilityPhase::Cooldown;
    case AttackAbilityPhase::Cooldown:
    case AttackAbilityPhase::Ready:
    default:
        return AttackAbilityPhase::Ready;
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
    } else if (type == "FalseKingSlime") {
        profile.displayName = "偽王スライム";

        auto lanceRain = MakeAttack("crown_lance_rain", "王冠晶槍雨", 0.0f, 34.0f, 2.15f,
            1.15f, 2.65f, 0.68f, 3.25f, 0.34f, 0.90f,
            0.0f, 0.0f, 3.0f, 11.0f, 6.8f);
        lanceRain.animation = "crown_lance_cast";
        lanceRain.windupVfx = "false_king_charge";
        lanceRain.activeVfx = "false_king_lance_trail";
        lanceRain.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(lanceRain));

        auto shockwave = MakeAttack("royal_shockwave", "王威連環波", 0.0f, 30.0f, 0.95f,
            0.96f, 2.95f, 0.72f, 3.55f, 0.30f, 0.72f,
            0.0f, 0.0f, 3.0f, 10.0f, 6.5f);
        shockwave.animation = "royal_shockwave_slam";
        shockwave.windupVfx = "false_king_charge";
        shockwave.activeVfx = "false_king_shockwave";
        shockwave.impactVfx = "false_king_shockwave";
        profile.attacks.push_back(std::move(shockwave));

        auto rush = MakeAttack("king_rush", "覇王三連突進", 3.0f, 27.0f, 2.65f,
            0.86f, 2.75f, 0.88f, 3.75f, 0.28f, 1.25f,
            22.0f, 34.0f, 2.8f, 13.0f, 6.5f);
        rush.animation = "king_rush_squash";
        rush.windupVfx = "false_king_charge";
        rush.activeVfx = "false_king_rush_wake";
        rush.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(rush));

        auto cross = MakeAttack("royal_cross", "王冠十字掃射", 0.0f, 32.0f, 1.45f,
            1.12f, 3.55f, 0.78f, 4.35f, 0.36f, 0.62f,
            0.0f, 0.0f, 3.6f, 12.0f, 7.2f);
        cross.animation = "royal_cross_cast";
        cross.windupVfx = "false_king_charge";
        cross.activeVfx = "false_king_dominion";
        cross.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(cross));

        auto dominion = MakeAttack("crown_dominion", "王冠領域・終幕", 0.0f, 34.0f, 1.55f,
            1.68f, 5.60f, 1.12f, 7.20f, 0.48f, 0.58f,
            0.0f, 0.0f, 5.7f, 14.0f, 10.0f);
        dominion.animation = "crown_dominion_cast";
        dominion.windupVfx = "false_king_dominion";
        dominion.activeVfx = "false_king_dominion";
        dominion.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(dominion));
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
        if (attack.hitShape == "Sphere") attack.hitShape = "sphere";
        else if (attack.hitShape == "Capsule") attack.hitShape = "capsule";
        else if (attack.hitShape == "Box") attack.hitShape = "box";
        else if (attack.hitShape == "Cone") attack.hitShape = "cone";
        if (attack.hitShape != "sphere" && attack.hitShape != "capsule" &&
            attack.hitShape != "box" && attack.hitShape != "cone") {
            attack.hitShape = "sphere";
        }
        attack.hitSize.x = (std::max)(0.01f, std::abs(attack.hitSize.x));
        attack.hitSize.y = (std::max)(0.01f, std::abs(attack.hitSize.y));
        attack.hitSize.z = (std::max)(0.01f, std::abs(attack.hitSize.z));
        attack.windupDuration = (std::max)(0.0f, attack.windupDuration);
        attack.activeDuration = (std::max)(0.0f, attack.activeDuration);
        attack.recoveryDuration = (std::max)(0.0f, attack.recoveryDuration);
        attack.cooldown = (std::max)(0.0f, attack.cooldown);
        attack.warningLeadTime = std::clamp(attack.warningLeadTime, 0.0f, attack.windupDuration + attack.activeDuration);
        const float actionDuration = attack.GetActionDuration();
        attack.cancelWindowStart = std::clamp(attack.cancelWindowStart, 0.0f, actionDuration);
        attack.cancelWindowEnd = std::clamp(
            attack.cancelWindowEnd,
            attack.cancelWindowStart,
            actionDuration);
        attack.damage = (std::max)(0.0f, attack.damage);
        attack.invincibilityDuration = (std::max)(0.0f, attack.invincibilityDuration);
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
