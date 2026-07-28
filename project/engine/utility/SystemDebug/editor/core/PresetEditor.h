#pragma once

#ifdef USE_IMGUI

#include "IEditable.h"
#include "PresetManager.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class PresetEditor : public IEditable {
public:
    static PresetEditor* GetInstance() {
        static PresetEditor instance;
        return &instance;
    }

    void Initialize() {
        PresetManager::GetInstance()->Initialize();
        RefreshModelList();
    }

    void SetPlacePresetCallback(std::function<void(const std::string&)> callback) {
        placePresetCallback_ = std::move(callback);
    }

    void SetBrushPresetCallback(std::function<void(const std::string&)> callback) {
        brushPresetCallback_ = std::move(callback);
    }

    void SetThumbnailProvider(std::function<uint64_t(const std::string&)> provider) {
        thumbnailProvider_ = std::move(provider);
    }

    void DrawImGui() override {
        HandleDeferredDelete();

        ImGui::TextColored(ImVec4(0.35f, 0.9f, 1.0f, 1.0f), "配置プリセットエディタ");
        ImGui::TextDisabled("左のサムネイルをGame Viewへドラッグして配置。右側で配置前の数値を調整します。");
        ImGui::Separator();

        ImGui::Columns(2, "PresetPlacementEditorColumns", true);
        ImGui::SetColumnWidth(0, 380.0f);
        DrawPalettePane();
        ImGui::NextColumn();
        DrawSettingsPane();
        ImGui::Columns(1);
    }

    std::string GetName() override { return "プリセットエディタ (Preset Editor)"; }

