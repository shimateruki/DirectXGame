#include "PresetEditor.h"

#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

#ifdef USE_IMGUI

// プリセットの生成、分類、検索、モデル候補の解決を担当します。
void PresetEditor::HandleDeferredDelete() {
    if (!requestDelete_ || selectedName_.empty()) {
        return;
    }

    PresetManager::GetInstance()->RemovePreset(selectedName_);
    selectedName_.clear();
    requestDelete_ = false;
}

void PresetEditor::RefreshModelList() {
    modelNames_.clear();
    const std::filesystem::path root = "Resources/3DModel";
    if (!std::filesystem::exists(root)) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".obj" && ext != ".gltf" && ext != ".glb") {
            continue;
        }

        std::filesystem::path relative = std::filesystem::relative(entry.path().parent_path(), root);
        std::string modelName = relative.generic_string();
        if (modelName == ".") {
            modelName = entry.path().stem().generic_string();
        }
        if (std::find(modelNames_.begin(), modelNames_.end(), modelName) == modelNames_.end()) {
            modelNames_.push_back(modelName);
        }
    }

    std::sort(modelNames_.begin(), modelNames_.end());
}

bool PresetEditor::MatchesSearch(const std::string& name, const json& data) const {
    std::string needle = ToLower(searchBuffer_);
    if (needle.empty()) {
        return true;
    }

    std::string haystack = name + " " +
        ReadString(data, "name", "") + " " +
        ReadString(data, "modelName", "") + " " +
        ReadString(data, "enemyType", "") + " " +
        ReadString(data, "gimmickType", "") + " " +
        ReadString(data, "itemType", "") + " " +
        ReadString(data, "presetCollection", "") + " " +
        ReadString(data, "presetSection", "") + " " +
        ReadString(data, "presetDescription", "") + " " +
        ReadString(data, "presetSourceObject", "");
    if (data.contains("param") && data["param"].is_object()) {
        const json& param = data["param"];
        haystack += " " + ReadString(param, "enemyType", "");
        haystack += " " + ReadString(param, "gimmickType", "");
        haystack += " " + ReadString(param, "itemType", "");
    }
    return ToLower(haystack).find(needle) != std::string::npos;
}

bool PresetEditor::MatchesCategory(const json& data, Category category) const {
    if (category == Category::All) {
        return true;
    }
    if (category == Category::Stage1) {
        return IsStage1Preset(data);
    }
    return DetectCategory(data) == category;
}

bool PresetEditor::IsStage1Preset(const json& data) const {
    return ReadString(data, "presetCollection", "") == "Stage1";
}

PresetEditor::Category PresetEditor::DetectCategory(const json& data) const {
    std::string type = ReadString(data, "type", "");
    if (type == "Enemy") return Category::Enemy;
    if (type == "Gimmick") return Category::Gimmick;
    if (type == "Item") return Category::Item;
    if (type == "Model") return Category::Model;

    if (!ReadString(data, "enemyType", "").empty()) return Category::Enemy;
    if (!ReadString(data, "gimmickType", "").empty()) return Category::Gimmick;
    if (!ReadString(data, "itemType", "").empty()) return Category::Item;

    if (data.contains("param") && data["param"].is_object()) {
        const json& param = data["param"];
        if (!ReadString(param, "enemyType", "").empty()) return Category::Enemy;
        if (!ReadString(param, "gimmickType", "").empty()) return Category::Gimmick;
        if (!ReadString(param, "itemType", "").empty()) return Category::Item;
    }

    return Category::Model;
}

const char* PresetEditor::GetCategoryLabel(Category category) const {
    switch (category) {
    case Category::Stage1: return "ステージ1";
    case Category::Enemy: return "敵";
    case Category::Gimmick: return "ギミック";
    case Category::Item: return "アイテム";
    case Category::Model: return "モデル";
    case Category::All:
    default: return "すべて";
    }
}

ImVec4 PresetEditor::GetCategoryColor(Category category) const {
    switch (category) {
    case Category::Stage1: return ImVec4(0.16f, 0.76f, 0.88f, 1.0f);
    case Category::Enemy: return ImVec4(0.78f, 0.28f, 0.24f, 1.0f);
    case Category::Gimmick: return ImVec4(0.24f, 0.50f, 0.84f, 1.0f);
    case Category::Item: return ImVec4(0.28f, 0.66f, 0.38f, 1.0f);
    case Category::Model: return ImVec4(0.56f, 0.48f, 0.78f, 1.0f);
    case Category::All:
    default: return ImVec4(0.40f, 0.42f, 0.46f, 1.0f);
    }
}

