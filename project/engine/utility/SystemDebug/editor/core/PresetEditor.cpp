#include "PresetEditor.h"

#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

// Singletonの寿命はEditor全体と同じで、外部から所有権を受け取りません。
PresetEditor* PresetEditor::GetInstance() {
    static PresetEditor instance;
    return &instance;
}

std::string PresetEditor::GetName() { return "プリセットエディタ (Preset Editor)"; }

#ifdef USE_IMGUI

// パレットの表示、選択、ドラッグ配置を担当します。
void PresetEditor::Initialize() {
    PresetManager::GetInstance()->Initialize();
    RefreshModelList();
}

void PresetEditor::SetPlacePresetCallback(std::function<void(const std::string&)> callback) {
    placePresetCallback_ = std::move(callback);
}

void PresetEditor::SetBrushPresetCallback(std::function<void(const std::string&)> callback) {
    brushPresetCallback_ = std::move(callback);
}

void PresetEditor::SetThumbnailProvider(std::function<uint64_t(const std::string&)> provider) {
    thumbnailProvider_ = std::move(provider);
}

void PresetEditor::DrawImGui() {
    HandleDeferredDelete();

    ImGui::TextColored(ImVec4(0.35f, 0.9f, 1.0f, 1.0f), "配置プリセットエディタ");
    ImGui::TextDisabled("左のサムネイルをGame Viewへドラッグして配置。右側で配置前の数値を調整します。");
    ImGui::Separator();

    ImGui::Columns(2, "PresetPlacementEditorColumns", true);
    ImGui::SetColumnWidth(0, 460.0f);
    DrawPalettePane();
    ImGui::NextColumn();
    DrawSettingsPane();
    ImGui::Columns(1);
}

void PresetEditor::DrawPalettePane() {
    ImGui::TextDisabled("種類で絞り込み");
    DrawCategoryButton("すべて", Category::All);
    ImGui::SameLine();
    DrawCategoryButton("敵", Category::Enemy);
    ImGui::SameLine();
    DrawCategoryButton("ギミック", Category::Gimmick);
    ImGui::SameLine();
    DrawCategoryButton("アイテム", Category::Item);
    ImGui::SameLine();
    DrawCategoryButton("モデル", Category::Model);

    ImGui::PushItemWidth(-1.0f);
    ImGui::InputText("##PresetSearch", searchBuffer_, sizeof(searchBuffer_));
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("名前、用途、モデル名、敵タイプ、ギミックタイプで検索します");
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

void PresetEditor::DrawCategoryButton(const char* label, Category category) {
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

void PresetEditor::DrawCreateButtons() {
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

void PresetEditor::DrawPresetList() {
    const auto& presets = PresetManager::GetInstance()->GetPresets();
    ImVec2 listSize = ImGui::GetContentRegionAvail();

    if (!ImGui::BeginChild("PresetAssetList", listSize, true)) {
        return;
    }

    struct PresetListEntry {
        const std::string* name = nullptr;
        const json* data = nullptr;
    };
    std::vector<PresetListEntry> entries;
    entries.reserve(presets.size());
    for (const auto& [name, data] : presets) {
        if (!MatchesCategory(data, activeCategory_) || !MatchesSearch(name, data)) {
            continue;
        }
        entries.push_back({ &name, &data });
    }

    std::stable_sort(entries.begin(), entries.end(), [](const PresetListEntry& lhs, const PresetListEntry& rhs) {
        return *lhs.name < *rhs.name;
    });

    for (const PresetListEntry& entry : entries) {
        DrawPresetListItem(*entry.name, *entry.data);
    }

    ImGui::EndChild();
}

void PresetEditor::DrawPresetListItem(const std::string& name, const json& data) {
    ImGui::PushID(name.c_str());

    bool selected = selectedName_ == name;
    Category category = DetectCategory(data);
    ImVec4 accent = GetCategoryColor(category);
    const float rowHeight = 96.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? ImVec4(0.18f, 0.25f, 0.38f, 1.0f) : ImVec4(0.11f, 0.12f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(0.70f, 0.86f, 1.0f, 1.0f) : ImVec4(0.25f, 0.26f, 0.30f, 1.0f));

    ImGui::BeginChild("PresetRow", ImVec2(0.0f, rowHeight), true);
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

void PresetEditor::DrawThumbnailButton(const std::string& name, const ImVec4& accent) {
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

void PresetEditor::DrawSettingsPane() {
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
        changed |= DrawModelField(data);
        changed |= DrawColorField(data, "色", "color", { 1.0f, 1.0f, 1.0f, 1.0f });
        changed |= DrawIntField(data, "マテリアルタイプ", "materialType", 0);
        changed |= DrawFloatField(data, "発光", "emissive", 1.0f, 0.05f);
    }

    if (ImGui::CollapsingHeader("配置時Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= DrawVector3Field(data, "位置オフセット", "translate", { 0.0f, 0.0f, 0.0f }, 0.1f);
        changed |= DrawVector3Field(data, "回転", "rotate", { 0.0f, 0.0f, 0.0f }, 0.01f);
        changed |= DrawVector3Field(data, "スケール", "scale", { 1.0f, 1.0f, 1.0f }, 0.05f);
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

#else

void PresetEditor::Initialize() {}

void PresetEditor::SetPlacePresetCallback(std::function<void(const std::string&)>) {}

void PresetEditor::SetBrushPresetCallback(std::function<void(const std::string&)>) {}

void PresetEditor::SetThumbnailProvider(std::function<uint64_t(const std::string&)>) {}

void PresetEditor::DrawImGui() {}

#endif
