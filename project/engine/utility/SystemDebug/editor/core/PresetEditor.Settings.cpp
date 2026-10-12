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

// 選択プリセットの種別固有値とColliderを編集します。
bool PresetEditor::DrawEnemySettings(json& data) {
    // 敵の共通ステータスは別画面で管理し、ここでは種類の選択だけ扱います。
    bool changed = false;
    ApplyHiddenCategoryFields(data, Category::Enemy);
    json& param = EnsureParam(data);

    if (ImGui::CollapsingHeader("敵パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= DrawTypeCombo("敵の種類", data, param, "enemyType", GetEnemyOptions(), Category::Enemy);
        ImGui::TextDisabled("HP・攻撃力・移動・重力などはステータス管理でタイプごとに編集します。");
    }
    return changed;
}

bool PresetEditor::RemoveManagedEnemyFields(json& data) {
    // ステータス管理と二重定義になる旧フィールドだけをPresetから取り除きます。
    bool changed = false;
    changed |= data.erase("modelName") > 0;
    changed |= data.erase("scale") > 0;
    if (!data.contains("param") || !data["param"].is_object()) {
        return changed;
    }

    json& param = data["param"];
    const char* managedKeys[] = {
        "hp", "maxHp", "attackPower", "speed", "gravity", "maxFallSpeed",
        "jumpPower", "detectionRange", "morphLimited", "morphDuration", "interval"
    };
    for (const char* key : managedKeys) {
        changed |= param.erase(key) > 0;
    }
    return changed;
}

bool PresetEditor::DrawGimmickSettings(json& data) {
    // 選択したギミック型に該当する設定だけを表示し、無関係な値はUIへ出しません。
    bool changed = false;
    ApplyHiddenCategoryFields(data, Category::Gimmick);
    json& param = EnsureParam(data);

    if (ImGui::CollapsingHeader("ギミックパラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= DrawTypeCombo("ギミックの種類", data, param, "gimmickType", GetGimmickOptions(), Category::Gimmick);
        const std::string type = ReadString(data, "gimmickType", ReadString(param, "gimmickType", ""));

        if (type == "Trampoline") {
            changed |= DrawParamFloat(param, "ジャンプ力", "jumpPower", 18.0f, 0.5f);
        }
        else if (type == "MovingFloor") {
            changed |= DrawParamFloat(param, "移動速度", "speed", 3.0f, 0.1f);
            changed |= DrawParamIntCombo(param, "移動方式", "actionMode", 0,
                { "従来の上下移動", "浮遊", "X方向に往復", "Z方向に往復", "Y方向に往復" });
            if (param.value("actionMode", 0) == 1) {
                changed |= DrawParamFloat(param, "浮き沈み幅", "moveAmount", 0.35f, 0.01f);
            }
            else if (param.value("actionMode", 0) >= 2) {
                changed |= DrawParamFloat(param, "往復幅", "moveAmount", 8.0f, 0.1f);
            }
        }
        else if (type == "ChikuwaBlock") {
            changed |= DrawParamFloat(param, "震え時間", "shakeDuration", 1.0f, 0.05f);
            changed |= DrawParamFloat(param, "落下時間", "fallDuration", 2.0f, 0.05f);
            changed |= DrawParamFloat(param, "復帰間隔", "interval", 3.0f, 0.05f);
            changed |= DrawParamFloat(param, "重力", "gravity", 50.0f, 1.0f);
        }
        else if (type == "BlinkBlock") {
            changed |= DrawParamIntCombo(param, "色タイプ", "colorType", 0, { "青", "赤" });
        }
        else if (type == "BreakableBlock") {
            const int actionMode = param.value("actionMode", 0);
            int condition = actionMode == 7 ? 2 : (actionMode == 6 ? 1 : 0);
            if (ImGui::Combo("破壊条件", &condition,
                "ボムのみ\0汎用衝撃攻撃\0ピンクのバウンド落下\0")) {
                param["actionMode"] = condition == 2 ? 7 : (condition == 1 ? 6 : 0);
                changed = true;
            }
            ImGui::TextDisabled("ピンク専用は通常攻撃や横突進では開きません。");
        }
        else if (type == "Switch") {
            changed |= DrawParamIntCombo(param, "スイッチ方式", "switchMode", 0, { "押している間", "押すたび切替", "一定時間" });
            changed |= DrawParamFloat(param, "有効時間", "interval", 3.0f, 0.05f);
        }
        else if (type == "EventReceiver") {
            changed |= DrawParamIntCombo(param, "動作モード", "actionMode", 0, { "出現", "Y移動", "X移動", "Z移動", "有効化", "無効化", "ボス王冠落下" });
            changed |= DrawParamFloat(param, "移動量", "moveAmount", 10.0f, 0.1f);
            changed |= DrawParamFloat(param, "移動速度", "moveSpeed", 6.0f, 0.05f);
            changed |= DrawParamFloat(param, "落下演出時間", "fallDuration", 1.55f, 0.02f);
            changed |= DrawParamFloat(param, "弧の高さ", "jumpPower", 1.85f, 0.05f);
            changed |= DrawParamBool(param, "開始時に有効", "startActive", false);
            changed |= DrawParamBool(param, "OFFで元に戻す", "returnOnOff", true);
        }
        else if (type == "ArenaEncounter") {
            changed |= DrawParamInt(param, "障壁数", "maxCount", 4);
            changed |= DrawParamFloat(param, "ボス出現まで", "shakeDuration", 0.72f, 0.02f);
            changed |= DrawParamBool(param, "遭遇を有効にする", "startActive", true);
        }
        else if (type == "GameplayVolume") {
            changed |= DrawParamIntCombo(param, "処理", "volumeMode", 0, { "イベント連携", "チェックポイント", "落下死", "Feedback Cue", "BGM", "Environment Profile" });
            changed |= DrawStringField(param, "Cue / Audio ID", "volumePayload", "");
            changed |= DrawParamBool(param, "一度だけ実行", "volumeTriggerOnce", true);
            changed |= DrawParamBool(param, "退出時に実行", "volumeTriggerOnExit", false);
            changed |= DrawParamFloat(param, "再実行まで", "volumeRearmDelay", 0.0f, 0.05f);
            changed |= DrawParamFloat(param, "環境切替時間", "volumeBlendDuration", 0.75f, 0.05f);
            changed |= DrawParamBool(param, "開始時に有効", "startActive", true);
            changed |= DrawParamBool(param, "退出時にOFFへ戻す", "returnOnOff", false);
        }
        else if (type == "CopyMemoryStation") {
            changed |= DrawStringField(param, "左コア", "copyMemoryTypeA", "FireSlime");
            changed |= DrawStringField(param, "中央コア", "copyMemoryTypeB", "ThunderSlime");
            changed |= DrawStringField(param, "右コア", "copyMemoryTypeC", "WindSlime");
            changed |= DrawStringField(param, "4台目（空欄で無効）", "copyMemoryTypeD", "");
            changed |= DrawParamFloat(param, "コア接触半径", "copyMemoryActivationRadius", 0.9f, 0.02f);
            changed |= DrawParamBool(param, "時間制限なし", "copyMemoryUnlimitedDuration", true);
            changed |= DrawParamBool(param, "開始時に有効", "startActive", true);
        }
        else if (type == "PrismBarrier") {
            changed |= DrawParamFloat(param, "展開・解除時間", "moveSpeed", 0.42f, 0.02f);
            changed |= DrawParamFloat(param, "発光パルス速度", "interval", 2.4f, 0.05f);
            changed |= DrawParamBool(param, "開始時に展開", "startActive", false);
            changed |= DrawParamBool(param, "OFFで解除", "returnOnOff", true);
        }
        else if (type == "HookPullBlock") {
            changed |= DrawParamFloat(param, "引っ張り速度", "speed", 24.0f, 0.5f);
            changed |= DrawParamFloat(param, "重力", "gravity", 50.0f, 1.0f);
        }
        else if (type == "LiquidLevel") {
            changed |= DrawParamIntCombo(param, "液体の種類", "colorType", 0, { "水", "マグマ" });
            changed |= DrawParamFloat(param, "上下量", "moveAmount", 10.0f, 0.1f);
            changed |= DrawParamFloat(param, "上下速度", "moveSpeed", 3.0f, 0.05f);
            changed |= DrawParamBool(param, "開始時に上昇", "startActive", false);
            changed |= DrawParamBool(param, "OFFで元に戻す", "returnOnOff", true);
        }
        else if (type == "MagmaHazard") {
            changed |= DrawParamFloat(param, "接触ダメージ", "speed", 12.0f, 0.5f);
            changed |= DrawParamFloat(param, "ダメージ間隔", "interval", 0.8f, 0.05f);
        }
        else if (type == "ChainCollapseFloor") {
            changed |= DrawParamFloat(param, "揺れ時間", "shakeDuration", 0.8f, 0.05f);
            changed |= DrawParamFloat(param, "連鎖までの時間", "interval", 0.3f, 0.01f);
            changed |= DrawParamFloat(param, "落下時間", "fallDuration", 2.0f, 0.05f);
            changed |= DrawParamFloat(param, "重力", "gravity", 50.0f, 1.0f);
        }
        else if (type == "RotatingFloor" || type == "RotatingPillar") {
            changed |= DrawParamIntCombo(param, "回転軸", "actionMode", 1, { "X", "Y", "Z" });
            changed |= DrawParamFloat(param, "回転速度", "speed", 90.0f, 1.0f);
            changed |= DrawParamBool(param, "開始時に回転", "startActive", true);
            changed |= DrawParamBool(param, "OFFで停止", "returnOnOff", true);
        }
        else if (type == "PhaseFlipFloor") {
            changed |= DrawParamInt(param, "床番号", "colorType", 0);
            changed |= DrawParamInt(param, "全体の床数", "maxCount", 3);
            changed |= DrawParamFloat(param, "1フェーズの時間", "interval", 2.0f, 0.05f);
            changed |= DrawParamBool(param, "正方向に回転", "startActive", true);
        }
        else if (type == "StageGate") {
            changed |= DrawParamIntCombo(param, "入口の用途", "actionMode", 0,
                {"ステージ選択の入口", "指定シーンへ遷移", "指定ステージへ遷移"});
            changed |= DrawStringField(param, "遷移先シーン", "targetScene", "SELECT");
            changed |= DrawIntField(data, "ステージ選択番号（0始まり）", "targetID", 0);
            changed |= DrawParamInt(param, "ステージ番号", "colorType", 0);
            changed |= DrawParamBool(param, "遷移を有効にする", "startActive", true);
        }
        else {
            ImGui::TextDisabled("このギミックは追加パラメータなしで配置できます。");
        }
    }
    return changed;
}

bool PresetEditor::DrawItemSettings(json& data) {
    // アイテム共通の種類を先に選び、型固有の値だけ後続で編集します。
    bool changed = false;
    ApplyHiddenCategoryFields(data, Category::Item);
    json& param = EnsureParam(data);

    if (ImGui::CollapsingHeader("アイテムパラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= DrawTypeCombo("アイテムの種類", data, param, "itemType", GetItemOptions(), Category::Item);
        const std::string type = ReadString(data, "itemType", ReadString(param, "itemType", ""));
        if (type == "Heal") {
            changed |= DrawParamFloat(param, "回復量", "healAmount", 1.0f, 0.1f);
        }
    }
    return changed;
}

bool PresetEditor::DrawTypeCombo(const char* label, json& data, json& param, const char* key, const std::vector<TypeOption>& options, Category category) {
    // 旧JSONで型がparam内にある場合も読み、変更時は両方の参照先を揃えます。
    std::string current = ReadString(data, key, ReadString(param, key, ""));
    int currentIndex = 0;
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        if (current == options[i].value) {
            currentIndex = i;
            break;
        }
    }

    std::vector<const char*> labels;
    labels.reserve(options.size());
    for (const TypeOption& option : options) {
        labels.push_back(option.label);
    }

    if (ImGui::Combo(label, &currentIndex, labels.data(), static_cast<int>(labels.size()))) {
        data[key] = options[currentIndex].value;
        param[key] = options[currentIndex].value;
        ApplyHiddenCategoryFields(data, category);
        ApplyTypeDefaults(data, category, options[currentIndex].value);
        return true;
    }
    return false;
}

void PresetEditor::ApplyHiddenCategoryFields(json& data, Category category) {
    if (category == Category::Enemy) {
        data["type"] = "Enemy";
        data["saveCategory"] = "Enemy";
    }
    else if (category == Category::Gimmick) {
        data["type"] = "Gimmick";
        data["saveCategory"] = "Object";
    }
    else if (category == Category::Item) {
        data["type"] = "Item";
        data["saveCategory"] = "Object";
    }
    else {
        // EffectやInvisibleBoxの数値編集で、種別をModelへ変えてしまわないようにします。
        if (!data.contains("type") || ReadString(data, "type", "").empty())
            data["type"] = "Model";
        data["saveCategory"] = "Object";
    }
}

void PresetEditor::ApplyTypeDefaults(json& data, Category category, const std::string& type) {
    if (category == Category::Enemy) {
        if (type == "GiantSlime") {
            SetColliderDefaults(data, 1, { 2.2f, 2.2f, 2.2f });
        }
        else {
            SetColliderDefaults(data, 1, { 1.0f, 1.0f, 1.0f });
        }
    }
    else if (category == Category::Gimmick) {
        if (type == "Coin") {
            data["modelName"] = "Primitives/sphere";
            data["scale"] = { 0.6f, 0.6f, 0.15f };
            data["color"] = { 1.0f, 0.9f, 0.0f, 1.0f };
            SetColliderDefaults(data, 1, { 1.0f, 1.0f, 1.0f });
        }
        else if (type == "StageGate") {
            data["modelName"] = "Gimmicks/portal_surface";
            data["scale"] = { 1.0f, 1.0f, 1.0f };
            data["materialType"] = 22;
            data["blendMode"] = 1;
            data["emissive"] = 1.8f;
            json& param = EnsureParam(data);
            param["gimmickType"] = "StageGate";
            param["actionMode"] = 0;
            param["targetScene"] = "SELECT";
            param["startActive"] = true;
            SetColliderDefaults(data, 2, { 1.5f, 1.5f, 1.5f });
        }
        else if (type == "ArenaEncounter") {
            data["modelName"] = "Primitives/cube";
            data["scale"] = { 1.0f, 1.0f, 1.0f };
            data["color"] = { 0.42f, 0.24f, 1.0f, 0.22f };
            data["collisionAttribute"] = CollisionCategories::Get("trigger");
            data["collisionMask"] = CollisionCategories::Get("player");
            json& param = EnsureParam(data);
            param["gimmickType"] = "ArenaEncounter";
            param["maxCount"] = 4;
            param["shakeDuration"] = 0.72f;
            param["startActive"] = true;
            SetColliderDefaults(data, 3, { 3.0f, 3.0f, 8.0f });
        }
        else if (type == "GameplayVolume") {
            data["modelName"] = "Primitives/cube";
            data["scale"] = { 3.0f, 3.0f, 3.0f };
            data["color"] = { 0.15f, 0.75f, 1.0f, 0.3f };
            data["blendMode"] = 1;
            data["collisionAttribute"] = CollisionCategories::Get("trigger");
            data["collisionMask"] = CollisionCategories::Get("player");
            json& param = EnsureParam(data);
            param["gimmickType"] = "GameplayVolume";
            param["volumeMode"] = 0;
            param["volumePayload"] = "";
            param["volumeTriggerOnce"] = true;
            param["volumeTriggerOnExit"] = false;
            param["volumeRearmDelay"] = 0.0f;
            param["volumeBlendDuration"] = 0.75f;
            param["startActive"] = true;
            param["returnOnOff"] = false;
            SetColliderDefaults(data, 3, { 1.0f, 1.0f, 1.0f });
        }
        else if (type == "CopyMemoryStation") {
            data["modelName"] = "Gimmicks/copy_memory_station";
            data["scale"] = { 1.0f, 1.0f, 1.0f };
            data["materialType"] = 0;
            data["emissive"] = 1.0f;
            data["metallic"] = 0.18f;
            data["roughness"] = 0.34f;
            data["collisionAttribute"] = CollisionCategories::Get("trigger");
            data["collisionMask"] = CollisionCategories::Get("player");
            json& param = EnsureParam(data);
            param["gimmickType"] = "CopyMemoryStation";
            param["copyMemoryTypeA"] = "FireSlime";
            param["copyMemoryTypeB"] = "ThunderSlime";
            param["copyMemoryTypeC"] = "WindSlime";
            param["copyMemoryActivationRadius"] = 0.9f;
            param["copyMemoryUnlimitedDuration"] = true;
            param["startActive"] = true;
            SetColliderDefaults(data, 3, { 3.1f, 1.6f, 1.85f });
            data["collider"]["center"] = { 0.0f, 0.95f, 0.0f };
        }
        else if (type == "PrismBarrier") {
            data["modelName"] = "Gimmicks/portal_surface";
            data["scale"] = { 8.0f, 3.0f, 1.0f };
            data["materialType"] = 22;
            data["blendMode"] = 1;
            data["color"] = { 0.30f, 0.86f, 1.0f, 0.84f };
            data["emissive"] = 2.4f;
            data["collisionAttribute"] = CollisionCategories::Get("ground");
            data["collisionMask"] = CollisionCategories::GetDefaultGameplayMask();
            json& param = EnsureParam(data);
            param["gimmickType"] = "PrismBarrier";
            param["moveSpeed"] = 0.42f;
            param["interval"] = 2.4f;
            param["startActive"] = false;
            param["returnOnOff"] = true;
            SetColliderDefaults(data, 3, { 1.0f, 1.0f, 0.45f });
        }
        else {
            data["modelName"] = "Stages/block";
            SetColliderDefaults(data, 3, { 1.0f, 1.0f, 1.0f });
        }
    }
    else if (category == Category::Item) {
        data["modelName"] = "Item/heart.gltf";
        data["scale"] = { 0.8f, 0.8f, 0.8f };
        SetColliderDefaults(data, 1, { 1.2f, 1.2f, 1.2f });
    }
    // ここで作った数値Maskは現在のJSON定義の値なので、IDも同時に保存します。
    if (data.contains("collisionAttribute") && data["collisionAttribute"].is_number_unsigned()) {
        data["collisionAttributeIds"] = CollisionCategories::GetIds(data["collisionAttribute"].get<uint32_t>());
    }
    if (data.contains("collisionMask") && data["collisionMask"].is_number_unsigned()) {
        data["collisionMaskIds"] = CollisionCategories::GetIds(data["collisionMask"].get<uint32_t>());
    }
}

void PresetEditor::SetColliderDefaults(json& data, int type, const Vector3& size) {
    // 形状と大きさだけ更新し、調整済みの中心・回転は残します。
    if (!data.contains("collider") || !data["collider"].is_object()) {
        data["collider"] = json::object();
    }
    data["collider"]["type"] = type;
    data["collider"]["size"] = { size.x, size.y, size.z };
    if (!data["collider"].contains("center")) data["collider"]["center"] = { 0.0f, 0.0f, 0.0f };
    if (!data["collider"].contains("rotation")) data["collider"]["rotation"] = { 0.0f, 0.0f, 0.0f };
}

bool PresetEditor::DrawModelField(json& data) {
    // 表示名ではなくモデルの参照名をJSONへ保存します。
    bool changed = false;
    std::string current = ReadString(data, "modelName", "");
    if (ImGui::BeginCombo("モデル", current.empty() ? "(なし)" : current.c_str())) {
        for (const std::string& modelName : modelNames_) {
            bool selected = modelName == current;
            if (ImGui::Selectable(modelName.c_str(), selected)) {
                data["modelName"] = modelName;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    changed |= DrawStringField(data, "モデル名", "modelName", "");
    return changed;
}

bool PresetEditor::DrawCollider(json& data) {
    // colliderが欠けた旧Presetも編集できるよう、初回表示時に空オブジェクトを補います。
    bool changed = false;
    if (!data.contains("collider") || !data["collider"].is_object()) {
        data["collider"] = json::object();
        changed = true;
    }

    json& collider = data["collider"];
    int type = ReadInt(collider, "type", 0);
    const char* labels[] = { "なし", "球", "AABB", "OBB" };
    if (ImGui::Combo("形状", &type, labels, 4)) {
        collider["type"] = type;
        changed = true;
    }
    changed |= DrawVector3Field(collider, "中心", "center", { 0.0f, 0.0f, 0.0f }, 0.05f);
    changed |= DrawVector3Field(collider, "サイズ", "size", { 1.0f, 1.0f, 1.0f }, 0.05f);
    changed |= DrawVector3Field(collider, "回転", "rotation", { 0.0f, 0.0f, 0.0f }, 0.01f);
    return changed;
}

bool PresetEditor::DrawStringField(json& data, const char* label, const char* key, const std::string& defaultValue) {
    // 入力が変わった時だけJSONを更新し、未編集値には既定値を使います。
    char buffer[256]{};
    std::string value = ReadString(data, key, defaultValue);
    strncpy_s(buffer, value.c_str(), _TRUNCATE);
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
        data[key] = std::string(buffer);
        return true;
    }
    return false;
}

bool PresetEditor::DrawFloatField(json& data, const char* label, const char* key, float defaultValue, float speed) {
    // Drag操作が確定した値だけ保存し、呼び出し側へ変更を伝えます。
    float value = ReadFloat(data, key, defaultValue);
    if (ImGui::DragFloat(label, &value, speed)) {
        data[key] = value;
        return true;
    }
    return false;
}

bool PresetEditor::DrawIntField(json& data, const char* label, const char* key, int defaultValue) {
    // 整数パラメータも共通の変更フラグで保存処理へ渡します。
    int value = ReadInt(data, key, defaultValue);
    if (ImGui::DragInt(label, &value, 1.0f)) {
        data[key] = value;
        return true;
    }
    return false;
}

bool PresetEditor::DrawColorField(json& data, const char* label, const char* key, const Vector4& defaultValue) {
    // 壊れた配列要素は既定色を保ち、編集時にRGBA配列へ正規化します。
    float color[4] = { defaultValue.x, defaultValue.y, defaultValue.z, defaultValue.w };
    if (data.contains(key) && data[key].is_array() && data[key].size() >= 4) {
        for (int i = 0; i < 4; ++i) {
            if (data[key][i].is_number()) {
                color[i] = data[key][i].get<float>();
            }
        }
    }
    if (ImGui::ColorEdit4(label, color)) {
        data[key] = { color[0], color[1], color[2], color[3] };
        return true;
    }
    return false;
}

bool PresetEditor::DrawVector3Field(json& data, const char* label, const char* key, const Vector3& defaultValue, float speed) {
    Vector3 value = ReadVector3(data, key, defaultValue);
    float values[3] = { value.x, value.y, value.z };
    if (ImGui::DragFloat3(label, values, speed)) {
        data[key] = { values[0], values[1], values[2] };
        // 保存されたQuaternionより、今回編集したEuler角を優先します。
        if (std::string(key) == "rotate") data.erase("quaternion");
        return true;
    }
    return false;
}

bool PresetEditor::DrawParamFloat(json& param, const char* label, const char* key, float defaultValue, float speed) {
    // param直下の浮動小数点設定も通常フィールドと同じ更新経路へ渡します。
    return DrawFloatField(param, label, key, defaultValue, speed);
}

bool PresetEditor::DrawParamInt(json& param, const char* label, const char* key, int defaultValue) {
    // param直下の整数設定を共通UIへ委譲します。
    return DrawIntField(param, label, key, defaultValue);
}

bool PresetEditor::DrawParamBool(json& param, const char* label, const char* key, bool defaultValue) {
    // 未登録値は既定値で表示し、チェック変更時に初めてJSONへ確定します。
    bool value = ReadBool(param, key, defaultValue);
    if (ImGui::Checkbox(label, &value)) {
        param[key] = value;
        return true;
    }
    return false;
}

bool PresetEditor::DrawParamIntCombo(json& param, const char* label, const char* key, int defaultValue, const std::vector<const char*>& labels) {
    // UIの選択番号をそのまま保存形式の整数値に対応させます。
    int value = ReadInt(param, key, defaultValue);
    if (ImGui::Combo(label, &value, labels.data(), static_cast<int>(labels.size()))) {
        param[key] = value;
        return true;
    }
    return false;
}

json& PresetEditor::EnsureParam(json& data) {
    // 旧Presetでparamが欠けていても、型別の編集先を必ずObjectにします。
    if (!data.contains("param") || !data["param"].is_object()) {
        data["param"] = json::object();
    }
    return data["param"];
}

void PresetEditor::AddBlankPreset(Category category) {
    // 新規Presetを保存して即選択し、続けて同じ画面から調整できる状態にします。
    if (newName_[0] == '\0') {
        return;
    }

    std::string name = newName_;
    json data = BuildBlankPreset(name, category);
    PresetManager::GetInstance()->GetPreset(name) = data;
    PresetManager::GetInstance()->SaveAll();
    selectedName_ = name;
    newName_[0] = '\0';
}

json PresetEditor::BuildBlankPreset(const std::string& name, Category category) const {
    // 敵はモデルと倍率をステータス側で決めるため、他カテゴリと初期項目を分けます。
    json data = json::object();
    data["name"] = name;
    data["presetDisplayName"] = name;
    data["presetUsage"] = category == Category::Model ? "装飾" : GetCategoryLabel(category);
    if (category != Category::Enemy) {
        data["modelName"] = "Stages/block";
    }
    data["translate"] = { 0.0f, 0.0f, 0.0f };
    data["rotate"] = { 0.0f, 0.0f, 0.0f };
    if (category != Category::Enemy) {
        data["scale"] = { 1.0f, 1.0f, 1.0f };
    }
    data["color"] = { 1.0f, 1.0f, 1.0f, 1.0f };
    data["materialType"] = 0;
    data["emissive"] = 1.0f;
    data["myEventID"] = -1;
    data["targetID"] = -1;
    data["eventType"] = 0;
    data["collisionAttribute"] = 0;
    data["collisionMask"] = 0;
    data["enemyType"] = category == Category::Enemy ? "Slime" : "";
    data["gimmickType"] = category == Category::Gimmick ? "Default" : "";
    data["itemType"] = category == Category::Item ? "Heal" : "";
    data["collider"] = {
        { "type", 2 },
        { "center", { 0.0f, 0.0f, 0.0f } },
        { "size", { 1.0f, 1.0f, 1.0f } },
        { "rotation", { 0.0f, 0.0f, 0.0f } }
    };
    if (category == Category::Enemy) {
        data["param"] = { { "enemyType", "Slime" } };
    } else {
        data["param"] = {
            { "speed", 1.0f },
            { "gravity", 50.0f },
            { "interval", 3.0f },
            { "gimmickType", category == Category::Gimmick ? "Default" : "" },
            { "itemType", category == Category::Item ? "Heal" : "" },
            { "healAmount", 1.0f }
        };
    }

    const_cast<PresetEditor*>(this)->ApplyHiddenCategoryFields(data, category);
    const std::string type =
        category == Category::Enemy ? "Slime" :
        category == Category::Gimmick ? "Default" :
        category == Category::Item ? "Heal" : "";
    const_cast<PresetEditor*>(this)->ApplyTypeDefaults(data, category, type);
    return data;
}

#endif
