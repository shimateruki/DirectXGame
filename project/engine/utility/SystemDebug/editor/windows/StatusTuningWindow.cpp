#define NOMINMAX
#include "StatusTuningWindow.h"

#include "BaseEnemy.h"
#include "BaseScene.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "SceneManager.h"
#include "imgui.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char* kPresetPath = "Resources/json/gameplay/status_presets.json";
constexpr const char* kStageObjectDirectory = "Resources/json/3Dobject";
constexpr const char* kModelRootDirectory = "Resources/3DModel";
constexpr const char* kCharacterModelDirectory = "Resources/3DModel/Characters";
constexpr float kPlayerDefaultMaxFallSpeed = 60.0f;

StatusTuningWindow::StatusPreset MakePreset(
    float hp,
    float maxHp,
    float attackPower,
    float speed,
    float gravity,
    float jumpPower,
    float detectionRange,
    const char* modelName = "",
    bool morphLimited = true,
    float morphDuration = 5.0f) {
    StatusTuningWindow::StatusPreset preset;
    preset.hp = hp;
    preset.maxHp = maxHp;
    preset.attackPower = attackPower;
    preset.speed = speed;
    preset.gravity = gravity;
    preset.jumpPower = jumpPower;
    preset.detectionRange = detectionRange;
    preset.scale = { 0.0f, 0.0f, 0.0f };
    preset.modelName = modelName ? modelName : "";
    preset.morphLimited = morphLimited;
    preset.morphDuration = morphDuration;
    return preset;
}

json PresetToJson(const StatusTuningWindow::StatusPreset& preset) {
    return json{
        { "hp", preset.hp },
        { "maxHp", preset.maxHp },
        { "attackPower", preset.attackPower },
        { "speed", preset.speed },
        { "gravity", preset.gravity },
        { "jumpPower", preset.jumpPower },
        { "detectionRange", preset.detectionRange },
        { "scale", json::array({ preset.scale.x, preset.scale.y, preset.scale.z }) },
        { "modelName", preset.modelName },
        { "morphLimited", preset.morphLimited },
        { "morphDuration", preset.morphDuration }
    };
}

void ReadFloat(const json& source, const char* key, float& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_number()) {
        value = source.at(key).get<float>();
    }
}

void ReadBool(const json& source, const char* key, bool& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_boolean()) {
        value = source.at(key).get<bool>();
    }
}

void ReadString(const json& source, const char* key, std::string& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_string()) {
        value = source.at(key).get<std::string>();
    }
}

void ReadVector3(const json& source, const char* key, Vector3& value) {
    if (!source.is_object() || !source.contains(key) || !source.at(key).is_array()) {
        return;
    }
    const json& array = source.at(key);
    if (array.size() < 3 || !array[0].is_number() || !array[1].is_number() || !array[2].is_number()) {
        return;
    }
    value = { array[0].get<float>(), array[1].get<float>(), array[2].get<float>() };
}

bool IsStatusScaleEnabled(const StatusTuningWindow::StatusPreset& preset) {
    return preset.scale.x > 0.0f && preset.scale.y > 0.0f && preset.scale.z > 0.0f;
}

json ScaleToJson(const Vector3& scale) {
    return json::array({ scale.x, scale.y, scale.z });
}

void JsonToPreset(const json& source, StatusTuningWindow::StatusPreset& preset) {
    ReadFloat(source, "hp", preset.hp);
    ReadFloat(source, "maxHp", preset.maxHp);
    ReadFloat(source, "attackPower", preset.attackPower);
    ReadFloat(source, "speed", preset.speed);
    ReadFloat(source, "gravity", preset.gravity);
    ReadFloat(source, "jumpPower", preset.jumpPower);
    ReadFloat(source, "detectionRange", preset.detectionRange);
    ReadVector3(source, "scale", preset.scale);
    ReadString(source, "modelName", preset.modelName);
    ReadBool(source, "morphLimited", preset.morphLimited);
    ReadFloat(source, "morphDuration", preset.morphDuration);
}

StatusTuningWindow::StatusPreset ParameterToPreset(const Object3d::EntityParameter& param) {
    StatusTuningWindow::StatusPreset preset;
    preset.hp = param.hp;
    preset.maxHp = param.maxHp;
    preset.attackPower = param.attackPower;
    preset.speed = param.speed;
    preset.gravity = param.gravity;
    preset.jumpPower = param.jumpPower;
    preset.detectionRange = param.detectionRange;
    preset.morphLimited = param.morphLimited;
    preset.morphDuration = param.morphDuration;
    return preset;
}

bool JsonStringEquals(const json& source, const char* key, const char* value) {
    return source.is_object() && source.contains(key) && source.at(key).is_string() && source.at(key).get<std::string>() == value;
}

