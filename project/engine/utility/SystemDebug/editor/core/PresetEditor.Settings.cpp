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
    ApplyHiddenCategoryFields(data, Category::Enemy);
    json& param = EnsureParam(data);
    if (ImGui::CollapsingHeader("敵タイプ", ImGuiTreeNodeFlags_DefaultOpen)) {
        return DrawTypeCombo("敵の種類", data, param, "enemyType", GetEnemyOptions(), Category::Enemy);
    }
    return false;
}

bool PresetEditor::RemoveManagedEnemyFields(json&) {
    return false;
}

bool PresetEditor::DrawGimmickSettings(json& data) {
    ApplyHiddenCategoryFields(data, Category::Gimmick);
    json& param = EnsureParam(data);
    if (ImGui::CollapsingHeader("ギミックタイプ", ImGuiTreeNodeFlags_DefaultOpen)) {
        return DrawTypeCombo("ギミックの種類", data, param, "gimmickType", GetGimmickOptions(), Category::Gimmick);
    }
    return false;
}

bool PresetEditor::DrawItemSettings(json& data) {
    ApplyHiddenCategoryFields(data, Category::Item);
    json& param = EnsureParam(data);
    if (ImGui::CollapsingHeader("アイテムタイプ", ImGuiTreeNodeFlags_DefaultOpen)) {
        return DrawTypeCombo("アイテムの種類", data, param, "itemType", GetItemOptions(), Category::Item);
    }
    return false;
}

bool PresetEditor::DrawTypeCombo(const char* label, json& data, json& param, const char* key, const std::vector<TypeOption>& options, Category category) {
    if (options.empty()) {
        ImGui::TextDisabled("Factoryへの登録はありません");
        return false;
    }
    const std::string current = ReadString(data, key, ReadString(param, key, ""));
    int currentIndex = 0;
    for (int index = 0; index < static_cast<int>(options.size()); ++index) {
        if (current == options[index].value) { currentIndex = index; break; }
    }
    std::vector<const char*> labels;
    labels.reserve(options.size());
    for (const TypeOption& option : options) labels.push_back(option.label.c_str());
    if (!ImGui::Combo(label, &currentIndex, labels.data(), static_cast<int>(labels.size()))) return false;
    data[key] = options[currentIndex].value;
    param[key] = options[currentIndex].value;
    ApplyHiddenCategoryFields(data, category);
    ApplyTypeDefaults(data, category, options[currentIndex].value);
    return true;
}

void PresetEditor::ApplyHiddenCategoryFields(json& data, Category category) {
    if (category == Category::Enemy) {
        data["type"] = "Enemy";
        data["saveCategory"] = "Enemy";
    } else if (category == Category::Gimmick) {
        data["type"] = "Gimmick";
        data["saveCategory"] = "Object";
    } else if (category == Category::Item) {
        data["type"] = "Item";
        data["saveCategory"] = "Object";
    } else {
        data["type"] = "Model";
        data["saveCategory"] = "Object";
    }
}

void PresetEditor::ApplyTypeDefaults(json& data, Category category, const std::string&) {
    if (category == Category::Enemy || category == Category::Item) {
        SetColliderDefaults(data, 1, { 1.0f, 1.0f, 1.0f });
    } else {
        SetColliderDefaults(data, 3, { 1.0f, 1.0f, 1.0f });
    }
}

void PresetEditor::SetColliderDefaults(json& data, int type, const Vector3& size) {
    if (!data.contains("collider") || !data["collider"].is_object()) {
        data["collider"] = json::object();
    }
    data["collider"]["type"] = type;
    data["collider"]["size"] = { size.x, size.y, size.z };
    if (!data["collider"].contains("center")) data["collider"]["center"] = { 0.0f, 0.0f, 0.0f };
    if (!data["collider"].contains("rotation")) data["collider"]["rotation"] = { 0.0f, 0.0f, 0.0f };
}

bool PresetEditor::DrawModelField(json& data) {
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
    float value = ReadFloat(data, key, defaultValue);
    if (ImGui::DragFloat(label, &value, speed)) {
        data[key] = value;
        return true;
    }
    return false;
}

bool PresetEditor::DrawIntField(json& data, const char* label, const char* key, int defaultValue) {
    int value = ReadInt(data, key, defaultValue);
    if (ImGui::DragInt(label, &value, 1.0f)) {
        data[key] = value;
        return true;
    }
    return false;
}

bool PresetEditor::DrawColorField(json& data, const char* label, const char* key, const Vector4& defaultValue) {
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
        return true;
    }
    return false;
}

bool PresetEditor::DrawParamFloat(json& param, const char* label, const char* key, float defaultValue, float speed) {
    return DrawFloatField(param, label, key, defaultValue, speed);
}

bool PresetEditor::DrawParamInt(json& param, const char* label, const char* key, int defaultValue) {
    return DrawIntField(param, label, key, defaultValue);
}

bool PresetEditor::DrawParamBool(json& param, const char* label, const char* key, bool defaultValue) {
    bool value = ReadBool(param, key, defaultValue);
    if (ImGui::Checkbox(label, &value)) {
        param[key] = value;
        return true;
    }
    return false;
}

bool PresetEditor::DrawParamIntCombo(json& param, const char* label, const char* key, int defaultValue, const std::vector<const char*>& labels) {
    int value = ReadInt(param, key, defaultValue);
    if (ImGui::Combo(label, &value, labels.data(), static_cast<int>(labels.size()))) {
        param[key] = value;
        return true;
    }
    return false;
}

json& PresetEditor::EnsureParam(json& data) {
    if (!data.contains("param") || !data["param"].is_object()) {
        data["param"] = json::object();
    }
    return data["param"];
}

void PresetEditor::AddBlankPreset(Category category) {
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
    json data = json::object();
    data["name"] = name;
    data["modelName"] = "Primitives/cube";
    data["translate"] = { 0.0f, 0.0f, 0.0f };
    data["rotate"] = { 0.0f, 0.0f, 0.0f };
    data["scale"] = { 1.0f, 1.0f, 1.0f };
    data["color"] = { 1.0f, 1.0f, 1.0f, 1.0f };
    data["materialType"] = 0;
    data["emissive"] = 1.0f;
    data["myEventID"] = -1;
    data["targetID"] = -1;
    data["eventType"] = 0;
    data["collisionAttribute"] = 0;
    data["collisionMask"] = 0;
    data["enemyType"] = "";
    data["gimmickType"] = "";
    data["itemType"] = "";
    data["param"] = json::object();
    data["collider"] = {
        { "type", category == Category::Enemy || category == Category::Item ? 1 : 3 },
        { "center", { 0.0f, 0.0f, 0.0f } },
        { "size", { 1.0f, 1.0f, 1.0f } },
        { "rotation", { 0.0f, 0.0f, 0.0f } }
    };
    const std::vector<TypeOption> options =
        category == Category::Enemy ? GetEnemyOptions() :
        category == Category::Gimmick ? GetGimmickOptions() :
        category == Category::Item ? GetItemOptions() : std::vector<TypeOption>{};
    if (!options.empty()) {
        const std::string& type = options.front().value;
        const char* key = category == Category::Enemy ? "enemyType" :
            category == Category::Gimmick ? "gimmickType" : "itemType";
        data[key] = type;
        data["param"][key] = type;
    }
    const_cast<PresetEditor*>(this)->ApplyHiddenCategoryFields(data, category);
    return data;
}

#endif
