#include "game/actor/enemy/boss/core/MidbossEncounterEffects.h"

#include "game/actor/enemy/core/EnemySettingsPaths.h"
#include "json.hpp"

#include <fstream>

namespace {

// 欠損・不正値では従来のVFX名を使い、他の設定項目の読み込みは続ける。
void ReadSequence(const nlohmann::json& settings, const char* key, std::string& sequence) {
    const auto it = settings.find(key);
    if (it == settings.end() || !it->is_string()) {
        return;
    }

    const auto& name = it->get_ref<const std::string&>();
    if (!name.empty() && name.find_first_of(R"(/\:)") == std::string::npos) {
        sequence = name;
    }
}

MidbossEncounterEffects LoadEncounterEffects(const char* enemyType, const char* prefix) {
    const std::string name = prefix;
    MidbossEncounterEffects effects{
        name + "_arena_seal_cue", name + "_midboss_appear_cue", name + "_arena_release_cue"
    };

    std::ifstream file(EnemySettingsPaths::Directory(enemyType) + "encounter.json");
    if (!file) {
        return effects;
    }

    const auto settings = nlohmann::json::parse(file, nullptr, false);
    if (settings.is_object()) {
        ReadSequence(settings, "seal", effects.seal);
        ReadSequence(settings, "appearance", effects.appearance);
        ReadSequence(settings, "release", effects.release);
    }
    return effects;
}

} // namespace

const MidbossEncounterEffects& MidbossEncounterEffects::Prism() {
    static const auto effects = LoadEncounterEffects("PrismSlime", "prism");
    return effects;
}

const MidbossEncounterEffects& MidbossEncounterEffects::Magma() {
    static const auto effects = LoadEncounterEffects("MagmaSlime", "magma");
    return effects;
}
