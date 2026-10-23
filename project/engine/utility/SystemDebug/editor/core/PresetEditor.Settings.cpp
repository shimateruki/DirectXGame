#include "PresetEditor.h"

#include "ModelManager.h"
#include "EditorObjectFactoryRegistry.h"
#include "EditorPropertyDrawer.h"
#include "EditorPropertyJsonBinding.h"
#include "SceneManager.h"
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
        ImGui::TextDisabled("HP・攻撃力・移動・重力などは「敵の調整・攻撃プレビュー」の「基本ステータス」で編集します。");
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

PresetEditor::PropertyEditorContext* PresetEditor::GetPropertyEditor(const std::string& type, bool commonObject) {
    const std::string cacheKey = (commonObject ? "Object/" : "Gimmick/") + type;
    const auto found = propertyEditors_.find(cacheKey);
    if (found != propertyEditors_.end()) return &found->second;
    auto* scene = SceneManager::GetInstance()->GetCurrentScene();
    if (!scene || !scene->GetObject3dCommon()) return nullptr;
    std::unique_ptr<engine::Object3d> object;
    if (commonObject) {
        object = std::make_unique<engine::Object3d>();
        object->Initialize(scene->GetObject3dCommon());
    } else {
        object = EditorObjectFactoryRegistry::Create(EditorObjectFactoryRegistry::Kind::Gimmick, type, scene->GetObject3dCommon());
    }
    if (!object) return nullptr;
    PropertyEditorContext context;
    context.defaults = object->ExportToJson();
    context.object = std::move(object);
    return &propertyEditors_.emplace(cacheKey, std::move(context)).first->second;
}

