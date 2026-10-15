#include "game/actor/enemy/combat/EnemyAttackProfile.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using json = nlohmann::json;

namespace {
constexpr int kCurrentVersion = 7;
constexpr const char* kProfileDirectory = "Resources/json/enemy_attack/";
std::mutex gProfileCacheMutex;
std::unordered_map<std::string, EnemyAttackProfile> gProfileCache;

// 攻撃JSONの数値項目を読み、欠落時は指定された既定値を返します。
float ReadFloat(const json& object, const char* key, float fallback) {
    // 欠損やNaNを既定値へ戻し、後段の距離・秒数計算へ不正値を流さない。
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return fallback;
    }
    const float value = it->get<float>();
    return std::isfinite(value) ? value : fallback;
}

std::string ReadString(const json& object, const char* key, const std::string& fallback) {
    // 古いJSONにキーがなくても既定の表示名やアセット名を維持する。
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

engine::Vector3 ReadVector3(const json& object, const char* key, const engine::Vector3& fallback) {
    // 3軸がすべて数値の場合だけ採用し、部分的な配列は既定値へ戻す。
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() < 3 || !(*it)[0].is_number() || !(*it)[1].is_number() ||
        !(*it)[2].is_number()) {
        return fallback;
    }
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}

json WriteVector3(const engine::Vector3& value) {
    // 読み込み側と同じXYZ順の配列として保存する。
    return json::array({value.x, value.y, value.z});
}

// 弾の当たり判定、運動、外観を一つの攻撃部品JSONへ保存します。
json WriteProjectile(const AttackProjectileComponent& p) {
    // 弾の判定・移動・外観を同じ部品の下にまとめ、攻撃定義から独立して保存する。
    const auto& a = p.appearance;
    return {{"model", p.model}, {"style", p.style}, {"collisionEnabled", p.collisionEnabled},
        {"radius", p.radius}, {"lifetime", p.lifetime}, {"minSpeed", p.minSpeed}, {"maxSpeed", p.maxSpeed},
        {"damage", p.damage}, {"visualScale", p.visualScale}, {"trail", p.trail}, {"secondaryTrail", p.secondaryTrail},
        {"impact", p.impact}, {"secondaryImpact", p.secondaryImpact},
        {"appearance", {{"color", {a.red, a.green, a.blue, a.alpha}}, {"emissive", a.emissive},
        {"effectScale", a.effectScale}, {"softness", a.softness}, {"intensity", a.intensity},
        {"billboardScale", a.billboardScale}, {"trailInterval", a.trailInterval}, {"trailSpeedScale", a.trailSpeedScale}}}};
}

// 省略値には既定値を使い、弾部品全体を検証して復元します。
AttackProjectileComponent ReadProjectile(const json& source) {
    // 省略された項目は部品の既定値を使い、最後に組み合わせ全体を検証する。
    AttackProjectileComponent p;
    p.configured = true;
    p.model = source.value("model", p.model); p.style = source.value("style", p.style);
    p.collisionEnabled = source.value("collisionEnabled", p.collisionEnabled);
    p.radius = source.value("radius", p.radius); p.lifetime = source.value("lifetime", p.lifetime);
    p.minSpeed = source.value("minSpeed", p.minSpeed); p.maxSpeed = source.value("maxSpeed", p.maxSpeed);
    p.damage = source.value("damage", p.damage); p.visualScale = source.value("visualScale", p.visualScale);
    p.trail = source.value("trail", p.trail); p.secondaryTrail = source.value("secondaryTrail", p.secondaryTrail);
    p.impact = source.value("impact", p.impact); p.secondaryImpact = source.value("secondaryImpact", p.secondaryImpact);
    if (source.contains("appearance")) {
        const auto& j = source.at("appearance"); auto& a = p.appearance;
        if (j.contains("color")) {
            const auto& c = j.at("color"); a.red = c.at(0).get<float>(); a.green = c.at(1).get<float>();
            a.blue = c.at(2).get<float>(); a.alpha = c.at(3).get<float>();
        }
        a.emissive = j.value("emissive", a.emissive); a.effectScale = j.value("effectScale", a.effectScale);
        a.softness = j.value("softness", a.softness); a.intensity = j.value("intensity", a.intensity);
        a.billboardScale = j.value("billboardScale", a.billboardScale);
        a.trailInterval = j.value("trailInterval", a.trailInterval); a.trailSpeedScale = j.value("trailSpeedScale", a.trailSpeedScale);
    }
    if (!p.Validate()) throw std::runtime_error("弾部品の設定が不正です。");
    return p;
}