bool IsPlayerJsonObject(const json& objectData) {
    return JsonStringEquals(objectData, "type", "Player") ||
        JsonStringEquals(objectData, "name", "Player") ||
        JsonStringEquals(objectData, "saveCategory", "Player");
}

bool IsEnemyJsonObject(const json& objectData, const std::string& enemyType) {
    if (enemyType.empty() || !objectData.is_object()) {
        return false;
    }
    if (JsonStringEquals(objectData, "enemyType", enemyType.c_str())) {
        return true;
    }
    if (objectData.contains("param") && objectData["param"].is_object()) {
        return JsonStringEquals(objectData["param"], "enemyType", enemyType.c_str());
    }
    return false;
}

void ApplyPlayerPresetToJsonObject(json& objectData, const StatusTuningWindow::StatusPreset& preset) {
    objectData["type"] = "Player";
    objectData["saveCategory"] = "Player";
    if (!preset.modelName.empty()) {
        objectData["modelName"] = preset.modelName;
    }
    if (IsStatusScaleEnabled(preset)) {
        objectData["scale"] = ScaleToJson(preset.scale);
    }

    json& param = objectData["param"];
    if (!param.is_object()) {
        param = json::object();
    }

    const float hp = (std::max)(preset.hp, 0.0f);
    const float maxHp = (std::max)((std::max)(preset.maxHp, 1.0f), hp);
    param["hp"] = hp;
    param["maxHp"] = maxHp;
    param["attackPower"] = (std::max)(preset.attackPower, 0.0f);
    param["speed"] = (std::max)(preset.speed, 0.0f);
    param["gravity"] = preset.gravity;
    param["jumpPower"] = (std::max)(preset.jumpPower, 0.0f);
    if (!param.contains("maxFallSpeed")) {
        param["maxFallSpeed"] = kPlayerDefaultMaxFallSpeed;
    }
    param["detectionRange"] = (std::max)(preset.detectionRange, 0.0f);
}

void ApplyEnemyPresetToJsonObject(json& objectData, const std::string& enemyType, const StatusTuningWindow::StatusPreset& preset) {
    objectData["type"] = "Enemy";
    objectData["saveCategory"] = "Enemy";
    objectData["enemyType"] = enemyType;
    if (!preset.modelName.empty()) {
        objectData["modelName"] = preset.modelName;
    }
    if (IsStatusScaleEnabled(preset)) {
        objectData["scale"] = ScaleToJson(preset.scale);
    }

    json& param = objectData["param"];
    if (!param.is_object()) {
        param = json::object();
    }

    const float hp = (std::max)(preset.hp, 0.0f);
    const float maxHp = (std::max)((std::max)(preset.maxHp, 1.0f), hp);
    param["hp"] = hp;
    param["maxHp"] = maxHp;
    param["attackPower"] = (std::max)(preset.attackPower, 0.0f);
    param["speed"] = (std::max)(preset.speed, 0.0f);
    param["gravity"] = preset.gravity;
    param["jumpPower"] = (std::max)(preset.jumpPower, 0.0f);
    param["detectionRange"] = (std::max)(preset.detectionRange, 0.0f);
    param["enemyType"] = enemyType;
    param["morphLimited"] = preset.morphLimited;
    param["morphDuration"] = (std::max)(preset.morphDuration, 0.1f);
}

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& value) {
    if (!buffer || bufferSize == 0) {
        return;
    }
    strncpy_s(buffer, bufferSize, value.c_str(), _TRUNCATE);
}

std::string ToLowerAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool ContainsInsensitive(const std::string& text, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    return ToLowerAscii(text).find(ToLowerAscii(filter)) != std::string::npos;
}

std::string GetModelShortName(const std::string& modelName) {
    if (modelName.empty()) {
        return "未設定";
    }
    fs::path path(modelName);
    return path.filename().string();
}

std::string GetModelDisplayName(const std::string& modelName) {
    const std::string shortName = GetModelShortName(modelName);
    if (shortName == "slime_pink") return "ピンクスライム";
    if (shortName == "slime_red") return "ファイアスライム";
    if (shortName == "slime_yellow") return "サンダースライム";
    if (shortName == "slime_black") return "ブラックスライム";
    if (shortName == "slime_white") return "ホワイトスライム";
    if (shortName == "slime") return "スライム";
    if (shortName == "bomb_slime") return "ボムスライム";
    if (shortName == "bat") return "コウモリ";
    if (shortName == "eye") return "アイ";
    if (shortName == "player") return "プレイヤー";
    if (shortName == "bunny") return "バニー";
    if (shortName == "enemy_core") return "敵コア";
    if (shortName == "slimeBody") return "スライム胴体";
    if (shortName == "slimeBody_pink") return "ピンク胴体";
    if (shortName == "slimeBody_yellow") return "イエロー胴体";
    if (shortName == "slimeBody_black") return "ブラック胴体";
    if (shortName == "suzanne") return "スザンヌ";
    return shortName;
}