ImVec4 PresetEditor::GetStage1SectionColor(int sectionOrder) const {
    if (sectionOrder < 200) return ImVec4(0.30f, 0.84f, 0.55f, 1.0f);
    if (sectionOrder < 300) return ImVec4(0.24f, 0.70f, 1.0f, 1.0f);
    if (sectionOrder < 400) return ImVec4(0.78f, 0.52f, 1.0f, 1.0f);
    if (sectionOrder < 500) return ImVec4(1.0f, 0.76f, 0.24f, 1.0f);
    return ImVec4(0.60f, 0.78f, 0.50f, 1.0f);
}

std::string PresetEditor::ShortModelName(const std::string& modelName) const {
    if (modelName.empty()) {
        return "(なし)";
    }

    size_t slash = modelName.find_last_of("/\\");
    if (slash == std::string::npos) {
        return modelName;
    }
    return modelName.substr(slash + 1);
}

std::string PresetEditor::ReadString(const json& data, const char* key, const std::string& defaultValue) const {
    if (data.contains(key) && data[key].is_string()) {
        return data[key].get<std::string>();
    }
    return defaultValue;
}

float PresetEditor::ReadFloat(const json& data, const char* key, float defaultValue) const {
    if (data.contains(key) && data[key].is_number()) {
        return data[key].get<float>();
    }
    return defaultValue;
}

int PresetEditor::ReadInt(const json& data, const char* key, int defaultValue) const {
    if (data.contains(key) && data[key].is_number_integer()) {
        return data[key].get<int>();
    }
    if (data.contains(key) && data[key].is_number()) {
        return static_cast<int>(data[key].get<float>());
    }
    return defaultValue;
}

bool PresetEditor::ReadBool(const json& data, const char* key, bool defaultValue) const {
    if (data.contains(key) && data[key].is_boolean()) {
        return data[key].get<bool>();
    }
    return defaultValue;
}

Vector3 PresetEditor::ReadVector3(const json& data, const char* key, const Vector3& defaultValue) const {
    Vector3 value = defaultValue;
    if (data.contains(key) && data[key].is_array() && data[key].size() >= 3) {
        if (data[key][0].is_number()) value.x = data[key][0].get<float>();
        if (data[key][1].is_number()) value.y = data[key][1].get<float>();
        if (data[key][2].is_number()) value.z = data[key][2].get<float>();
    }
    return value;
}

std::string PresetEditor::ToLower(const std::string& value) const {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
        });
    return result;
}

std::vector<PresetEditor::TypeOption> PresetEditor::GetEnemyOptions() const {
    return {
        { "FireSlime", "Fire Slime" },
        { "ThunderSlime", "Thunder Slime" },
        { "WindSlime", "Wind Slime" },
        { "Slime", "スライム" },
        { "BossCore", "ボスコア" },
        { "Bomb", "ボム" },
        { "Bomber", "ボマー" },
        { "Mushroom", "キノコ" },
        { "GiantSlime", "巨大スライム" },
        { "Bat", "コウモリ" },
        { "BeamDrone", "目玉ビーム" }
    };
}

std::vector<PresetEditor::TypeOption> PresetEditor::GetGimmickOptions() const {
    return {
        { "Default", "通常" },
        { "MovingFloor", "移動床" },
        { "Trampoline", "トランポリン" },
        { "LaunchStar", "スターランチャー" },
        { "ChikuwaBlock", "ちくわブロック" },
        { "BlinkBlock", "点滅ブロック" },
        { "BreakableBlock", "破壊ブロック" },
        { "Coin", "コイン" },
        { "HookAnchor", "フックアンカー" },
        { "SinkingFloor", "沈む床" },
        { "SeesawFloor", "シーソー床" },
        { "DashPanel", "ダッシュパネル" },
        { "IceFloor", "氷の床" },
        { "TimedSwitch", "時限スイッチ床" },
        { "AppearingFloor", "出現床" },
        { "Switch", "汎用スイッチ" },
        { "EventReceiver", "イベント受信" },
        { "ArenaEncounter", "中ボス遭遇管理" },
        { "GameplayVolume", "ゲームプレイボリューム" },
        { "CopyMemoryStation", "コピー記憶台" },
        { "PrismBarrier", "プリズム障壁" },
        { "HookPullBlock", "フック可動ブロック" },
        { "OneWayFloor", "一方通行床" },
        { "LiquidLevel", "水位・マグマ上下" },
        { "MagmaHazard", "マグマダメージ床" },
        { "ChainCollapseFloor", "連鎖崩れ床" },
        { "RotatingFloor", "回転床" },
        { "RotatingPillar", "回転柱" },
        { "PhaseFlipFloor", "順番反転床" },
        { "LaserEmitter", "レーザー発生器" },
        { "LaserNode", "レーザー接続ノード" },
        { "StageGate", "ステージゲート" }
    };
}

std::vector<PresetEditor::TypeOption> PresetEditor::GetItemOptions() const {
    return {
        { "Heal", "回復アイテム" }
    };
}

#endif