// 攻撃イベントと各トラックを時間順の攻撃シーケンスJSONへ保存します。
json WriteSequence(const AttackSequenceDefinition& sequence) {
    // 時間順のイベントと見た目・判定トラックを一つの攻撃アセットに書き出す。
    json tracks = json::array();
    for (const auto& track : sequence.tracks) {
        json keys = json::array();
        for (const auto& key : track.keys)
            keys.push_back({{"time", key.time}, {"position", key.position}, {"rotation", key.rotation},
                {"scale", key.scale}, {"interpolation", key.interpolation}});
        tracks.push_back({{"name", track.name}, {"type", track.type}, {"preset", track.preset},
            {"motion", track.motion}, {"launchDelay", track.launchDelay}, {"moveSpeed", track.moveSpeed},
            {"turnDegreesPerSecond", track.turnDegreesPerSecond}, {"homingDuration", track.homingDuration},
            {"targetHeight", track.targetHeight}, {"parentTrack", track.parentTrack},
            {"conditionTrack", track.conditionTrack}, {"playbackSpeed", track.playbackSpeed}, {"clipOffset", track.clipOffset},
            {"blendIn", track.blendIn}, {"blendOut", track.blendOut}, {"loop", track.loop},
            {"condition", track.condition}, {"enabled", track.enabled}, {"followOwner", track.followOwner},
            {"begin", track.begin}, {"end", track.end}, {"radius", track.radius},
            {"damageScale", track.damageScale}, {"keys", keys}});
    }
    json events = json::array();
    for (const auto& e : sequence.events) {
        events.push_back({{"time", e.time}, {"operation", e.operation}, {"condition", e.condition},
            {"stopOnFalse", e.stopOnFalse}, {"offset", {e.right, e.up, e.forward}},
            {"sizeOffset", {e.sizeRight, e.sizeUp, e.sizeForward}}, {"yawDegrees", e.yawDegrees},
            {"aimAtTarget", e.aimAtTarget}, {"targetHeight", e.targetHeight},
            {"distanceSpeedScale", e.distanceSpeedScale}, {"distanceSpeedBias", e.distanceSpeedBias}, {"verticalSpeed", e.verticalSpeed},
            {"directed", e.directed}, {"particleSpeedScale", e.particleSpeedScale},
            {"count", e.count}, {"spreadDegrees", e.spreadDegrees}, {"preset", e.preset}});
        events.back()["name"] = e.name;
        events.back()["enabled"] = e.enabled;
        if (e.projectile.configured) events.back()["projectile"] = WriteProjectile(e.projectile);
    }
    const auto& a = sequence.fireAppearance;
    const json appearance = {{"color", {a.red, a.green, a.blue, a.alpha}}, {"emissive", a.emissive},
        {"effectScale", a.effectScale}, {"softness", a.softness}, {"intensity", a.intensity},
        {"billboardScale", a.billboardScale}, {"trailInterval", a.trailInterval}, {"trailSpeedScale", a.trailSpeedScale}};
    return {{"assetPath", sequence.assetPath}, {"enabled", sequence.enabled}, {"binding", sequence.binding}, {"priority", sequence.priority},
        {"duration", sequence.duration}, {"actorControl", {
            {"enabled", sequence.actorControl.enabled}, {"stopMovement", sequence.actorControl.stopMovement},
            {"faceTarget", sequence.actorControl.faceTarget}, {"aimLockTime", sequence.actorControl.aimLockTime},
            {"modelYawOffset", sequence.actorControl.modelYawOffset}, {"telegraph", sequence.actorControl.telegraph},
            {"telegraphEnd", sequence.actorControl.telegraphEnd}, {"telegraphWidth", sequence.actorControl.telegraphWidth},
            {"cueTime", sequence.actorControl.cueTime}, {"tint", sequence.actorControl.tint},
            {"color", sequence.actorControl.color}, {"telegraphColor", sequence.actorControl.telegraphColor}}},
        {"projectileStyle", sequence.projectileStyle},
        {"visualScale", sequence.visualScale}, {"fireAppearance", appearance}, {"events", events}, {"tracks", tracks}};
}

// 外部アセット参照を優先し、なければ埋め込みトラックを復元します。
void ReadSequence(const json& source, AttackSequenceDefinition& sequence) {
    // 外部アセット参照があればそれを優先し、なければJSON内のトラックを復元する。
    const std::string assetPath = source.value("assetPath", std::string{});
    if (!assetPath.empty()) {
        std::string error;
        sequence.binding = source.value("binding", std::string("enemy"));
        sequence.priority = source.value("priority", 0);
        if (!EnemyAttackProfile::LoadSequenceAsset(assetPath, sequence, &error)) throw std::runtime_error(error);
        sequence.enabled = source.value("enabled", true);
        return;
    }
    sequence.assetPath.clear();
    sequence.tracks.clear();
    for (const auto& item : source.value("tracks", json::array())) {
        AttackSequenceTrack track;
        track.name = item.value("name", track.name);
        track.type = item.value("type", track.type);
        track.motion = item.value("motion", "keyframed");
        track.launchDelay = item.value("launchDelay", 0.5f);
        track.moveSpeed = item.value("moveSpeed", 6.0f);
        track.turnDegreesPerSecond = item.value("turnDegreesPerSecond", 90.0f);
        track.homingDuration = item.value("homingDuration", 1.0f);
        track.targetHeight = item.value("targetHeight", 0.7f);
        track.parentTrack = item.value("parentTrack", -1);
        track.preset = item.value("preset", track.preset);
        track.condition = item.value("condition", track.condition);
        track.conditionTrack = item.value("conditionTrack", -1);
        track.playbackSpeed = item.value("playbackSpeed", 1.0f);
        track.clipOffset = item.value("clipOffset", 0.0f);
        track.blendIn = item.value("blendIn", 0.05f);
        track.blendOut = item.value("blendOut", 0.05f);
        track.loop = item.value("loop", false);
        track.enabled = item.value("enabled", true);
        track.followOwner = item.value("followOwner", true);
        track.begin = item.value("begin", 0.0f);
        track.end = item.value("end", 1.0f);
        track.radius = item.value("radius", 0.5f);
        track.damageScale = item.value("damageScale", 1.0f);
        track.keys.clear();
        for (const auto& entry : item.at("keys")) {
            AttackTransformKey key;
            key.time = entry.at("time").get<float>();
            key.position = entry.at("position").get<std::array<float, 3>>();
            key.rotation = entry.at("rotation").get<std::array<float, 3>>();
            key.scale = entry.at("scale").get<std::array<float, 3>>();
            key.interpolation = entry.value("interpolation", 1);
            track.keys.push_back(key);
        }
        sequence.tracks.push_back(std::move(track));
    }
    sequence.enabled = source.value("enabled", false);
    sequence.binding = source.value("binding", std::string("enemy"));
    sequence.priority = source.value("priority", 0);
    sequence.duration = source.value("duration", 0.42f);
    sequence.actorControl = {};
    if (source.contains("actorControl")) {
        const auto& j = source.at("actorControl"); auto& c = sequence.actorControl;
        c.enabled = j.value("enabled", c.enabled); c.stopMovement = j.value("stopMovement", c.stopMovement);
        c.faceTarget = j.value("faceTarget", c.faceTarget); c.aimLockTime = j.value("aimLockTime", c.aimLockTime);
        c.modelYawOffset = j.value("modelYawOffset", c.modelYawOffset); c.telegraph = j.value("telegraph", c.telegraph);
        c.telegraphEnd = j.value("telegraphEnd", c.telegraphEnd); c.telegraphWidth = j.value("telegraphWidth", c.telegraphWidth);
        c.cueTime = j.value("cueTime", c.cueTime); c.tint = j.value("tint", c.tint);
        c.color = j.value("color", c.color); c.telegraphColor = j.value("telegraphColor", c.telegraphColor);
    }
    sequence.projectileStyle = source.value("projectileStyle", std::string("fire"));
    sequence.visualScale = source.value("visualScale", 1.2f);
    sequence.fireAppearance = {};
    if (source.contains("fireAppearance")) {
        const auto& appearance = source.at("fireAppearance");
        auto& a = sequence.fireAppearance;
        if (appearance.contains("color")) {
            const auto& color = appearance.at("color");
            a.red = color.at(0).get<float>(); a.green = color.at(1).get<float>();
            a.blue = color.at(2).get<float>(); a.alpha = color.at(3).get<float>();
        }
        a.emissive = appearance.value("emissive", a.emissive);
        a.effectScale = appearance.value("effectScale", a.effectScale);
        a.softness = appearance.value("softness", a.softness);
        a.intensity = appearance.value("intensity", a.intensity);
        a.billboardScale = appearance.value("billboardScale", a.billboardScale);
        a.trailInterval = appearance.value("trailInterval", a.trailInterval);
        a.trailSpeedScale = appearance.value("trailSpeedScale", a.trailSpeedScale);
    }
    sequence.events.clear();
    for (const auto& item : source.value("events", json::array())) {
        AttackSequenceEvent event;
        event.name = item.value("name", std::string{});
        event.enabled = item.value("enabled", true);
        if (item.contains("projectile")) event.projectile = ReadProjectile(item.at("projectile"));
        event.time = item.value("time", 0.0f);
        event.operation = item.value("operation", std::string("projectile"));
        event.condition = item.value("condition", std::string("always"));
        event.stopOnFalse = item.value("stopOnFalse", false);
        const engine::Vector3 offset = ReadVector3(item, "offset", {0, 1.05f, 0.65f});
        event.right = offset.x; event.up = offset.y; event.forward = offset.z;
        const engine::Vector3 sizeOffset = ReadVector3(item, "sizeOffset", {0, 0, 0});
        event.sizeRight = sizeOffset.x; event.sizeUp = sizeOffset.y; event.sizeForward = sizeOffset.z;
        event.yawDegrees = item.value("yawDegrees", 0.0f);
        event.aimAtTarget = item.value("aimAtTarget", true);
        event.targetHeight = item.value("targetHeight", 0.35f);
        event.distanceSpeedScale = item.value("distanceSpeedScale", 1.65f);
        event.distanceSpeedBias = item.value("distanceSpeedBias", 0.0f);
        event.verticalSpeed = item.value("verticalSpeed", 0.0f);
        event.count = item.value("count", 1);
        event.spreadDegrees = item.value("spreadDegrees", 0.0f);
        event.preset = item.value("preset", std::string{});
        event.directed = item.value("directed", false);
        event.particleSpeedScale = item.value("particleSpeedScale", 1.0f);
        sequence.events.push_back(event);
    }
}