ImVec4 GetModelAccentColor(const std::string& modelName) {
    const std::string lower = ToLowerAscii(modelName);
    if (lower.find("pink") != std::string::npos) return ImVec4(0.95f, 0.35f, 0.70f, 1.0f);
    if (lower.find("red") != std::string::npos || lower.find("fire") != std::string::npos) return ImVec4(0.95f, 0.32f, 0.18f, 1.0f);
    if (lower.find("yellow") != std::string::npos || lower.find("thunder") != std::string::npos) return ImVec4(0.95f, 0.78f, 0.20f, 1.0f);
    if (lower.find("bat") != std::string::npos) return ImVec4(0.45f, 0.42f, 0.68f, 1.0f);
    if (lower.find("bomb") != std::string::npos) return ImVec4(0.35f, 0.35f, 0.42f, 1.0f);
    if (lower.find("eye") != std::string::npos) return ImVec4(0.25f, 0.72f, 0.86f, 1.0f);
    if (lower.find("slime") != std::string::npos) return ImVec4(0.35f, 0.78f, 0.48f, 1.0f);
    return ImVec4(0.40f, 0.58f, 0.88f, 1.0f);
}

std::string MakeModelKeyFromFile(const fs::path& modelFile) {
    fs::path relative = fs::relative(modelFile, kModelRootDirectory);
    fs::path parent = relative.parent_path();
    if (!parent.empty() && parent.filename() == modelFile.stem()) {
        return parent.generic_string();
    }
    if (!parent.empty() && parent.generic_string() != "Characters") {
        return parent.generic_string();
    }
    relative.replace_extension("");
    return relative.generic_string();
}
} // namespace

void StatusTuningWindow::Initialize(DebugEditor* editor, SceneManager* sceneManager) {
    editor_ = editor;
    sceneManager_ = sceneManager;
    ResetPresetsToDefaults();
    LoadPresets();
    RefreshCharacterModelList();
}

void StatusTuningWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.62f, 0.95f, 1.0f, 1.0f), ICON_FA_SLIDERS_H " ステータス調整");
    ImGui::TextWrapped("プレイヤーと敵のステータス、モデル、変身時間制限をまとめて調整します。");
    ImGui::TextDisabled("モデルカードを押すと Characters フォルダ内のモデル一覧からすぐ切り替えできます。ステージJSONへの反映は専用ボタンで行います。");
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_SAVE " プリセット保存")) {
        SavePresets();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " プリセット再読込")) {
        LoadPresets();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " 初期値に戻す")) {
        ResetPresetsToDefaults();
        statusText_ = "表示中のプリセット値を初期値に戻しました。保存はまだ行っていません。";
    }

    ImGui::TextWrapped("%s", statusText_.c_str());
    ImGui::Separator();

    DrawCurrentSceneSummary();
    ImGui::Separator();

    DrawSelectedObjectEditor();
    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_USER " プレイヤー一括調整", ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool modelChanged = DrawPresetEditor(playerPreset_, "PlayerPreset");
        if (modelChanged) {
            const int applied = ApplyPlayerPreset();
            statusText_ = "プレイヤーモデルを現在シーンへ即時反映しました: " + std::to_string(applied) + " 件";
            SavePresets();
        }
        if (ImGui::Button(ICON_FA_SYNC " 現在シーンのプレイヤーから取得", ImVec2(-1.0f, 0.0f))) {
            statusText_ = CapturePlayerPresetFromScene()
                ? "現在シーンのプレイヤーステータスをプリセットへ取り込みました。"
                : "現在シーンにステータスを持つプレイヤーが見つかりませんでした。";
        }
        if (ImGui::Button(ICON_FA_CHECK " 現在シーンのプレイヤー全員へ反映", ImVec2(-1.0f, 0.0f))) {
            const int applied = ApplyPlayerPreset();
            statusText_ = "プレイヤーへステータスを反映しました: " + std::to_string(applied) + " 件";
            SavePresets();
        }
        if (ImGui::Button(ICON_FA_SAVE " 全ステージJSONのプレイヤーへ反映", ImVec2(-1.0f, 0.0f))) {
            const int applied = ApplyPlayerPresetToStageFiles();
            statusText_ = "ステージJSON内のプレイヤーへ反映しました: " + std::to_string(applied) + " 件";
            SavePresets();
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS_H " 敵タイプ別一括調整", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawEnemyPresetRows();
    }