bool PresetEditor::DrawGimmickSettings(json& data) {
    if (!ImGui::CollapsingHeader("ギミックパラメータ", ImGuiTreeNodeFlags_DefaultOpen)) return false;
    bool changed = false;
    // 種類を変更した時だけ初期値を入れ、閲覧だけで既存Presetを補完しません。
    const auto options = GetGimmickOptions();
    const auto readType = [&]() {
        const auto param = data.find("param");
        return ReadString(data, "gimmickType", param != data.end() && param->is_object()
            ? ReadString(*param, "gimmickType", "") : std::string());
    };
    const std::string type = readType();
    const auto current = std::find_if(options.begin(), options.end(), [&](const TypeOption& option) { return type == option.value; });
    const char* preview = current != options.end() ? current->label : type.c_str();
    if (ImGui::BeginCombo("ギミックの種類", preview)) {
        for (const auto& option : options) {
            if (ImGui::Selectable(option.label, type == option.value)) {
                data["gimmickType"] = option.value;
                ApplyTypeDefaults(data, Category::Gimmick, option.value);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    const std::string selected = readType();
    auto* context = GetPropertyEditor(selected);
    if (!context) {
        ImGui::TextDisabled("未登録の種類です。保存済みの値は保持します。");
        return changed;
    }
    auto& object = *context->object;
    // 前のPresetの省略値が混ざらないよう、Factoryの初期値から現在のJSONを復元します。
    if (context->source != data) {
        EditorPropertyJsonBinding::Restore(object, context->defaults, data);
        context->source = data;
    }
    auto* registry = EditorPropertyRegistry::GetInstance();
    for (const auto* component : registry->GetComponentsForObject(&object)) {
        if (component->typeId.rfind("Game_", 0) != 0) continue;
        changed |= DrawRegisteredSettings(data, *context, component->typeId);
    }
    context->source = data;
    return changed;
}


// ギミックとイベントで同じ入力・保存差分処理を使い、未知のJSON項目を保持します。
bool PresetEditor::DrawRegisteredSettings(json& data, PropertyEditorContext& context, const std::string& componentType) {
    auto& object = *context.object;
    if (context.source != data) {
        EditorPropertyJsonBinding::Restore(object, context.defaults, data);
        context.source = data;
    }
    auto* registry = EditorPropertyRegistry::GetInstance();
    const auto* component = registry->FindComponent(componentType);
    if (!component) return false;
    bool changed = false;
    if (!component->description.empty()) ImGui::TextWrapped("%s", component->description.c_str());
    for (const auto* property : registry->GetPropertiesForComponent(componentType)) {
        if (!registry->IsApplicable(&object, property->path) ||
            (property->visible && !property->visible(object))) continue;
        ImGui::PushID(property->path.c_str());
        ImGui::TextUnformatted(property->displayName.c_str());
        auto value = property->displayValue ? property->displayValue(object) : registry->GetValue(&object, property->path);
        auto effective = *property;
        if (property->maximum) effective.ui.maxValue = property->maximum(object);
        const bool disabled = HasEditorPropertyFlag(property->flags, EditorPropertyFlags::ReadOnly) ||
            (property->enabled && !property->enabled(object));
        ImGui::BeginDisabled(disabled);
        EditorPropertyDrawOptions drawOptions;
        drawOptions.compact = true;
        if (EditorPropertyDrawer::DrawValue(effective, value, "##Value", drawOptions))
            changed |= EditorPropertyJsonBinding::Apply(object, *property, data, value);
        ImGui::EndDisabled();
        if (property->help) {
            const auto help = property->help(object);
            if (!help.empty()) ImGui::TextWrapped("%s", help.c_str());
        }
        ImGui::PopID();
    }
    context.source = data;
    return changed;
}

bool PresetEditor::DrawEventSettings(json& data) {
    auto* context = GetPropertyEditor("ContactEvent", true);
    if (!context) {
        ImGui::TextDisabled("シーンの初期化後にイベントを編集できます。");
        return false;
    }
    return DrawRegisteredSettings(data, *context, "ContactEvent");
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
        if (const auto* context = GetPropertyEditor(type)) {
            const auto& defaults = context->defaults;
            for (const char* key : {"param", "modelName", "scale", "color", "materialType", "blendMode", "emissive",
                 "metallic", "roughness", "collider", "collisionAttribute", "collisionMask", "collisionAttributeIds", "collisionMaskIds"})
                if (defaults.contains(key)) data[key] = defaults[key];
            data["gimmickType"] = type;
            EnsureParam(data)["gimmickType"] = type;
        }
    }

    // ここで作った数値Maskは現在のJSON定義の値なので、IDも同時に保存します。
    if (data.contains("collisionAttribute") && data["collisionAttribute"].is_number_unsigned()) {
        data["collisionAttributeIds"] = CollisionCategories::GetIds(data["collisionAttribute"].get<uint32_t>());
    }

    if (data.contains("collisionMask") && data["collisionMask"].is_number_unsigned()) {
        data["collisionMaskIds"] = CollisionCategories::GetIds(data["collisionMask"].get<uint32_t>());
    }
}

void PresetEditor::SetColliderDefaults(json& data, int type, const engine::Vector3& size) {
    // 形状と大きさだけ更新し、調整済みの中心・回転は残します。
    if (!data.contains("collider") || !data["collider"].is_object()) {
        data["collider"] = json::object();
    }

    data["collider"]["type"] = type;
    data["collider"]["size"] = { size.x, size.y, size.z };

    if (!data["collider"].contains("center"))
        data["collider"]["center"] = { 0.0f, 0.0f, 0.0f };
    if (!data["collider"].contains("rotation"))
        data["collider"]["rotation"] = { 0.0f, 0.0f, 0.0f };
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

bool PresetEditor::DrawColorField(json& data, const char* label, const char* key, const engine::Vector4& defaultValue) {
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

bool PresetEditor::DrawVector3Field(json& data, const char* label, const char* key, const engine::Vector3& defaultValue, float speed) {
    engine::Vector3 value = ReadVector3(data, key, defaultValue);
    float values[3] = { value.x, value.y, value.z };

    if (ImGui::DragFloat3(label, values, speed)) {
        data[key] = { values[0], values[1], values[2] };

        // 保存されたQuaternionより、今回編集したEuler角を優先します。
        if (std::string(key) == "rotate")
            data.erase("quaternion");
        return true;
    }

    return false;
}

bool PresetEditor::DrawParamFloat(json& param, const char* label, const char* key, float defaultValue, float speed) {
    // param直下の浮動小数点設定も通常フィールドと同じ更新経路へ渡します。
    return DrawFloatField(param, label, key, defaultValue, speed);
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

    // 描画側の初期値と一致させ、作成直後からPBR設定をJSON上で確認できるようにします。
    data["metallic"] = 0.0f;
    data["roughness"] = 0.5f;
    data["emissive"] = 1.0f;
    data["enableLighting"] = true;
    data["enableEnvMap"] = false;
    data["envIntensity"] = 1.0f;
    data["enableNormalMap"] = false;
    data["myEventID"] = -1;
    data["targetID"] = -1;
    data["eventType"] = 0;
    data["collisionAttribute"] = 0;
    data["collisionMask"] = 0;
    data["enemyType"] = category == Category::Enemy ? "Slime" : "";
    data["gimmickType"] = category == Category::Gimmick ? "Default" : "";
    data["itemType"] = "";
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
            { "itemType", "" }
        };
    }

    const_cast<PresetEditor*>(this)->ApplyHiddenCategoryFields(data, category);
    const std::string type =
        category == Category::Enemy ? "Slime" :
        category == Category::Gimmick ? "Default" :
        "";
    const_cast<PresetEditor*>(this)->ApplyTypeDefaults(data, category, type);
    return data;
}

#endif