// 既定攻撃へJSONで明示された項目を重ね、未指定項目は既定値を保ちます。
void MergeAttackJson(const json& source, EnemyAttackDefinition& attack) {
    // 既定攻撃へ設定済みの項目だけ上書きし、古いJSONの欠損項目は既定値を残す。
    attack.selectionWeight = source.value("selectionWeight", 1.0f);
    if (!std::isfinite(attack.selectionWeight) || attack.selectionWeight < 0 || attack.selectionWeight > 100000)
        throw std::runtime_error("攻撃の抽選重みが不正です。");
    attack.displayName = ReadString(source, "displayName", attack.displayName);
    if (source.contains("sequence")) ReadSequence(source.at("sequence"), attack.sequence);

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

// ランタイムと編集画面が共有する攻撃定義を階層JSONへ出力します。
json WriteAttackJson(const EnemyAttackDefinition& attack) {
    // 編集画面とランタイムが参照する項目を同じ階層構造で保存する。
    return {{"id", attack.id}, {"selectionWeight", attack.selectionWeight},
            {"sequence", WriteSequence(attack.sequence)},
            {"displayName", attack.displayName},
            {"targeting",
             {{"minRange", attack.minRange},
              {"maxRange", attack.maxRange},
              {"radius", attack.radius},
              {"shape", attack.hitShape},
              {"offset", WriteVector3(attack.hitOffset)},
              {"size", WriteVector3(attack.hitSize)}}},
            {"timing",
             {{"windup", attack.windupDuration},
              {"active", attack.activeDuration},
              {"recovery", attack.recoveryDuration},
              {"cooldown", attack.cooldown},
              {"warningLead", attack.warningLeadTime},
              {"cancelStart", attack.cancelWindowStart},
              {"cancelEnd", attack.cancelWindowEnd}}},
            {"combat",
             {{"damage", attack.damage},
              {"knockback", WriteVector3(attack.knockbackVelocity)},
              {"invincibility", attack.invincibilityDuration},
              {"statusEffect",
               {{"type", attack.statusEffectType},
                {"duration", attack.statusDuration},
                {"tickInterval", attack.statusTickInterval},
                {"tickDamage", attack.statusTickDamage},
                {"vfx", attack.statusVfx}}}}},
            {"movement", {{"minSpeed", attack.minSpeed}, {"maxSpeed", attack.maxSpeed}, {"lifetime", attack.lifetime}}},
            {"preview", {{"targetDistance", attack.recommendedTargetDistance}, {"duration", attack.previewDuration}}},
            {"presentation",
             {{"animation", attack.animation},
              {"windupVfx", attack.windupVfx},
              {"activeVfx", attack.activeVfx},
              {"impactVfx", attack.impactVfx},
              {"audioCue", attack.audioCue},
              {"feedbackCue", attack.feedbackCue}}}};
}

// アセットがない場合にも使う攻撃定義を、共通の既定値から構築します。
EnemyAttackDefinition MakeAttack(const char* id, const char* displayName, float minRange, float maxRange, float radius,
                                 float windup, float active, float recovery, float cooldown, float warningLead,
                                 float damage, float minSpeed, float maxSpeed, float lifetime, float previewDistance,
                                 float previewDuration) {
    // JSONがない場合にも既存の敵攻撃を使えるよう、共通の既定値を組み立てる。
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
} // namespace

bool EnemyAttackProfile::SaveSequenceAsset(const std::string& path, const AttackSequenceDefinition& sequence, std::string* error) {
    // 検証済みの一時ファイルを書き切ってから置換し、保存途中の元ファイル破損を避ける。
    try {
        auto content = sequence;
        content.enabled = true;
        content.assetPath.clear();
        if (!content.Validate(error)) return false;
        const std::filesystem::path destination(std::u8string(path.begin(), path.end()));
        if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
        auto temporary = destination;
        temporary += L".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file << json({{"version", 1}, {"sequence", WriteSequence(content)}}).dump(2) << '\n';
            file.flush();
            if (!file) throw std::runtime_error("攻撃アセットの書き込みに失敗しました。");
        }
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("攻撃アセットの置換に失敗しました。元ファイルは保持しています。");
        if (error) error->clear();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool EnemyAttackProfile::LoadSequenceAsset(const std::string& path, AttackSequenceDefinition& sequence, std::string* error) {
    // アセットの入れ子参照を禁じ、呼び出し元の結び付けと優先度は読み込み後に維持する。
    try {
        std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        if (!file) throw std::runtime_error("攻撃アセットを開けません: " + path);
        json root;
        file >> root;
        if (root.at("version").get<int>() != 1) throw std::runtime_error("未対応の攻撃アセットバージョンです。");
        const auto& source = root.at("sequence");
        if (!source.value("assetPath", std::string{}).empty()) throw std::runtime_error("攻撃アセットの入れ子参照はできません。");
        AttackSequenceDefinition loaded;
        ReadSequence(source, loaded);
        loaded.enabled = true;
        if (!loaded.Validate(error)) return false;
        loaded.assetPath = path;
        loaded.binding = sequence.binding;
        loaded.priority = sequence.priority;
        sequence = std::move(loaded);
        if (error) error->clear();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

float EnemyAttackDefinition::GetPhaseDuration(AttackAbilityPhase phase) const {
    // 未設定や負値の秒数は0として扱い、段階遷移が逆行しないようにする。
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

float EnemyAttackDefinition::GetPhaseProgressFromElapsed(AttackAbilityPhase phase, float elapsed) const {
    // 長さ0の段階でも除算できるよう、計算用の下限だけ設ける。
    const float duration = (std::max)(0.01f, GetPhaseDuration(phase));
    return std::clamp(elapsed / duration, 0.0f, 1.0f);
}

float EnemyAttackDefinition::GetPhaseProgressFromRemaining(AttackAbilityPhase phase, float remaining) const {
    // 残り秒数から予兆描画用の進行率へ変換する。
    const float duration = (std::max)(0.01f, GetPhaseDuration(phase));
    return 1.0f - std::clamp(remaining / duration, 0.0f, 1.0f);
}

float EnemyAttackDefinition::GetActionDuration() const {
    // 再使用待ちは攻撃動作の長さに含めず、予備動作から硬直までを合計する。
    return (std::max)(0.0f, windupDuration) + (std::max)(0.0f, activeDuration) + (std::max)(0.0f, recoveryDuration);
}

float EnemyAttackDefinition::GetTotalDuration() const {
    // プレビュー1周の長さには、攻撃後の再使用待ちも含める。
    return GetActionDuration() + (std::max)(0.0f, cooldown);
}

bool EnemyAttackDefinition::IsCancelWindowOpen(float elapsedActionTime) const {
    // 開始と終了が同じ場合は窓を無効とし、入力の境界時刻だけ両端を含める。
    const float start = (std::max)(0.0f, cancelWindowStart);
    const float end = (std::max)(start, cancelWindowEnd);
    return end > start && elapsedActionTime >= start && elapsedActionTime <= end;
}

bool EnemyAttackDefinition::HasConfiguredKnockback() const {
    // ほぼゼロの設定は旧処理のノックバック値に任せる。
    return engine::Math::Length(knockbackVelocity) > 0.001f;
}

engine::Vector3 EnemyAttackDefinition::ResolveKnockback(const engine::Vector3& forward, const engine::Vector3& legacyFallback) const {
    if (!HasConfiguredKnockback()) {
        return legacyFallback;
    }

    // 設定値の X/Z は敵の右/前を基準にした成分。空中へ向いた forward は水平化してから変換する。
    engine::Vector3 planarForward = {forward.x, 0.0f, forward.z};
    if (engine::Math::Length(planarForward) <= 0.001f) {
        planarForward = {0.0f, 0.0f, 1.0f};
    } else {
        planarForward = engine::Math::Normalize(planarForward);
    }
    const engine::Vector3 right = {planarForward.z, 0.0f, -planarForward.x};
    return right * knockbackVelocity.x + engine::Vector3{0.0f, knockbackVelocity.y, 0.0f} + planarForward * knockbackVelocity.z;
}

float EnemyAttackDefinition::ResolveInvincibilityDuration(float legacyFallback) const {
    // 個別設定が0以下なら従来の被弾無敵時間を維持する。
    return invincibilityDuration > 0.0f ? invincibilityDuration : (std::max)(0.0f, legacyFallback);
}

EnemyAttackProfile EnemyAttackProfile::CreateDefault(const std::string& type) {
    EnemyAttackProfile profile;
    profile.version = kCurrentVersion;
    profile.enemyType = type;

    if (type == "Slime") {
        profile.displayName = "通常スライム";
        auto attack = MakeAttack("dive_slam", "溜めジャンプ急降下", 0.35f, 20.0f, 1.35f, 1.35f, 0.46f, 0.24f, 0.0f,
                                 0.18f, 1.0f, 22.0f, 40.0f, 0.0f, 6.0f, 4.6f);
        attack.windupVfx = "Resources/json/effect/effect_pink_slime_charge_pulse_ring.json";
        attack.activeVfx = "Resources/json/effect/effect_pink_slime_dive_streak.json";
        attack.impactVfx = "Resources/json/effect/effect_pink_slime_landing_burst_ring.json";
        profile.attacks.push_back(std::move(attack));
    } else if (type == "FireSlime") {
        profile.displayName = "炎スライム";
        auto breath = MakeAttack("flame_breath", "近距離炎ブレス", 0.0f, 4.3f, 1.65f, 0.0f, 0.62f, 0.0f, 1.55f, 0.18f,
                                 1.0f, 0.0f, 0.0f, 0.0f, 3.2f, 3.0f);
        breath.activeVfx = "fire_slime_breath";
        breath.statusEffectType = "burning";
        breath.statusDuration = 2.2f;
        breath.statusTickInterval = 0.55f;
        breath.statusTickDamage = 0.12f;
        breath.statusVfx = "status_burning_flame";
        profile.attacks.push_back(std::move(breath));

        auto fireball = MakeAttack("fireball", "中距離火球", 6.2f, 20.0f, 0.5f, 0.42f, 0.0f, 0.38f, 1.45f, 0.18f, 1.0f,
                                   15.0f, 28.0f, 2.65f, 8.0f, 3.35f);
        fireball.activeVfx = "fire_slime_cast";
        fireball.statusEffectType = "burning";
        fireball.statusDuration = 2.8f;
        fireball.statusTickInterval = 0.55f;
        fireball.statusTickDamage = 0.16f;
        fireball.statusVfx = "status_burning_flame";
        profile.attacks.push_back(std::move(fireball));
        // 古いJSONでもコピー火球の威力・入力感を保つための移行時既定値です。
        auto playerFireball = MakeAttack("player_fireball", "プレイヤー炎火球", 0, 40, 0.52f, 0, 0, 0.58f, 0.58f, 0,
            14, 28, 28, 2.65f, 8, 3.35f);
        playerFireball.animation = "player_ability_fireball";
        playerFireball.statusEffectType = "burning";
        playerFireball.statusDuration = 2.8f;
        playerFireball.statusTickInterval = 0.55f;
        playerFireball.statusTickDamage = 0.16f;
        playerFireball.statusVfx = "status_burning_flame";
        playerFireball.sequence.enabled = true;
        playerFireball.sequence.binding = "player_special";
        playerFireball.sequence.duration = 0.58f;
        playerFireball.sequence.visualScale = 1.28f;
        AttackSequenceEvent shot;
        shot.up = 1.35f; shot.forward = 1.75f;
        shot.aimAtTarget = false; shot.distanceSpeedScale = 0; shot.verticalSpeed = 1.4f;
        playerFireball.sequence.events.push_back(shot);
        shot.operation = "gpu_vfx"; shot.preset = "fire_slime_cast";
        playerFireball.sequence.events.push_back(shot);
        profile.attacks.push_back(std::move(playerFireball));
    } else if (type == "ThunderSlime") {
        profile.displayName = "雷スライム";
        auto attack = MakeAttack("radial_shock", "チャージ放電", 0.0f, 4.1f, 5.2f, 0.52f, 0.0f, 0.34f, 2.35f, 0.18f,
                                 1.0f, 0.0f, 0.0f, 0.0f, 3.2f, 3.2f);
        attack.windupVfx = "thunder_slime_radial_charge";
        attack.activeVfx = "thunder_slime_discharge";
        profile.attacks.push_back(std::move(attack));

        auto lineLightning = MakeAttack("line_lightning", "中距離連続落雷", 5.8f, 15.5f, 1.35f, 0.65f, 0.13f, 0.44f,
                                        3.2f, 0.22f, 1.0f, 0.0f, 0.0f, 0.0f, 9.0f, 4.0f);
        lineLightning.windupVfx = "thunder_slime_charge";
        lineLightning.activeVfx = "player_thunder_strike_impact";
        lineLightning.impactVfx = "Resources/json/effect/effect_thunder_scorch_mark.json";
        profile.attacks.push_back(std::move(lineLightning));
    } else if (type == "WindSlime") {
        profile.displayName = "風スライム";
        auto gust = MakeAttack("gust_breath", "暴風ブレス", 0.0f, 7.2f, 2.4f, 0.38f, 1.55f, 0.34f, 2.05f, 0.18f, 0.0f,
                               0.0f, 0.0f, 0.0f, 4.2f, 4.4f);
        gust.animation = "wind_breath_squash";
        gust.windupVfx = "wind_slime_charge";
        gust.activeVfx = "wind_slime_breath_stream";
        gust.impactVfx = "Resources/json/effect/effect_wind_gust_ring.json";
        gust.audioCue = "wind_breath_loop";
        profile.attacks.push_back(std::move(gust));

        auto volley = MakeAttack("aerial_wind_volley", "空中三連風弾", 6.2f, 18.0f, 0.58f, 0.62f, 1.60f, 0.46f, 2.45f,
                                 0.20f, 1.0f, 14.0f, 22.0f, 2.8f, 9.5f, 5.0f);
        volley.animation = "aerial_wind_orb_volley";
        volley.windupVfx = "wind_slime_orb_hold";
        volley.activeVfx = "wind_slime_orb_trail";
        volley.impactVfx = "wind_slime_orb_impact";
        volley.audioCue = "wind_orb_volley";
        profile.attacks.push_back(std::move(volley));
    } else if (type == "PrismSlime") {
        profile.displayName = "プリズムスライム";

        auto spikes = MakeAttack("crystal_spikes", "晶槍召喚陣", 2.0f, 12.0f, 3.6f, 0.88f, 0.82f, 0.62f, 2.9f, 0.28f,
                                 1.0f, 0.0f, 0.0f, 0.0f, 7.5f, 4.8f);
        spikes.animation = "prism_spike_summon";
        spikes.windupVfx = "prism_spike_warning";
        spikes.activeVfx = "prism_spike_burst";
        spikes.impactVfx = "prism_spike_shatter";
        profile.attacks.push_back(std::move(spikes));

        auto crystalVolley = MakeAttack("crystal_lance_volley", "浮遊晶槍斉射", 4.0f, 22.0f, 0.85f, 0.95f, 0.85f, 0.55f,
                                        3.8f, 0.24f, 1.0f, 18.0f, 24.0f, 2.4f, 10.0f, 5.0f);
        crystalVolley.animation = "crystal_lance_volley";
        crystalVolley.windupVfx = "prism_slime_charge";
        crystalVolley.activeVfx = "prism_lance_trail";
        crystalVolley.impactVfx = "prism_spike_shatter";
        profile.attacks.push_back(std::move(crystalVolley));

        auto fireFan = MakeAttack("fire_fan", "炎の扇状連射", 4.0f, 18.0f, 0.48f, 0.65f, 0.52f, 0.48f, 2.9f, 0.20f,
                                  1.0f, 13.0f, 28.0f, 2.8f, 9.0f, 4.2f);
        fireFan.animation = "fire_fan_recoil";
        fireFan.windupVfx = "fire_slime_cast";
        fireFan.activeVfx = "fire_slime_breath_embers";
        fireFan.statusEffectType = "burning";
        fireFan.statusDuration = 2.4f;
        fireFan.statusTickInterval = 0.55f;
        fireFan.statusTickDamage = 0.14f;
        fireFan.statusVfx = "status_burning_flame";
        profile.attacks.push_back(std::move(fireFan));

        auto thunder = MakeAttack("thunder_chain", "直線連続落雷", 4.0f, 18.0f, 1.42f, 0.76f, 0.62f, 0.48f, 3.15f,
                                  0.23f, 1.0f, 0.0f, 0.0f, 0.0f, 10.0f, 4.4f);
        thunder.animation = "thunder_chain_tremble";
        thunder.windupVfx = "thunder_slime_charge";
        thunder.activeVfx = "player_thunder_strike_impact";
        thunder.impactVfx = "Resources/json/effect/effect_thunder_scorch_mark.json";
        profile.attacks.push_back(std::move(thunder));

        auto wind = MakeAttack("wind_wave", "持続する暴風波", 0.0f, 10.5f, 3.1f, 0.58f, 1.12f, 0.46f, 2.75f, 0.20f,
                               0.65f, 0.0f, 0.0f, 0.0f, 6.8f, 4.3f);
        wind.animation = "wind_wave_stretch";
        wind.windupVfx = "wind_slime_charge";
        wind.activeVfx = "wind_slime_breath_stream";
        wind.impactVfx = "Resources/json/effect/effect_wind_gust_ring.json";
        profile.attacks.push_back(std::move(wind));

        auto summon = MakeAttack("slime_summon", "彩色スライム召喚陣", 0.0f, 30.0f, 6.0f, 1.25f, 0.72f, 0.55f, 5.5f,
                                 0.28f, 0.0f, 0.0f, 0.0f, 0.0f, 8.0f, 5.4f);
        summon.animation = "slime_summon_cast";
        summon.windupVfx = "prism_slime_charge";
        summon.activeVfx = "prism_slime_pulse";
        profile.attacks.push_back(std::move(summon));
    } else if (type == "FalseKingSlime") {
        profile.displayName = "偽王スライム";

        auto lanceRain = MakeAttack("crown_lance_rain", "王冠晶槍雨", 0.0f, 34.0f, 2.15f, 1.15f, 2.65f, 0.68f, 3.25f,
                                    0.34f, 0.90f, 0.0f, 0.0f, 3.0f, 11.0f, 6.8f);
        lanceRain.animation = "crown_lance_cast";
        lanceRain.windupVfx = "false_king_charge";
        lanceRain.activeVfx = "false_king_lance_trail";
        lanceRain.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(lanceRain));

        auto shockwave = MakeAttack("royal_shockwave", "王威連環波", 0.0f, 30.0f, 0.95f, 0.96f, 2.95f, 0.72f, 3.55f,
                                    0.30f, 0.72f, 0.0f, 0.0f, 3.0f, 10.0f, 6.5f);
        shockwave.animation = "royal_shockwave_slam";
        shockwave.windupVfx = "false_king_charge";
        shockwave.activeVfx = "false_king_shockwave";
        shockwave.impactVfx = "false_king_shockwave";
        profile.attacks.push_back(std::move(shockwave));

        auto rush = MakeAttack("king_rush", "覇王三連突進", 3.0f, 27.0f, 2.65f, 0.86f, 2.75f, 0.88f, 3.75f, 0.28f,
                               1.25f, 22.0f, 34.0f, 2.8f, 13.0f, 6.5f);
        rush.animation = "king_rush_squash";
        rush.windupVfx = "false_king_charge";
        rush.activeVfx = "false_king_rush_wake";
        rush.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(rush));

        auto cross = MakeAttack("royal_cross", "王冠十字掃射", 0.0f, 32.0f, 1.45f, 1.12f, 3.55f, 0.78f, 4.35f, 0.36f,
                                0.62f, 0.0f, 0.0f, 3.6f, 12.0f, 7.2f);
        cross.animation = "royal_cross_cast";
        cross.windupVfx = "false_king_charge";
        cross.activeVfx = "false_king_dominion";
        cross.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(cross));

        auto dominion = MakeAttack("crown_dominion", "王冠領域・終幕", 0.0f, 34.0f, 1.55f, 1.68f, 5.60f, 1.12f, 7.20f,
                                   0.48f, 0.58f, 0.0f, 0.0f, 5.7f, 14.0f, 10.0f);
        dominion.animation = "crown_dominion_cast";
        dominion.windupVfx = "false_king_dominion";
        dominion.activeVfx = "false_king_dominion";
        dominion.impactVfx = "false_king_lance_impact";
        profile.attacks.push_back(std::move(dominion));

        auto umbraFlame = MakeAttack("umbra_flame", "双極冥炎陣", 0.0f, 34.0f, 1.75f, 2.25f, 2.65f, 0.82f, 5.80f, 0.42f,
                                     1.05f, 0.0f, 0.0f, 2.8f, 14.0f, 8.4f);
        umbraFlame.animation = "crown_dominion_cast";
        umbraFlame.windupVfx = "false_king_umbra_flame_charge";
        umbraFlame.activeVfx = "false_king_umbra_flame_flame";
        umbraFlame.impactVfx = "false_king_umbra_flame_flame";
        profile.attacks.push_back(std::move(umbraFlame));
    } else if (type == "GiantSlime") {
        profile.displayName = "巨大スライム";
        auto attack = MakeAttack("jump_press", "ジャンププレス", 4.0f, 18.0f, 7.0f, 0.78f, 0.0f, 1.15f, 1.15f, 0.18f,
                                 2.0f, 0.0f, 0.0f, 0.0f, 9.0f, 5.0f);
        profile.attacks.push_back(std::move(attack));
    } else {
        profile.displayName = type;
    }

    return profile;
}

std::string EnemyAttackProfile::GetDefaultPath(const std::string& type) {
    // C++の敵タイプ名をスネークケースへ変換し、既定の攻撃JSON名と揃える。
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

bool EnemyAttackProfile::LoadCachedForEnemy(const std::string& type, EnemyAttackProfile& destination,
                                            std::string* errorMessage) {
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
    if (loadedFromFile) {
        std::lock_guard<std::mutex> lock(gProfileCacheMutex);
        // 読込失敗を成功扱いでキャッシュすると、JSONを修正しても再読込されません。
        gProfileCache[type] = loadedProfile;
    }
    destination = std::move(loadedProfile);
    return loadedFromFile;
}

void EnemyAttackProfile::InvalidateCache(const std::string& type) {
    // 空文字は全件再読込の指示とし、エディターの保存後に古い定義を使わない。
    std::lock_guard<std::mutex> lock(gProfileCacheMutex);
    if (type.empty()) {
        gProfileCache.clear();
    } else {
        gProfileCache.erase(type);
    }
}

bool EnemyAttackProfile::LoadForEnemy(const std::string& type, std::string* errorMessage) {
    // 読み込み前に既定攻撃を用意し、ファイル欠損時も呼び出し元が使える状態を保つ。
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

// 攻撃プロファイルを読み込んだ後に値を正規化し、欠損した調整値を安全な範囲へ収めます。
bool EnemyAttackProfile::LoadFromFile(const std::string& path, std::string* errorMessage) {
    // JSONの攻撃IDで既定値と照合し、検証が済んでからプロフィール全体を反映する。
    try {
        std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
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
        merged.selectionMode = root.value("selectionMode", "priority");
        if (merged.selectionMode != "priority" && merged.selectionMode != "uniform" && merged.selectionMode != "weighted")
            throw std::runtime_error("未対応の攻撃選択方式です。");

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

        for (const auto& attack : merged.attacks) {
            std::string reason;
            if (!attack.sequence.Validate(&reason)) {
                if (errorMessage) *errorMessage = attack.id + ": " + reason;
                return false;
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
    // 全攻撃を検証し、一時ファイルへの書き込み成功後に元JSONを置き換える。
    try {
        if (selectionMode != "priority" && selectionMode != "uniform" && selectionMode != "weighted")
            throw std::runtime_error("未対応の攻撃選択方式です。");
        for (const auto& attack : attacks) {
            if (!std::isfinite(attack.selectionWeight) || attack.selectionWeight < 0 || attack.selectionWeight > 100000)
                throw std::runtime_error("攻撃の抽選重みが不正です。");
            std::string reason;
            if (!attack.sequence.Validate(&reason)) {
                if (errorMessage) *errorMessage = attack.id + ": " + reason;
                return false;
            }
        }
        const std::filesystem::path outputPath(std::u8string(path.begin(), path.end()));
        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }

        json root;
        root["version"] = kCurrentVersion;
        root["enemyType"] = enemyType;
        root["selectionMode"] = selectionMode;
        root["displayName"] = displayName;
        root["attacks"] = json::array();
        for (const EnemyAttackDefinition& attack : attacks) {
            root["attacks"].push_back(WriteAttackJson(attack));
        }

        auto temporaryPath = outputPath;
        temporaryPath += L".tmp";
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            if (errorMessage) {
                *errorMessage = "プロファイルを書き込めません: " + path;
            }
            return false;
        }
        file << root.dump(2) << '\n';
        file.flush();
        if (!file.good()) {
            if (errorMessage) {
                *errorMessage = "プロファイルの書き込み中にエラーが発生しました: " + path;
            }
            return false;
        }
        file.close();
        if (!file || !MoveFileExW(temporaryPath.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("プロファイルの置換に失敗しました。元ファイルは保持しています。");
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
    // IDをキーに検索し、未登録なら呼び出し側が代替動作を選べるようnullptrを返す。
    const auto it = std::find_if(attacks.begin(), attacks.end(),
                                 [&](const EnemyAttackDefinition& attack) { return attack.id == attackId; });
    return it != attacks.end() ? &*it : nullptr;
}

EnemyAttackDefinition* EnemyAttackProfile::FindAttack(const std::string& attackId) {
    // エディターの更新対象をIDで特定し、配列内の同じ定義を返す。
    const auto it = std::find_if(attacks.begin(), attacks.end(),
                                 [&](const EnemyAttackDefinition& attack) { return attack.id == attackId; });
    return it != attacks.end() ? &*it : nullptr;
}

const EnemyAttackDefinition* EnemyAttackProfile::SelectSequence(float distance, float detectionRange,
    const std::string& previewId, float roll) const {
    // 距離と結び付け条件で候補を絞り、プレビュー指定または優先度・抽選方式で選ぶ。
    if (!std::isfinite(distance) || distance < 0 || distance > detectionRange) return nullptr;
    std::vector<const EnemyAttackDefinition*> candidates;
    const EnemyAttackDefinition* best = nullptr;
    float total = 0;
    for (const auto& attack : attacks) {
        if (!attack.sequence.enabled || attack.sequence.binding != "enemy" || distance < attack.minRange || distance > attack.maxRange) continue;
        if (!previewId.empty()) { if (attack.id == previewId) return &attack; else continue; }
        if (selectionMode == "weighted" && attack.selectionWeight <= 0) continue;
        candidates.push_back(&attack);
        total += selectionMode == "weighted" ? attack.selectionWeight : 1;
        if (!best || attack.sequence.priority > best->sequence.priority) best = &attack;
    }
    if (selectionMode == "priority" || candidates.empty()) return best;
    if (roll < 0) {
        static thread_local std::mt19937 random{std::random_device{}()};
        roll = std::generate_canonical<float, 24>(random);
    }
    if (!std::isfinite(roll)) return nullptr;
    float ticket = std::clamp(roll, 0.0f, 1.0f) * total;
    for (const auto* attack : candidates) {
        ticket -= selectionMode == "weighted" ? attack->selectionWeight : 1;
        if (ticket < 0) return attack;
    }
    return candidates.back();
}

void EnemyAttackProfile::Sanitize() {
    auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    std::unordered_set<std::string> usedIds;
    // Editor上で重複IDを残すと、表示された攻撃と実行される攻撃が食い違います。
    attacks.erase(std::remove_if(attacks.begin(), attacks.end(),
                                 [&](const EnemyAttackDefinition& attack) {
                                     return attack.id.empty() || !usedIds.insert(attack.id).second;
                                 }),
                  attacks.end());
    for (EnemyAttackDefinition& attack : attacks) {
        // Editor入力の非有限値は std::clamp だけでは除去できないため、先に既定値へ戻します。
        attack.minRange = finiteOr(attack.minRange, 0.0f);
        attack.maxRange = finiteOr(attack.maxRange, 10.0f);
        attack.radius = finiteOr(attack.radius, 1.0f);
        attack.hitOffset = {finiteOr(attack.hitOffset.x, 0.0f), finiteOr(attack.hitOffset.y, 0.0f),
                            finiteOr(attack.hitOffset.z, 0.0f)};
        attack.hitSize = {finiteOr(attack.hitSize.x, 1.0f), finiteOr(attack.hitSize.y, 1.0f),
                          finiteOr(attack.hitSize.z, 1.0f)};
        attack.windupDuration = finiteOr(attack.windupDuration, 0.3f);
        attack.activeDuration = finiteOr(attack.activeDuration, 0.2f);
        attack.recoveryDuration = finiteOr(attack.recoveryDuration, 0.3f);
        attack.cooldown = finiteOr(attack.cooldown, 1.0f);
        attack.warningLeadTime = finiteOr(attack.warningLeadTime, 0.15f);
        attack.cancelWindowStart = finiteOr(attack.cancelWindowStart, 0.0f);
        attack.cancelWindowEnd = finiteOr(attack.cancelWindowEnd, 0.0f);
        attack.damage = finiteOr(attack.damage, 1.0f);
        attack.knockbackVelocity = {finiteOr(attack.knockbackVelocity.x, 0.0f),
                                    finiteOr(attack.knockbackVelocity.y, 0.0f),
                                    finiteOr(attack.knockbackVelocity.z, 0.0f)};
        attack.invincibilityDuration = finiteOr(attack.invincibilityDuration, 0.0f);
        attack.statusDuration = finiteOr(attack.statusDuration, 0.0f);
        attack.statusTickInterval = finiteOr(attack.statusTickInterval, 0.5f);
        attack.statusTickDamage = finiteOr(attack.statusTickDamage, 0.0f);
        attack.minSpeed = finiteOr(attack.minSpeed, 0.0f);
        attack.maxSpeed = finiteOr(attack.maxSpeed, 0.0f);
        attack.lifetime = finiteOr(attack.lifetime, 0.0f);
        attack.recommendedTargetDistance = finiteOr(attack.recommendedTargetDistance, 5.0f);
        attack.previewDuration = finiteOr(attack.previewDuration, 4.0f);

        attack.minRange = (std::max)(0.0f, attack.minRange);
        attack.maxRange = (std::max)(attack.minRange, attack.maxRange);
        attack.radius = (std::max)(0.01f, attack.radius);
        if (attack.hitShape == "Sphere")
            attack.hitShape = "sphere";
        else if (attack.hitShape == "Capsule")
            attack.hitShape = "capsule";
        else if (attack.hitShape == "Box")
            attack.hitShape = "box";
        else if (attack.hitShape == "Cone")
            attack.hitShape = "cone";
        if (attack.hitShape != "sphere" && attack.hitShape != "capsule" && attack.hitShape != "box" &&
            attack.hitShape != "cone") {
            attack.hitShape = "sphere";
        }
        attack.hitSize.x = (std::max)(0.01f, std::abs(attack.hitSize.x));
        attack.hitSize.y = (std::max)(0.01f, std::abs(attack.hitSize.y));
        attack.hitSize.z = (std::max)(0.01f, std::abs(attack.hitSize.z));
        attack.windupDuration = (std::max)(0.0f, attack.windupDuration);
        attack.activeDuration = (std::max)(0.0f, attack.activeDuration);
        attack.recoveryDuration = (std::max)(0.0f, attack.recoveryDuration);
        attack.cooldown = (std::max)(0.0f, attack.cooldown);
        attack.warningLeadTime =
            std::clamp(attack.warningLeadTime, 0.0f, attack.windupDuration + attack.activeDuration);
        const float actionDuration = attack.GetActionDuration();
        attack.cancelWindowStart = std::clamp(attack.cancelWindowStart, 0.0f, actionDuration);
        attack.cancelWindowEnd = std::clamp(attack.cancelWindowEnd, attack.cancelWindowStart, actionDuration);
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