#else
    (void)editor_;
    (void)sceneManager_;
#endif
}

void StatusTuningWindow::ResetPresetsToDefaults() {
    playerPreset_ = MakePreset(100.0f, 100.0f, 1.0f, 27.7f, 50.0f, 24.0f, 20.0f, "Characters/slime", true, 5.0f);
    enemyPresets_ = {
        EnemyTypeEntry{ "Slime", "ピンクスライム", MakePreset(50.0f, 50.0f, 1.0f, 0.1f, 60.0f, 18.0f, 20.0f, "Characters/slime_pink", true, 5.0f) },
        EnemyTypeEntry{ "Bomb", "ボム", MakePreset(30.0f, 30.0f, 1.0f, 0.04f, 60.0f, 16.0f, 20.0f, "Gimmicks/blob", true, 5.0f) },
        EnemyTypeEntry{ "Bomber", "ボムスライム", MakePreset(60.0f, 60.0f, 1.15f, 0.0f, 60.0f, 16.0f, 32.0f, "Characters/slime_black", true, 5.0f) },
        EnemyTypeEntry{ "Mushroom", "キノコ", MakePreset(35.0f, 35.0f, 1.0f, 2.1f, 60.0f, 16.0f, 16.0f, "Primitives/cylinder", true, 5.0f) },
        EnemyTypeEntry{ "FireSlime", "ファイアスライム", MakePreset(45.0f, 45.0f, 1.0f, 2.35f, 60.0f, 18.0f, 24.0f, "Characters/slime_red", true, 5.0f) },
        EnemyTypeEntry{ "ThunderSlime", "サンダースライム", MakePreset(45.0f, 45.0f, 1.0f, 3.0f, 62.0f, 18.0f, 20.0f, "Characters/slime_yellow", true, 5.0f) },
        EnemyTypeEntry{ "GiantSlime", "巨大スライム", MakePreset(160.0f, 160.0f, 1.25f, 0.0f, 70.0f, 24.0f, 26.0f, "Characters/slime_pink", true, 5.0f) },
        EnemyTypeEntry{ "Bat", "コウモリ", MakePreset(25.0f, 25.0f, 0.8f, 2.6f, 0.0f, 10.0f, 24.0f, "Characters/bat", true, 5.0f) },
        EnemyTypeEntry{ "BeamDrone", "ビームドローン", MakePreset(45.0f, 45.0f, 1.0f, 4.0f, 0.0f, 10.0f, 30.0f, "Characters/eye", true, 5.0f) },
        EnemyTypeEntry{ "BossCore", "ボスコア", MakePreset(1000.0f, 1000.0f, 1.5f, 0.05f, 0.0f, 10.0f, 20.0f, "Stages/block", true, 5.0f) }
    };
    enemyPresets_[1].preset.scale = { 0.16f, 0.16f, 0.16f };
}

void StatusTuningWindow::LoadPresets() {
    std::ifstream file(kPresetPath);
    if (!file) {
        statusText_ = "プリセットJSONがないため初期値を使用中: " + std::string(kPresetPath);
        return;
    }

    try {
        json root;
        file >> root;
        JsonToPreset(root.value("player", json::object()), playerPreset_);

        const json enemies = root.value("enemies", json::object());
        for (auto& entry : enemyPresets_) {
            if (enemies.contains(entry.type)) {
                JsonToPreset(enemies.at(entry.type), entry.preset);
            }
        }

        statusText_ = "プリセットを読み込みました: " + std::string(kPresetPath);
    } catch (...) {
        statusText_ = "プリセットJSONの読み込みに失敗しました。現在の値を維持します。";
    }
}

void StatusTuningWindow::SavePresets() {
    json root;
    root["schemaVersion"] = 3;
    root["player"] = PresetToJson(playerPreset_);
    root["enemies"] = json::object();
    for (const auto& entry : enemyPresets_) {
        root["enemies"][entry.type] = PresetToJson(entry.preset);
    }

    try {
        fs::create_directories(fs::path(kPresetPath).parent_path());
        std::ofstream file(kPresetPath);
        if (!file) {
            statusText_ = "プリセットJSONを開けませんでした: " + std::string(kPresetPath);
            return;
        }

        file << root.dump(4);
        statusText_ = "プリセットを保存しました: " + std::string(kPresetPath);
    } catch (...) {
        statusText_ = "プリセット保存中にエラーが発生しました。";
    }
}