private:
    enum class Category {
        All,
        Enemy,
        Gimmick,
        Item,
        Model
    };

    struct TypeOption {
        const char* value;
        const char* label;
    };

    PresetEditor() = default;
    ~PresetEditor() = default;

    void DrawPalettePane() {
        ImGui::TextDisabled("作成パレット");
        DrawCategoryButton("すべて", Category::All);
        ImGui::SameLine();
        DrawCategoryButton("敵", Category::Enemy);
        ImGui::SameLine();
        DrawCategoryButton("ギミック", Category::Gimmick);
        ImGui::SameLine();
        DrawCategoryButton("アイテム", Category::Item);

        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##PresetSearch", searchBuffer_, sizeof(searchBuffer_));
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("名前、モデル名、敵タイプ、ギミックタイプで検索します");
        }

        if (ImGui::Button("プリセット再読込")) {
            PresetManager::GetInstance()->Initialize();
        }
        ImGui::SameLine();
        if (ImGui::Button("モデル一覧更新")) {
            RefreshModelList();
        }

        ImGui::Separator();
        DrawCreateButtons();
        ImGui::Separator();
        DrawPresetList();
    }

    void DrawCategoryButton(const char* label, Category category) {
        bool selected = activeCategory_ == category;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.38f, 0.72f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.44f, 0.82f, 1.0f));
        }

        if (ImGui::Button(label)) {
            activeCategory_ = category;
        }

        if (selected) {
            ImGui::PopStyleColor(2);
        }
    }

    void DrawCreateButtons() {
        ImGui::TextDisabled("新規テンプレート");
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##NewPresetName", newName_, sizeof(newName_));
        ImGui::PopItemWidth();

        if (ImGui::Button("敵", ImVec2(72.0f, 0.0f))) AddBlankPreset(Category::Enemy);
        ImGui::SameLine();
        if (ImGui::Button("ギミック", ImVec2(92.0f, 0.0f))) AddBlankPreset(Category::Gimmick);
        ImGui::SameLine();
        if (ImGui::Button("アイテム", ImVec2(92.0f, 0.0f))) AddBlankPreset(Category::Item);
        ImGui::SameLine();
        if (ImGui::Button("モデル", ImVec2(72.0f, 0.0f))) AddBlankPreset(Category::Model);
    }

    void DrawPresetList() {
        const auto& presets = PresetManager::GetInstance()->GetPresets();
        ImVec2 listSize = ImGui::GetContentRegionAvail();

        if (!ImGui::BeginChild("PresetAssetList", listSize, true)) {
            return;
        }

        for (const auto& [name, data] : presets) {
            if (!MatchesCategory(data, activeCategory_) || !MatchesSearch(name, data)) {
                continue;
            }
            DrawPresetListItem(name, data);
        }

        ImGui::EndChild();
    }

    void DrawPresetListItem(const std::string& name, const json& data) {
        ImGui::PushID(name.c_str());

        bool selected = selectedName_ == name;
        Category category = DetectCategory(data);
        ImVec4 accent = GetCategoryColor(category);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? ImVec4(0.18f, 0.25f, 0.38f, 1.0f) : ImVec4(0.11f, 0.12f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(0.70f, 0.86f, 1.0f, 1.0f) : ImVec4(0.25f, 0.26f, 0.30f, 1.0f));

        ImGui::BeginChild("PresetRow", ImVec2(0.0f, 96.0f), true);
        DrawThumbnailButton(name, accent);

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);
            ImGui::Text("配置: %s", name.c_str());
            ImGui::TextDisabled("%s", ReadString(data, "modelName", "(なし)").c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemClicked()) {
            selectedName_ = name;
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        std::string displayName = ReadString(data, "name", name);
        if (ImGui::Selectable(displayName.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 22.0f))) {
            selectedName_ = name;
            if (ImGui::IsMouseDoubleClicked(0) && placePresetCallback_) {
                placePresetCallback_(selectedName_);
            }
        }
        ImGui::TextColored(accent, "%s", GetCategoryLabel(category));
        ImGui::TextDisabled("Model: %s", ShortModelName(ReadString(data, "modelName", "(なし)")).c_str());
        Vector3 scale = ReadVector3(data, "scale", { 1.0f, 1.0f, 1.0f });
        ImGui::TextDisabled("Scale: %.2f / %.2f / %.2f", scale.x, scale.y, scale.z);
        ImGui::EndGroup();

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    void DrawThumbnailButton(const std::string& name, const ImVec4& accent) {
        uint64_t gpuPtr = thumbnailProvider_ ? thumbnailProvider_(name) : 0;
        ImVec2 size = { 72.0f, 72.0f };

        if (gpuPtr != 0) {
            ImTextureID texId = static_cast<ImTextureID>(gpuPtr);
            ImGui::ImageButton(("##Thumb_" + name).c_str(), ImTextureRef(texId), size);
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x * 0.45f, accent.y * 0.45f, accent.z * 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
            ImGui::Button("3D", size);
            ImGui::PopStyleColor(2);
        }
    }

    void DrawSettingsPane() {
        PresetManager* manager = PresetManager::GetInstance();
        if (selectedName_.empty() || !manager->HasPreset(selectedName_)) {
            ImGui::TextDisabled("左の作成パレットから編集したいプリセットを選択してください。");
            return;
        }

        json& data = manager->GetPreset(selectedName_);
        Category category = DetectCategory(data);
        bool changed = false;
        if (category == Category::Enemy) {
            changed |= RemoveManagedEnemyFields(data);
        }

        std::string displayName = ReadString(data, "name", selectedName_);
        ImGui::TextColored(GetCategoryColor(category), "%s", displayName.c_str());
        if (displayName != selectedName_) {
            ImGui::TextDisabled("%s", selectedName_.c_str());
        }
        ImGui::SameLine();
        if (placePresetCallback_ && ImGui::Button("配置プレビュー")) {
            placePresetCallback_(selectedName_);
        }
        ImGui::SameLine();
        if (brushPresetCallback_ && ImGui::Button("ブラシ配置")) {
            brushPresetCallback_(selectedName_);
        }
        ImGui::SameLine();
        if (ImGui::Button("保存")) {
            manager->SaveAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("削除")) {
            requestDelete_ = true;
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("配置テンプレート", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawStringField(data, "表示名", "name", selectedName_);
            if (category == Category::Enemy) {
                ImGui::TextDisabled("敵のモデルとタイプ共通スケールはステータス管理で設定します。");
            } else {
                changed |= DrawModelField(data);
            }
            changed |= DrawColorField(data, "色", "color", { 1.0f, 1.0f, 1.0f, 1.0f });
            changed |= DrawIntField(data, "マテリアルタイプ", "materialType", 0);
            changed |= DrawFloatField(data, "発光", "emissive", 1.0f, 0.05f);
        }

        if (ImGui::CollapsingHeader("配置時Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawVector3Field(data, "位置オフセット", "translate", { 0.0f, 0.0f, 0.0f }, 0.1f);
            changed |= DrawVector3Field(data, "回転", "rotate", { 0.0f, 0.0f, 0.0f }, 0.01f);
            if (category == Category::Enemy) {
                ImGui::TextDisabled("スケールはステータス管理の敵タイプ共通値を使用します。");
            } else {
                changed |= DrawVector3Field(data, "スケール", "scale", { 1.0f, 1.0f, 1.0f }, 0.05f);
            }
        }

        if (category == Category::Enemy) {
            changed |= DrawEnemySettings(data);
        }
        else if (category == Category::Gimmick) {
            changed |= DrawGimmickSettings(data);
        }
        else if (category == Category::Item) {
            changed |= DrawItemSettings(data);
        }

        if (ImGui::CollapsingHeader("当たり判定", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawCollider(data);
            changed |= DrawIntField(data, "自分の属性", "collisionAttribute", 0);
            changed |= DrawIntField(data, "衝突対象", "collisionMask", 0);
        }

        if (ImGui::CollapsingHeader("イベント連携", category == Category::Gimmick ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            changed |= DrawIntField(data, "自分ID", "myEventID", -1);
            changed |= DrawIntField(data, "送信先ID", "targetID", -1);
            changed |= DrawIntField(data, "イベント種別", "eventType", 0);
        }

        if (changed) {
            ApplyHiddenCategoryFields(data, category);
            manager->SaveAll();
        }
    }

    bool DrawEnemySettings(json& data) {
        bool changed = false;
        ApplyHiddenCategoryFields(data, Category::Enemy);
        json& param = EnsureParam(data);

        if (ImGui::CollapsingHeader("敵パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawTypeCombo("敵の種類", data, param, "enemyType", GetEnemyOptions(), Category::Enemy);
            ImGui::TextDisabled("HP・攻撃力・移動・重力などはステータス管理でタイプごとに編集します。");
        }
        return changed;
    }

    bool RemoveManagedEnemyFields(json& data) {
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

    bool DrawGimmickSettings(json& data) {
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
                changed |= DrawParamFloat(param, "移動量", "moveAmount", 10.0f, 0.1f);
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
            else if (type == "Switch") {
                changed |= DrawParamIntCombo(param, "スイッチ方式", "switchMode", 0, { "押している間", "押すたび切替", "一定時間" });
                changed |= DrawParamFloat(param, "有効時間", "interval", 3.0f, 0.05f);
            }
            else if (type == "EventReceiver") {
                changed |= DrawParamIntCombo(param, "動作モード", "actionMode", 0, { "出現", "Y移動", "X移動", "Z移動", "有効化", "無効化" });
                changed |= DrawParamFloat(param, "移動量", "moveAmount", 10.0f, 0.1f);
                changed |= DrawParamFloat(param, "移動速度", "moveSpeed", 6.0f, 0.05f);
                changed |= DrawParamBool(param, "開始時に有効", "startActive", false);
                changed |= DrawParamBool(param, "OFFで元に戻す", "returnOnOff", true);
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
            else if (type == "LaserEmitter" || type == "LaserNode") {
                changed |= DrawParamFloat(param, "ダメージ量", "speed", 1.0f, 0.5f);
                changed |= DrawParamFloat(param, "ダメージ間隔", "interval", 0.6f, 0.05f);
                changed |= DrawParamFloat(param, "レーザーの太さ", "moveAmount", 0.25f, 0.01f);
                changed |= DrawParamBool(param, "開始時に有効", "startActive", true);
                changed |= DrawParamBool(param, "OFFで停止", "returnOnOff", true);
            }
            else if (type == "StageGate") {
                changed |= DrawParamInt(param, "ステージ番号", "colorType", 0);
            }
            else {
                ImGui::TextDisabled("このギミックは追加パラメータなしで配置できます。");
            }
        }
        return changed;
    }

    bool DrawItemSettings(json& data) {
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

    bool DrawTypeCombo(const char* label, json& data, json& param, const char* key, const std::vector<TypeOption>& options, Category category) {
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

    void ApplyHiddenCategoryFields(json& data, Category category) {
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
            data["type"] = "Model";
            data["saveCategory"] = "Object";
        }
    }

    void ApplyTypeDefaults(json& data, Category category, const std::string& type) {
        if (category == Category::Enemy) {
            if (type == "Bat") {
                data["animation"]["animName"] = "ArmatureAction";
                data["animation"]["isAnimLoop"] = true;
                SetColliderDefaults(data, 1, { 0.85f, 0.85f, 0.85f });
            }
            else if (type == "BeamDrone") {
                SetColliderDefaults(data, 1, { 1.1f, 1.1f, 1.1f });
            }
            else if (type == "GiantSlime") {
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
            else if (type == "HookAnchor" || type == "LaserNode") {
                data["modelName"] = "Primitives/sphere";
                data["scale"] = { 1.2f, 1.2f, 1.2f };
                SetColliderDefaults(data, 1, { 2.5f, 2.5f, 2.5f });
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
    }

    void SetColliderDefaults(json& data, int type, const Vector3& size) {
        if (!data.contains("collider") || !data["collider"].is_object()) {
            data["collider"] = json::object();
        }
        data["collider"]["type"] = type;
        data["collider"]["size"] = { size.x, size.y, size.z };
        if (!data["collider"].contains("center")) data["collider"]["center"] = { 0.0f, 0.0f, 0.0f };
        if (!data["collider"].contains("rotation")) data["collider"]["rotation"] = { 0.0f, 0.0f, 0.0f };
    }

    bool DrawModelField(json& data) {
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

    bool DrawCollider(json& data) {
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

    bool DrawStringField(json& data, const char* label, const char* key, const std::string& defaultValue) {
        char buffer[256]{};
        std::string value = ReadString(data, key, defaultValue);
        strncpy_s(buffer, value.c_str(), _TRUNCATE);
        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            data[key] = std::string(buffer);
            return true;
        }
        return false;
    }

    bool DrawFloatField(json& data, const char* label, const char* key, float defaultValue, float speed) {
        float value = ReadFloat(data, key, defaultValue);
        if (ImGui::DragFloat(label, &value, speed)) {
            data[key] = value;
            return true;
        }
        return false;
    }

    bool DrawIntField(json& data, const char* label, const char* key, int defaultValue) {
        int value = ReadInt(data, key, defaultValue);
        if (ImGui::DragInt(label, &value, 1.0f)) {
            data[key] = value;
            return true;
        }
        return false;
    }

    bool DrawColorField(json& data, const char* label, const char* key, const Vector4& defaultValue) {
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

    bool DrawVector3Field(json& data, const char* label, const char* key, const Vector3& defaultValue, float speed) {
        Vector3 value = ReadVector3(data, key, defaultValue);
        float values[3] = { value.x, value.y, value.z };
        if (ImGui::DragFloat3(label, values, speed)) {
            data[key] = { values[0], values[1], values[2] };
            return true;
        }
        return false;
    }

    bool DrawParamFloat(json& param, const char* label, const char* key, float defaultValue, float speed) {
        return DrawFloatField(param, label, key, defaultValue, speed);
    }

    bool DrawParamInt(json& param, const char* label, const char* key, int defaultValue) {
        return DrawIntField(param, label, key, defaultValue);
    }

    bool DrawParamBool(json& param, const char* label, const char* key, bool defaultValue) {
        bool value = ReadBool(param, key, defaultValue);
        if (ImGui::Checkbox(label, &value)) {
            param[key] = value;
            return true;
        }
        return false;
    }

    bool DrawParamIntCombo(json& param, const char* label, const char* key, int defaultValue, const std::vector<const char*>& labels) {
        int value = ReadInt(param, key, defaultValue);
        if (ImGui::Combo(label, &value, labels.data(), static_cast<int>(labels.size()))) {
            param[key] = value;
            return true;
        }
        return false;
    }

    json& EnsureParam(json& data) {
        if (!data.contains("param") || !data["param"].is_object()) {
            data["param"] = json::object();
        }
        return data["param"];
    }

    void AddBlankPreset(Category category) {
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

    json BuildBlankPreset(const std::string& name, Category category) const {
        json data = json::object();
        data["name"] = name;
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

    void HandleDeferredDelete() {
        if (!requestDelete_ || selectedName_.empty()) {
            return;
        }

        PresetManager::GetInstance()->RemovePreset(selectedName_);
        selectedName_.clear();
        requestDelete_ = false;
    }

    void RefreshModelList() {
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

    bool MatchesSearch(const std::string& name, const json& data) const {
        std::string needle = ToLower(searchBuffer_);
        if (needle.empty()) {
            return true;
        }

        std::string haystack = name + " " +
            ReadString(data, "name", "") + " " +
            ReadString(data, "modelName", "") + " " +
            ReadString(data, "enemyType", "") + " " +
            ReadString(data, "gimmickType", "") + " " +
            ReadString(data, "itemType", "");
        if (data.contains("param") && data["param"].is_object()) {
            const json& param = data["param"];
            haystack += " " + ReadString(param, "enemyType", "");
            haystack += " " + ReadString(param, "gimmickType", "");
            haystack += " " + ReadString(param, "itemType", "");
        }
        return ToLower(haystack).find(needle) != std::string::npos;
    }

    bool MatchesCategory(const json& data, Category category) const {
        if (category == Category::All) {
            return true;
        }
        return DetectCategory(data) == category;
    }

    Category DetectCategory(const json& data) const {
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

    const char* GetCategoryLabel(Category category) const {
        switch (category) {
        case Category::Enemy: return "敵";
        case Category::Gimmick: return "ギミック";
        case Category::Item: return "アイテム";
        case Category::Model: return "モデル";
        case Category::All:
        default: return "すべて";
        }
    }

    ImVec4 GetCategoryColor(Category category) const {
        switch (category) {
        case Category::Enemy: return ImVec4(0.78f, 0.28f, 0.24f, 1.0f);
        case Category::Gimmick: return ImVec4(0.24f, 0.50f, 0.84f, 1.0f);
        case Category::Item: return ImVec4(0.28f, 0.66f, 0.38f, 1.0f);
        case Category::Model: return ImVec4(0.56f, 0.48f, 0.78f, 1.0f);
        case Category::All:
        default: return ImVec4(0.40f, 0.42f, 0.46f, 1.0f);
        }
    }

    std::string ShortModelName(const std::string& modelName) const {
        if (modelName.empty()) {
            return "(なし)";
        }

        size_t slash = modelName.find_last_of("/\\");
        if (slash == std::string::npos) {
            return modelName;
        }
        return modelName.substr(slash + 1);
    }

    std::string ReadString(const json& data, const char* key, const std::string& defaultValue) const {
        if (data.contains(key) && data[key].is_string()) {
            return data[key].get<std::string>();
        }
        return defaultValue;
    }

    float ReadFloat(const json& data, const char* key, float defaultValue) const {
        if (data.contains(key) && data[key].is_number()) {
            return data[key].get<float>();
        }
        return defaultValue;
    }

    int ReadInt(const json& data, const char* key, int defaultValue) const {
        if (data.contains(key) && data[key].is_number_integer()) {
            return data[key].get<int>();
        }
        if (data.contains(key) && data[key].is_number()) {
            return static_cast<int>(data[key].get<float>());
        }
        return defaultValue;
    }

    bool ReadBool(const json& data, const char* key, bool defaultValue) const {
        if (data.contains(key) && data[key].is_boolean()) {
            return data[key].get<bool>();
        }
        return defaultValue;
    }

    Vector3 ReadVector3(const json& data, const char* key, const Vector3& defaultValue) const {
        Vector3 value = defaultValue;
        if (data.contains(key) && data[key].is_array() && data[key].size() >= 3) {
            if (data[key][0].is_number()) value.x = data[key][0].get<float>();
            if (data[key][1].is_number()) value.y = data[key][1].get<float>();
            if (data[key][2].is_number()) value.z = data[key][2].get<float>();
        }
        return value;
    }

    std::string ToLower(const std::string& value) const {
        std::string result = value;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
            });
        return result;
    }

    std::vector<TypeOption> GetEnemyOptions() const {
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

    std::vector<TypeOption> GetGimmickOptions() const {
        return {
            { "Default", "通常" },
            { "MovingFloor", "移動床" },
            { "Trampoline", "トランポリン" },
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
            { "HookPullBlock", "フック可動ブロック" },
            { "OneWayFloor", "一方通行床" },
            { "LiquidLevel", "水位・マグマ上下" },
            { "ChainCollapseFloor", "連鎖崩れ床" },
            { "RotatingFloor", "回転床" },
            { "RotatingPillar", "回転柱" },
            { "PhaseFlipFloor", "順番反転床" },
            { "LaserEmitter", "レーザー発生器" },
            { "LaserNode", "レーザー接続ノード" },
            { "StageGate", "ステージゲート" }
        };
    }

    std::vector<TypeOption> GetItemOptions() const {
        return {
            { "Heal", "回復アイテム" }
        };
    }

private:
    std::function<void(const std::string&)> placePresetCallback_;
    std::function<void(const std::string&)> brushPresetCallback_;
    std::function<uint64_t(const std::string&)> thumbnailProvider_;
    std::vector<std::string> modelNames_;
    std::string selectedName_;
    Category activeCategory_ = Category::All;
    char searchBuffer_[128]{};
    char newName_[64] = "NewPreset";
    bool requestDelete_ = false;
};

#else

#include "IEditable.h"

#include <cstdint>
#include <functional>
#include <string>

class PresetEditor : public IEditable {
public:
    static PresetEditor* GetInstance() {
        static PresetEditor instance;
        return &instance;
    }

    void Initialize() {}
    void SetPlacePresetCallback(std::function<void(const std::string&)>) {}
    void SetBrushPresetCallback(std::function<void(const std::string&)>) {}
    void SetThumbnailProvider(std::function<uint64_t(const std::string&)>) {}
    void DrawImGui() override {}
    std::string GetName() override { return "プリセットエディタ (Preset Editor)"; }

private:
    PresetEditor() = default;
};

#endif
