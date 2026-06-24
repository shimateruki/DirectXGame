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
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char* kPresetPath = "Resources/json/gameplay/status_presets.json";

StatusTuningWindow::StatusPreset MakePreset(
    float hp,
    float maxHp,
    float attackPower,
    float speed,
    float gravity,
    float jumpPower,
    float detectionRange) {
    StatusTuningWindow::StatusPreset preset;
    preset.hp = hp;
    preset.maxHp = maxHp;
    preset.attackPower = attackPower;
    preset.speed = speed;
    preset.gravity = gravity;
    preset.jumpPower = jumpPower;
    preset.detectionRange = detectionRange;
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
        { "detectionRange", preset.detectionRange }
    };
}

void ReadFloat(const json& source, const char* key, float& value) {
    if (!source.is_object() || !source.contains(key)) {
        return;
    }

    const auto& node = source.at(key);
    if (node.is_number()) {
        value = node.get<float>();
    }
}

void JsonToPreset(const json& source, StatusTuningWindow::StatusPreset& preset) {
    ReadFloat(source, "hp", preset.hp);
    ReadFloat(source, "maxHp", preset.maxHp);
    ReadFloat(source, "attackPower", preset.attackPower);
    ReadFloat(source, "speed", preset.speed);
    ReadFloat(source, "gravity", preset.gravity);
    ReadFloat(source, "jumpPower", preset.jumpPower);
    ReadFloat(source, "detectionRange", preset.detectionRange);
}
} // namespace

void StatusTuningWindow::Initialize(DebugEditor* editor, SceneManager* sceneManager) {
    editor_ = editor;
    sceneManager_ = sceneManager;
    ResetPresetsToDefaults();
    LoadPresets();
}

void StatusTuningWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.62f, 0.95f, 1.0f, 1.0f), ICON_FA_SLIDERS_H " ステータス調整");
    ImGui::TextWrapped("現在シーンに配置されているPlayer、またはEnemyかつ指定Enemy Type一致のオブジェクトへ、同じステータスをまとめて上書きします。");
    ImGui::TextDisabled("反映後はシーン保存を押すと、各オブジェクトのparamとしてステージJSONに保存されます。");
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_SAVE " プリセット保存")) {
        SavePresets();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " プリセット再読み込み")) {
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
        DrawPresetEditor(playerPreset_, "PlayerPreset");
        if (ImGui::Button(ICON_FA_CHECK " 現在シーンのPlayer全員へ反映", ImVec2(-1.0f, 0.0f))) {
            const int applied = ApplyPlayerPreset();
            statusText_ = "Playerへステータスを反映しました: " + std::to_string(applied) + " 件。";
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
    playerPreset_ = MakePreset(100.0f, 100.0f, 1.0f, 0.5f, 50.0f, 16.0f, 20.0f);
    enemyPresets_ = {
        EnemyTypeEntry{ "Slime", "スライム", MakePreset(50.0f, 50.0f, 1.0f, 0.1f, 60.0f, 10.0f, 20.0f) },
        EnemyTypeEntry{ "Bomb", "ボム", MakePreset(30.0f, 30.0f, 1.0f, 0.04f, 60.0f, 10.0f, 20.0f) },
        EnemyTypeEntry{ "Bomber", "ボマー", MakePreset(60.0f, 60.0f, 1.15f, 0.0f, 60.0f, 10.0f, 32.0f) },
        EnemyTypeEntry{ "Mushroom", "キノコ", MakePreset(35.0f, 35.0f, 1.0f, 2.1f, 60.0f, 10.0f, 16.0f) },
        EnemyTypeEntry{ "FireSlime", "ファイアスライム", MakePreset(45.0f, 45.0f, 1.0f, 2.35f, 60.0f, 10.0f, 24.0f) },
        EnemyTypeEntry{ "ThunderSlime", "サンダースライム", MakePreset(45.0f, 45.0f, 1.0f, 3.0f, 62.0f, 10.0f, 20.0f) },
        EnemyTypeEntry{ "GiantSlime", "ジャイアントスライム", MakePreset(160.0f, 160.0f, 1.25f, 0.0f, 70.0f, 24.0f, 26.0f) },
        EnemyTypeEntry{ "Bat", "コウモリ", MakePreset(25.0f, 25.0f, 0.8f, 2.6f, 0.0f, 10.0f, 24.0f) },
        EnemyTypeEntry{ "BeamDrone", "ビームドローン", MakePreset(45.0f, 45.0f, 1.0f, 4.0f, 0.0f, 10.0f, 30.0f) },
        EnemyTypeEntry{ "BossCore", "ボスコア", MakePreset(1000.0f, 1000.0f, 1.5f, 0.05f, 0.0f, 10.0f, 20.0f) }
    };
}

void StatusTuningWindow::LoadPresets() {
    std::ifstream file(kPresetPath);
    if (!file) {
        statusText_ = "プリセットJSONがないため初期値を使用しています: " + std::string(kPresetPath);
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
    root["schemaVersion"] = 2;
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

void StatusTuningWindow::DrawPresetEditor(StatusPreset& preset, const char* id) {
#ifdef USE_IMGUI
    ImGui::PushID(id);
    const float oldMaxHp = preset.maxHp;
    const bool wasFullHp = std::abs(preset.hp - preset.maxHp) <= 0.001f;
    ImGui::DragFloat("HP", &preset.hp, 1.0f, 0.0f, 9999.0f);
    if (ImGui::DragFloat("Max HP", &preset.maxHp, 1.0f, 1.0f, 9999.0f)) {
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
    ImGui::DragFloat("攻撃力倍率", &preset.attackPower, 0.05f, 0.0f, 100.0f);
    preset.attackPower = (std::max)(preset.attackPower, 0.0f);
    ImGui::DragFloat("速度", &preset.speed, 0.1f, 0.0f, 100.0f);
    ImGui::DragFloat("重力", &preset.gravity, 0.1f, -100.0f, 200.0f);
    ImGui::DragFloat("ジャンプ力", &preset.jumpPower, 0.1f, 0.0f, 100.0f);
    ImGui::DragFloat("検知範囲", &preset.detectionRange, 0.5f, 0.0f, 500.0f);
    ImGui::PopID();
#else
    (void)preset;
    (void)id;
#endif
}

void StatusTuningWindow::DrawSelectedObjectEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_CROSSHAIRS " 選択中オブジェクト個別調整", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    Object3d* selected = editor_ ? editor_->GetSelectedObject3D() : nullptr;
    if (!selected) {
        ImGui::TextDisabled("HierarchyやSceneでPlayer/Enemyを選択すると、ここで個別調整できます。");
        return;
    }

    const bool editable = selected->GetClassName() == "Player" || selected->GetClassName() == "Enemy";
    if (!editable) {
        ImGui::TextDisabled("選択中: %s", selected->GetName().c_str());
        ImGui::TextDisabled("PlayerまたはEnemyを選択してください。");
        return;
    }

    ImGui::Text("選択中: %s", selected->GetName().c_str());
    if (!selected->GetEnemyType().empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("EnemyType: %s", selected->GetEnemyType().c_str());
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
    ImGui::DragFloat("HP", &p.hp, 1.0f, 0.0f, 9999.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragFloat("Max HP", &p.maxHp, 1.0f, 1.0f, 9999.0f)) {
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
    ImGui::DragFloat("攻撃力倍率", &p.attackPower, 0.05f, 0.0f, 100.0f);
    p.attackPower = (std::max)(p.attackPower, 0.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("速度", &p.speed, 0.1f, 0.0f, 100.0f);

    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("重力", &p.gravity, 0.1f, -100.0f, 200.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("ジャンプ力", &p.jumpPower, 0.1f, 0.0f, 100.0f);

    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("検知範囲", &p.detectionRange, 0.5f, 0.0f, 500.0f);
    if (auto* enemy = dynamic_cast<BaseEnemy*>(selected)) {
        enemy->SetDetectionRange((std::max)(p.detectionRange, 0.0f));
    }
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

    ImGui::Text("現在シーン: Player %d / Enemy %d", playerCount, enemyCount);
#endif
}

void StatusTuningWindow::DrawEnemyPresetRows() {
#ifdef USE_IMGUI
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("StatusTuningEnemyTable", 9, flags, ImVec2(0.0f, 430.0f))) {
        ImGui::TableSetupColumn("Enemy Type", ImGuiTableColumnFlags_WidthFixed, 145.0f);
        ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("MaxHP", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("攻撃", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("速度", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("重力", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("ジャンプ", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("検知", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("反映", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        for (auto& entry : enemyPresets_) {
            ImGui::PushID(entry.type);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", entry.label);
            ImGui::TextDisabled("%s", entry.type);

            const float oldMaxHp = entry.preset.maxHp;
            const bool wasFullHp = std::abs(entry.preset.hp - entry.preset.maxHp) <= 0.001f;
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat("##hp", &entry.preset.hp, 1.0f, 0.0f, 9999.0f, "%.1f");
            ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##maxHp", &entry.preset.maxHp, 1.0f, 1.0f, 9999.0f, "%.1f")) {
                entry.preset.maxHp = (std::max)(entry.preset.maxHp, 1.0f);
                if (wasFullHp || std::abs(entry.preset.hp - oldMaxHp) <= 0.001f) {
                    entry.preset.hp = entry.preset.maxHp;
                }
            }
            entry.preset.maxHp = (std::max)(entry.preset.maxHp, 1.0f);
            entry.preset.hp = (std::max)(entry.preset.hp, 0.0f);
            if (entry.preset.hp > entry.preset.maxHp) {
                entry.preset.maxHp = entry.preset.hp;
            }
            ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat("##attack", &entry.preset.attackPower, 0.05f, 0.0f, 100.0f, "%.2f");
            entry.preset.attackPower = (std::max)(entry.preset.attackPower, 0.0f);
            ImGui::TableSetColumnIndex(4); ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat("##speed", &entry.preset.speed, 0.1f, 0.0f, 100.0f, "%.2f");
            ImGui::TableSetColumnIndex(5); ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat("##gravity", &entry.preset.gravity, 0.1f, -100.0f, 200.0f, "%.1f");
            ImGui::TableSetColumnIndex(6); ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat("##jump", &entry.preset.jumpPower, 0.1f, 0.0f, 100.0f, "%.1f");
            ImGui::TableSetColumnIndex(7); ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat("##detect", &entry.preset.detectionRange, 0.5f, 0.0f, 500.0f, "%.1f");

            ImGui::TableSetColumnIndex(8);
            if (ImGui::Button(ICON_FA_CHECK " このタイプへ", ImVec2(-1.0f, 0.0f))) {
                const int applied = ApplyEnemyPreset(entry.type, entry.preset);
                statusText_ = std::string(entry.type) + "へステータスを反映しました: " + std::to_string(applied) + " 件。";
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
#endif
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
        ++applied;
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
        if (auto* enemy = dynamic_cast<BaseEnemy*>(object.get())) {
            enemy->SetDetectionRange(object->param_->detectionRange);
        }
        ++applied;
    }
    return applied;
}

void StatusTuningWindow::ApplyPresetToObject(Object3d* object, const StatusPreset& preset) {
    if (!object) {
        return;
    }

    const Object3d::EntityParameter* baseParam = object->param_.has_value() ? &object->param_.value() : nullptr;
    object->param_ = MakeParameter(preset, baseParam);
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
    return result;
}