bool StatusTuningWindow::DrawPresetEditor(StatusPreset& preset, const char* id) {
#ifdef USE_IMGUI
    ImGui::PushID(id);
    const bool modelChanged = DrawModelPicker("モデルプレビュー", "PresetModel", preset.modelName);
    ImGui::Spacing();

    const float oldMaxHp = preset.maxHp;
    const bool wasFullHp = std::abs(preset.hp - preset.maxHp) <= 0.001f;
    ImGui::DragFloat("現在HP", &preset.hp, 1.0f, 0.0f, 9999.0f);
    if (ImGui::DragFloat("最大HP", &preset.maxHp, 1.0f, 1.0f, 9999.0f)) {
        preset.maxHp = (std::max)(preset.maxHp, 1.0f);
        if (wasFullHp || std::abs(preset.hp - oldMaxHp) <= 0.001f) {
            preset.hp = preset.maxHp;
        }
    }
    preset.maxHp = (std::max)(preset.maxHp, 1.0f);
    preset.hp = (std::max)(preset.hp, 0.0f);
    if (preset.hp > preset.maxHp) {
        preset.maxHp = preset.hp;
    }

    ImGui::DragFloat("攻撃力", &preset.attackPower, 0.05f, 0.0f, 100.0f);
    preset.attackPower = (std::max)(preset.attackPower, 0.0f);
    ImGui::DragFloat("移動速度", &preset.speed, 0.1f, 0.0f, 100.0f);
    ImGui::DragFloat("重力", &preset.gravity, 0.1f, -100.0f, 200.0f);
    ImGui::DragFloat("ジャンプ力", &preset.jumpPower, 0.1f, 0.0f, 100.0f);
    ImGui::DragFloat("感知範囲", &preset.detectionRange, 0.5f, 0.0f, 500.0f);
    ImGui::DragFloat3("スケールXYZ (0なら維持)", &preset.scale.x, 0.005f, 0.0f, 20.0f, "%.3f");

    ImGui::Checkbox("変身に制限時間を使う", &preset.morphLimited);
    ImGui::BeginDisabled(!preset.morphLimited);
    ImGui::DragFloat("変身時間(秒)", &preset.morphDuration, 0.1f, 0.1f, 999.0f, "%.1f");
    ImGui::EndDisabled();
    preset.morphDuration = (std::max)(preset.morphDuration, 0.1f);
    ImGui::PopID();
    return modelChanged;
#else
    (void)preset;
    (void)id;
    return false;
#endif
}

bool StatusTuningWindow::DrawModelPicker(const char* label, const char* id, std::string& modelName) {
#ifdef USE_IMGUI
    if (!characterModelListReady_) {
        RefreshCharacterModelList();
    }

    bool changed = false;
    const std::string popupId = std::string("モデル選択##") + id;
    const std::string displayName = GetModelDisplayName(modelName);
    const std::string modelPath = modelName.empty() ? "モデル未設定" : modelName;
    const bool isCharacterModel = modelName.rfind("Characters/", 0) == 0;
    const ImVec4 accent = GetModelAccentColor(modelName);
    const ImVec4 buttonColor(accent.x * 0.42f, accent.y * 0.42f, accent.z * 0.42f, 0.92f);
    const ImVec4 hoverColor(accent.x * 0.62f, accent.y * 0.62f, accent.z * 0.62f, 1.0f);
    const ImVec4 activeColor(accent.x * 0.78f, accent.y * 0.78f, accent.z * 0.78f, 1.0f);

    static char filterBuffer[128] = "";
    static char manualModelBuffer[256] = "";

    ImGui::Text("%s", label);
    ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
    const std::string buttonLabel =
        std::string(ICON_FA_CUBE "  ") + displayName +
        "\n" + modelPath +
        "\nクリックして Characters モデルを選択##" + id;
    if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1.0f, 78.0f))) {
        CopyToBuffer(manualModelBuffer, sizeof(manualModelBuffer), modelName);
        ImGui::OpenPopup(popupId.c_str());
    }
    ImGui::PopStyleColor(3);

    ImGui::TextDisabled("%s", isCharacterModel ? "Characters フォルダ内のモデルです。" : "Characters 以外のモデルです。必要なら一覧から差し替えできます。");

    if (ImGui::BeginPopup(popupId.c_str())) {
        ImGui::TextColored(ImVec4(0.62f, 0.95f, 1.0f, 1.0f), ICON_FA_FOLDER_OPEN " Characters モデル一覧");
        ImGui::TextDisabled("選んだ瞬間にこのカードへ反映されます。現在シーンの見た目も対応する場所では即時更新します。");
        if (ImGui::Button(ICON_FA_SYNC " 一覧を更新")) {
            RefreshCharacterModelList();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("検索", filterBuffer, sizeof(filterBuffer));

        ImGui::Separator();
        ImGui::BeginChild("CharacterModelList", ImVec2(620.0f, 320.0f), true);
        int visibleCount = 0;
        int column = 0;
        for (const std::string& candidate : characterModelNames_) {
            const std::string candidateDisplay = GetModelDisplayName(candidate);
            if (!ContainsInsensitive(candidate, filterBuffer) && !ContainsInsensitive(candidateDisplay, filterBuffer)) {
                continue;
            }

            ImGui::PushID(candidate.c_str());
            const bool selected = candidate == modelName;
            const ImVec4 candidateAccent = GetModelAccentColor(candidate);
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(candidateAccent.x, candidateAccent.y, candidateAccent.z, 0.95f) : ImVec4(candidateAccent.x * 0.36f, candidateAccent.y * 0.36f, candidateAccent.z * 0.36f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(candidateAccent.x * 0.68f, candidateAccent.y * 0.68f, candidateAccent.z * 0.68f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(candidateAccent.x * 0.86f, candidateAccent.y * 0.86f, candidateAccent.z * 0.86f, 1.0f));
            const std::string cardLabel = candidateDisplay + "\n" + candidate;
            if (ImGui::Button(cardLabel.c_str(), ImVec2(190.0f, 66.0f))) {
                modelName = candidate;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopID();

            ++visibleCount;
            ++column;
            if (column % 3 != 0) {
                ImGui::SameLine();
            }
        }
        if (visibleCount == 0) {
            ImGui::TextDisabled("該当するキャラクターモデルがありません。");
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::TextDisabled("直接パスを指定したい場合だけ使ってください。例: Characters/slime_pink");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("直接入力", manualModelBuffer, sizeof(manualModelBuffer));
        ImGui::SameLine();
        if (ImGui::Button("直接入力を反映")) {
            modelName = manualModelBuffer;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    return changed;
#else
    (void)label;
    (void)id;
    (void)modelName;
    return false;
#endif
}

void StatusTuningWindow::RefreshCharacterModelList() {
    characterModelNames_.clear();
    characterModelListReady_ = true;

    try {
        const fs::path characterRoot(kCharacterModelDirectory);
        if (!fs::exists(characterRoot) || !fs::is_directory(characterRoot)) {
            return;
        }

        for (const auto& entry : fs::recursive_directory_iterator(characterRoot)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string extension = ToLowerAscii(entry.path().extension().string());
            if (extension != ".obj" && extension != ".gltf" && extension != ".glb") {
                continue;
            }

            const std::string key = MakeModelKeyFromFile(entry.path());
            if (!key.empty() && std::find(characterModelNames_.begin(), characterModelNames_.end(), key) == characterModelNames_.end()) {
                characterModelNames_.push_back(key);
            }
        }

        std::sort(characterModelNames_.begin(), characterModelNames_.end());
    } catch (...) {
        characterModelNames_.clear();
    }
}

void StatusTuningWindow::DrawSelectedObjectEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_CROSSHAIRS " 選択中オブジェクト個別調整", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    Object3d* selected = editor_ ? editor_->GetSelectedObject3D() : nullptr;
    if (!selected) {
        ImGui::TextDisabled("ヒエラルキーまたはシーンでプレイヤー/敵を選択してください。");
        return;
    }

    const bool editable = selected->GetClassName() == "Player" || selected->GetClassName() == "Enemy";
    if (!editable) {
        ImGui::TextDisabled("選択中: %s", selected->GetName().c_str());
        ImGui::TextDisabled("プレイヤーまたは敵だけステータス調整できます。");
        return;
    }

    ImGui::Text("選択中: %s", selected->GetName().c_str());
    if (!selected->GetEnemyType().empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("敵タイプ: %s", selected->GetEnemyType().c_str());
    }

    if (!selected->param_.has_value()) {
        if (ImGui::Button(ICON_FA_PLUS_CIRCLE " ステータスを追加", ImVec2(-1.0f, 0.0f))) {
            selected->param_.emplace();
            statusText_ = selected->GetName() + " にステータスを追加しました。";
        }
        return;
    }

    auto& p = selected->param_.value();
    const float oldMaxHp = p.maxHp;
    const bool wasFullHp = std::abs(p.hp - p.maxHp) <= 0.001f;
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("現在HP", &p.hp, 1.0f, 0.0f, 9999.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragFloat("最大HP", &p.maxHp, 1.0f, 1.0f, 9999.0f)) {
        p.maxHp = (std::max)(p.maxHp, 1.0f);
        if (wasFullHp || std::abs(p.hp - oldMaxHp) <= 0.001f) {
            p.hp = p.maxHp;
        }
    }
    p.maxHp = (std::max)(p.maxHp, 1.0f);
    p.hp = (std::max)(p.hp, 0.0f);
    if (p.hp > p.maxHp) {
        p.maxHp = p.hp;
    }

    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("攻撃力", &p.attackPower, 0.05f, 0.0f, 100.0f);
    p.attackPower = (std::max)(p.attackPower, 0.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("移動速度", &p.speed, 0.1f, 0.0f, 100.0f);

    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("重力", &p.gravity, 0.1f, -100.0f, 200.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("ジャンプ力", &p.jumpPower, 0.1f, 0.0f, 100.0f);

    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("感知範囲", &p.detectionRange, 0.5f, 0.0f, 500.0f);
    if (auto* enemy = dynamic_cast<BaseEnemy*>(selected)) {
        enemy->SetDetectionRange((std::max)(p.detectionRange, 0.0f));
    }

    Vector3 selectedScale = selected->GetScale();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::DragFloat3("スケール", &selectedScale.x, 0.005f, 0.001f, 20.0f, "%.3f")) {
        selected->SetScale(selectedScale);
    }

    std::string selectedModelName = selected->GetModelName();
    if (DrawModelPicker("選択中オブジェクトのモデル", "SelectedStatusModel", selectedModelName)) {
        selected->SetModel(selectedModelName);
        statusText_ = selected->GetName() + " のモデルを " + selectedModelName + " に切り替えました。";
    }

    ImGui::Checkbox("変身に制限時間を使う", &p.morphLimited);
    ImGui::BeginDisabled(!p.morphLimited);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("変身時間(秒)", &p.morphDuration, 0.1f, 0.1f, 999.0f, "%.1f");
    ImGui::EndDisabled();
    p.morphDuration = (std::max)(p.morphDuration, 0.1f);
#else
    (void)editor_;
#endif
}

void StatusTuningWindow::DrawCurrentSceneSummary() {
#ifdef USE_IMGUI
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene) {
        ImGui::TextDisabled("現在シーンがありません。");
        return;
    }

    int playerCount = 0;
    int enemyCount = 0;
    for (const auto& object : scene->GetObjects()) {
        if (!object) {
            continue;
        }
        if (object->GetClassName() == "Player") {
            ++playerCount;
        } else if (object->GetClassName() == "Enemy") {
            ++enemyCount;
        }
    }

    ImGui::Text("現在シーン: プレイヤー %d / 敵 %d", playerCount, enemyCount);
#endif
}

void StatusTuningWindow::DrawEnemyPresetRows() {
#ifdef USE_IMGUI
    ImGui::TextWrapped("敵タイプごとに縦カードで調整します。モデルカードを選ぶと、現在シーン内の同じ敵タイプへ即時反映されます。");
    ImGui::TextDisabled("全ステージJSONへ反映したい場合は、各カード下の保存ボタンを使ってください。");
    if (ImGui::Button(ICON_FA_SYNC " Characters モデル一覧を更新", ImVec2(-1.0f, 0.0f))) {
        RefreshCharacterModelList();
        statusText_ = "Characters モデル一覧を更新しました。";
    }

    ImGui::BeginChild("EnemyPresetVerticalList", ImVec2(0.0f, 560.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& entry : enemyPresets_) {
        ImGui::PushID(entry.type);
        const std::string header = std::string(entry.label) + "  /  内部タイプ: " + entry.type;
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool modelChanged = DrawPresetEditor(entry.preset, "EnemyPresetEditor");
            if (modelChanged) {
                const int applied = ApplyEnemyPreset(entry.type, entry.preset);
                statusText_ = std::string(entry.label) + " のモデルを現在シーンへ即時反映しました: " + std::to_string(applied) + " 件";
                SavePresets();
            }

            if (ImGui::Button(ICON_FA_CHECK " この設定を現在シーンの同タイプ敵へ反映", ImVec2(-1.0f, 0.0f))) {
                const int applied = ApplyEnemyPreset(entry.type, entry.preset);
                statusText_ = std::string(entry.label) + " を現在シーンへ反映しました: " + std::to_string(applied) + " 件";
                SavePresets();
            }
            if (ImGui::Button(ICON_FA_SAVE " この設定を全ステージJSONの同タイプ敵へ反映", ImVec2(-1.0f, 0.0f))) {
                const int applied = ApplyEnemyPresetToStageFiles(entry.type, entry.preset);
                statusText_ = std::string(entry.label) + " をステージJSONへ反映しました: " + std::to_string(applied) + " 件";
                SavePresets();
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
#endif
}

bool StatusTuningWindow::CapturePlayerPresetFromScene() {
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene) {
        return false;
    }

    for (const auto& object : scene->GetObjects()) {
        if (!object || object->GetClassName() != "Player" || !object->param_.has_value()) {
            continue;
        }

        playerPreset_ = ParameterToPreset(object->param_.value());
        playerPreset_.modelName = object->GetModelName();
        playerPreset_.scale = object->GetScale();
        return true;
    }
    return false;
}

int StatusTuningWindow::ApplyPlayerPreset() {
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene) {
        return 0;
    }

    int applied = 0;
    for (const auto& object : scene->GetObjects()) {
        if (!object || object->GetClassName() != "Player") {
            continue;
        }
        ApplyPresetToObject(object.get(), playerPreset_);
        if (!playerPreset_.modelName.empty()) {
            object->SetModel(playerPreset_.modelName);
        }
        ++applied;
    }
    return applied;
}

int StatusTuningWindow::ApplyPlayerPresetToStageFiles() {
    int applied = 0;
    try {
        const fs::path root(kStageObjectDirectory);
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return 0;
        }

        for (const auto& entry : fs::directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            json sceneData;
            {
                std::ifstream file(entry.path());
                if (!file) {
                    continue;
                }
                file >> sceneData;
            }

            if (!sceneData.contains("objects") || !sceneData["objects"].is_array()) {
                continue;
            }

            bool changed = false;
            for (auto& objectData : sceneData["objects"]) {
                if (!IsPlayerJsonObject(objectData)) {
                    continue;
                }

                ApplyPlayerPresetToJsonObject(objectData, playerPreset_);
                changed = true;
                ++applied;
            }

            if (!changed) {
                continue;
            }

            std::ofstream file(entry.path());
            if (!file) {
                continue;
            }
            file << sceneData.dump(4);
        }
    } catch (...) {
        return applied;
    }

    return applied;
}

int StatusTuningWindow::ApplyEnemyPreset(const std::string& enemyType, const StatusPreset& preset) {
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene) {
        return 0;
    }

    int applied = 0;
    for (const auto& object : scene->GetObjects()) {
        if (!object || object->GetClassName() != "Enemy" || object->GetEnemyType() != enemyType) {
            continue;
        }

        ApplyPresetToObject(object.get(), preset);
        if (!preset.modelName.empty()) {
            object->SetModel(preset.modelName);
        }
        if (auto* enemy = dynamic_cast<BaseEnemy*>(object.get())) {
            enemy->SetDetectionRange(object->param_->detectionRange);
        }
        ++applied;
    }
    return applied;
}

int StatusTuningWindow::ApplyEnemyPresetToStageFiles(const std::string& enemyType, const StatusPreset& preset) {
    int applied = 0;
    try {
        const fs::path root(kStageObjectDirectory);
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return 0;
        }

        for (const auto& entry : fs::directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            json sceneData;
            {
                std::ifstream file(entry.path());
                if (!file) {
                    continue;
                }
                file >> sceneData;
            }

            if (!sceneData.contains("objects") || !sceneData["objects"].is_array()) {
                continue;
            }

            bool changed = false;
            for (auto& objectData : sceneData["objects"]) {
                if (!IsEnemyJsonObject(objectData, enemyType)) {
                    continue;
                }

                ApplyEnemyPresetToJsonObject(objectData, enemyType, preset);
                changed = true;
                ++applied;
            }

            if (!changed) {
                continue;
            }

            std::ofstream file(entry.path());
            if (!file) {
                continue;
            }
            file << sceneData.dump(4);
        }
    } catch (...) {
        return applied;
    }

    return applied;
}

void StatusTuningWindow::ApplyPresetToObject(Object3d* object, const StatusPreset& preset) {
    if (!object) {
        return;
    }

    const Object3d::EntityParameter* baseParam = object->param_.has_value() ? &object->param_.value() : nullptr;
    object->param_ = MakeParameter(preset, baseParam);
    if (IsStatusScaleEnabled(preset)) {
        object->SetScale(preset.scale);
    }
}

Object3d::EntityParameter StatusTuningWindow::MakeParameter(const StatusPreset& preset, const Object3d::EntityParameter* baseParam) const {
    Object3d::EntityParameter result = baseParam ? *baseParam : Object3d::EntityParameter{};
    result.maxHp = (std::max)(preset.maxHp, 1.0f);
    result.hp = (std::max)(preset.hp, 0.0f);
    if (result.hp > result.maxHp) {
        result.maxHp = result.hp;
    }
    result.attackPower = (std::max)(preset.attackPower, 0.0f);
    result.speed = (std::max)(preset.speed, 0.0f);
    result.gravity = preset.gravity;
    result.jumpPower = (std::max)(preset.jumpPower, 0.0f);
    result.detectionRange = (std::max)(preset.detectionRange, 0.0f);
    result.morphLimited = preset.morphLimited;
    result.morphDuration = (std::max)(preset.morphDuration, 0.1f);
    return result;
}
