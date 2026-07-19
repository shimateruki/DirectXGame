#include "PropertyMatrixWindow.h"

#include "DebugEditor.h"
#include "EditorPropertyDrawer.h"
#include "EditorPropertyRegistry.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "PresetManager.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace {

using json = nlohmann::json;

bool ContainsText(const std::string& value, const char* filter) {
    return filter == nullptr || filter[0] == '\0' || value.find(filter) != std::string::npos;
}

float GetPropertyColumnWidth(const EditorPropertyDescriptor& property) {
    switch (property.type) {
    case EditorPropertyType::Bool:
        return 90.0f;
    case EditorPropertyType::Integer:
    case EditorPropertyType::Number:
        return 120.0f;
    case EditorPropertyType::Vector2:
        return 180.0f;
    case EditorPropertyType::Vector3:
        return 245.0f;
    case EditorPropertyType::Vector4:
        return 260.0f;
    case EditorPropertyType::String:
    default:
        return 180.0f;
    }
}

}

void PropertyMatrixWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void PropertyMatrixWindow::Open() {
    isOpen_ = true;
}

void PropertyMatrixWindow::DrawImGui() {
#ifdef USE_IMGUI
    const std::size_t selectedCount = editor_ ? editor_->GetSelectedObjectCount() : 0;

    ImGui::TextColored(ImVec4(0.42f, 0.86f, 1.0f, 1.0f),
        ICON_FA_TABLE " プロパティマトリクス");
    ImGui::TextWrapped("Hierarchyで複数選択したObjectを、横長の表で比較・一括編集します。");
    ImGui::Text("現在の選択: %zu Object", selectedCount);
    if (selectedCount < 2) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f),
            "CtrlまたはShiftで2個以上選択すると比較しやすくなります。");
    }

    if (ImGui::Button(ICON_FA_COLUMNS " 大きい表を開く", ImVec2(-1.0f, 34.0f))) {
        Open();
    }
    ImGui::Separator();
    ImGui::TextDisabled("セル編集と一括適用はCtrl+Z / Ctrl+Yの共通履歴へ登録されます。");
    ImGui::TextDisabled("Prefab Instanceの差分セルは橙色で表示され、InspectorからApply / Revertできます。");
#else
    (void)editor_;
#endif
}

void PropertyMatrixWindow::DrawWindow() {
#ifdef USE_IMGUI
    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ICON_FA_TABLE " Property Matrix###PropertyMatrix", &isOpen_,
        ImGuiWindowFlags_NoCollapse)) {
        DrawMatrixContents();
    }
    ImGui::End();

    if (!isOpen_) {
        CommitActiveEdit("Property Matrix Cell Edit");
    }
#endif
}

void PropertyMatrixWindow::DrawMatrixContents() {
#ifdef USE_IMGUI
    if (!editor_) {
        ImGui::TextDisabled("DebugEditorへ接続されていません。");
        return;
    }

    DrawFilters();
    std::vector<Object3d*> targets = CollectTargets();
    std::vector<const EditorPropertyDescriptor*> properties = CollectProperties(targets);

    ImGui::Text("表示対象: %zu / 選択中: %zu", targets.size(), editor_->GetSelectedObjectCount());
    ImGui::SameLine();
    ImGui::TextDisabled("| 列: %zu", properties.size());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.63f, 0.20f, 1.0f), "橙 = Prefab Override");

    if (targets.empty()) {
        CommitActiveEdit("Property Matrix Cell Edit");
        ImGui::Separator();
        ImGui::TextWrapped("HierarchyでObjectを選択してください。Ctrlクリックで追加・解除、Shiftクリックで範囲選択できます。");
        return;
    }
    if (properties.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("表示条件に一致するプロパティがありません。カテゴリまたは検索文字を変更してください。");
        return;
    }

    DrawBulkEditor(targets, properties);
    ImGui::Separator();
    DrawPropertyTable(targets, properties);
#endif
}

void PropertyMatrixWindow::DrawFilters() {
#ifdef USE_IMGUI
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##PropertyMatrixObjectFilter", "Object名で絞り込み", objectFilter_, sizeof(objectFilter_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##PropertyMatrixPropertyFilter", "プロパティ名 / Pathで絞り込み", propertyFilter_, sizeof(propertyFilter_));

    ImGui::SameLine();
    ImGui::TextDisabled("カテゴリ:");
    ImGui::SameLine();
    ImGui::Checkbox("基本##MatrixIdentity", &showIdentity_);
    ImGui::SameLine();
    ImGui::Checkbox("Transform##MatrixTransform", &showTransform_);
    ImGui::SameLine();
    ImGui::Checkbox("描画##MatrixRendering", &showRendering_);
    ImGui::SameLine();
    ImGui::Checkbox("Editor##MatrixEditor", &showEditor_);
    ImGui::SameLine();
    ImGui::Checkbox("Collision##MatrixCollision", &showCollision_);
    ImGui::SameLine();
    ImGui::Checkbox("Component##MatrixComponent", &showComponent_);
    ImGui::SameLine();
    ImGui::Checkbox("Camera##MatrixCamera", &showCamera_);
    ImGui::SameLine();
    ImGui::Checkbox("Gameplay##MatrixGameplay", &showGameplay_);
#endif
}

void PropertyMatrixWindow::DrawBulkEditor(
    const std::vector<Object3d*>& targets,
    const std::vector<const EditorPropertyDescriptor*>& properties) {
#ifdef USE_IMGUI
    if (bulkPropertyPath_.empty() ||
        std::none_of(properties.begin(), properties.end(), [this](const EditorPropertyDescriptor* property) {
            return property && property->path == bulkPropertyPath_;
        })) {
        bulkPropertyPath_ = properties.front()->path;
        bulkSelectionSignature_.clear();
    }

    const EditorPropertyDescriptor* selectedProperty = nullptr;
    for (const EditorPropertyDescriptor* property : properties) {
        if (property && property->path == bulkPropertyPath_) {
            selectedProperty = property;
            break;
        }
    }
    if (!selectedProperty) {
        return;
    }

    RefreshBulkDraft(targets, selectedProperty, false);

    if (!ImGui::CollapsingHeader(ICON_FA_EDIT " 選択中Objectへ一括適用", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("プロパティ##MatrixBulkProperty", selectedProperty->displayName.c_str())) {
        for (const EditorPropertyDescriptor* property : properties) {
            if (!property) continue;
            const bool selected = property->path == bulkPropertyPath_;
            const std::string label = property->displayName + "##" + property->path;
            if (ImGui::Selectable(label.c_str(), selected)) {
                bulkPropertyPath_ = property->path;
                selectedProperty = property;
                RefreshBulkDraft(targets, selectedProperty, true);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", selectedProperty->path.c_str());

    const bool mixed = EditorPropertyRegistry::GetInstance()->HasMixedValue(targets, selectedProperty->path);
    ImGui::SetNextItemWidth(430.0f);
    DrawValueEditor(*selectedProperty, bulkValue_, "##MatrixBulkValue", false);
    if (mixed) {
        ImGui::SameLine();
        ImGui::TextDisabled("現在は複数の値");
    }

    int editableCount = 0;
    for (Object3d* object : targets) {
        if (CanEditProperty(object, *selectedProperty)) {
            ++editableCount;
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(editableCount == 0);
    if (ImGui::Button((ICON_FA_CHECK " " + std::to_string(editableCount) + "個へ適用").c_str())) {
        const std::vector<DebugEditor::ObjectStateSnapshot> beforeStates = editor_->CaptureObjectStates(targets);
        for (Object3d* object : targets) {
            if (CanEditProperty(object, *selectedProperty)) {
                if (selectedProperty->path == "identity.saveCategory") {
                    // 保存先変更では移動前と移動後の両カテゴリを保存対象にします。
                    editor_->MarkDirtyForObject(object);
                }
                EditorPropertyRegistry::GetInstance()->SetValue(object, selectedProperty->path, bulkValue_);
            }
        }
        editor_->RegisterObjectsEdited(beforeStates, "Property Matrix Bulk: " + selectedProperty->displayName);
        bulkSelectionSignature_ = BuildSelectionSignature(targets) + "|" + selectedProperty->path;
    }
    ImGui::EndDisabled();
    if (editableCount != static_cast<int>(targets.size())) {
        ImGui::SameLine();
        ImGui::TextDisabled("ロック中または読取専用のObjectは除外");
    }
#else
    (void)targets;
    (void)properties;
#endif
}

void PropertyMatrixWindow::DrawPropertyTable(
    const std::vector<Object3d*>& targets,
    const std::vector<const EditorPropertyDescriptor*>& properties) {
#ifdef USE_IMGUI
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("PropertyMatrixTable", static_cast<int>(properties.size()) + 1,
        flags, ImVec2(0.0f, 0.0f))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 210.0f);
    for (const EditorPropertyDescriptor* property : properties) {
        ImGui::TableSetupColumn(property->displayName.c_str(), ImGuiTableColumnFlags_WidthFixed,
            GetPropertyColumnWidth(*property), ImGui::GetID(property->path.c_str()));
    }
    ImGui::TableHeadersRow();

    for (Object3d* object : targets) {
        if (!object) continue;

        std::unordered_set<std::string> overridePaths;
        if (object->IsPrefabInstance()) {
            for (const auto& entry : PresetManager::GetInstance()->GetPrefabOverrides(object)) {
                overridePaths.insert(entry.propertyPath);
            }
        }

        ImGui::PushID(object);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (object->GetIsLocked()) {
            ImGui::TextDisabled(ICON_FA_LOCK " %s", object->GetName().c_str());
        } else {
            ImGui::TextUnformatted(object->GetName().c_str());
        }
        if (object->IsPrefabInstance()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.78f, 1.0f, 1.0f), ICON_FA_CUBES);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Class: %s", object->GetClassName().c_str());
            ImGui::Text("保存先: %s", object->GetSaveCategory().c_str());
            ImGui::EndTooltip();
        }

        for (std::size_t propertyIndex = 0; propertyIndex < properties.size(); ++propertyIndex) {
            const EditorPropertyDescriptor* property = properties[propertyIndex];
            if (!property) continue;

            ImGui::TableSetColumnIndex(static_cast<int>(propertyIndex) + 1);
            ImGui::PushID(property->path.c_str());

            const bool isOverride = overridePaths.find(property->path) != overridePaths.end();
            if (isOverride) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(126, 77, 18, 105));
            }

            if (!EditorPropertyRegistry::GetInstance()->IsApplicable(object, property->path)) {
                ImGui::TextDisabled("—");
                ImGui::PopID();
                continue;
            }

            json value = EditorPropertyRegistry::GetInstance()->GetValue(object, property->path);
            const bool editable = CanEditProperty(object, *property);
            ImGui::BeginDisabled(!editable);
            const bool changed = DrawValueEditor(*property, value, "##Value", true);
            const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
            const bool itemActive = ImGui::IsItemActive();
            ImGui::EndDisabled();

            const std::string editId = object->GetName() + "|" + property->path;
            if (changed && editable) {
                BeginActiveEdit(editId, { object });
                if (property->path == "identity.saveCategory") {
                    editor_->MarkDirtyForObject(object);
                }
                EditorPropertyRegistry::GetInstance()->SetValue(object, property->path, value);
                // 操作中に終了しても未保存警告へ反映されるよう、変更フレームでDirtyにします。
                editor_->MarkDirtyForObject(object);
            }
            if (activeEditId_ == editId &&
                (deactivatedAfterEdit || (!itemActive && !ImGui::IsAnyItemActive()))) {
                CommitActiveEdit("Property Matrix: " + property->displayName);
            }

            if (isOverride && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Prefab Override");
                ImGui::TextDisabled("InspectorのPrefab InstanceからApply / Revertできます。");
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    ImGui::EndTable();
#else
    (void)targets;
    (void)properties;
#endif
}

bool PropertyMatrixWindow::DrawValueEditor(
    const EditorPropertyDescriptor& property,
    json& value,
    const char* id,
    bool compact) const {
    EditorPropertyDrawOptions options;
    options.compact = compact;
    return EditorPropertyDrawer::DrawValue(property, value, id, options);
}

bool PropertyMatrixWindow::IsPropertyVisible(const EditorPropertyDescriptor& property) const {
    if (!HasEditorPropertyFlag(property.flags, EditorPropertyFlags::MultiEdit) ||
        property.path == "identity.name") {
        return false;
    }

    bool categoryVisible = false;
    if (property.category == "Identity") categoryVisible = showIdentity_;
    else if (property.category == "Transform") categoryVisible = showTransform_;
    else if (property.category == "Rendering") categoryVisible = showRendering_;
    else if (property.category == "Editor") categoryVisible = showEditor_;
    else if (property.category == "Collision") categoryVisible = showCollision_;
    else if (property.category == "Component") categoryVisible = showComponent_;
    else if (property.category == "Camera") categoryVisible = showCamera_;
    else if (property.category == "Gameplay") categoryVisible = showGameplay_;
    if (!categoryVisible) {
        return false;
    }

    return ContainsText(property.displayName, propertyFilter_) ||
        ContainsText(property.path, propertyFilter_) ||
        ContainsText(property.category, propertyFilter_);
}

bool PropertyMatrixWindow::CanEditProperty(
    const Object3d* object,
    const EditorPropertyDescriptor& property) const {
    if (!object || HasEditorPropertyFlag(property.flags, EditorPropertyFlags::ReadOnly)) {
        return false;
    }
    if (!EditorPropertyRegistry::GetInstance()->IsApplicable(object, property.path)) {
        return false;
    }
    return !object->GetIsLocked() || property.path == "editor.locked";
}

std::vector<Object3d*> PropertyMatrixWindow::CollectTargets() const {
    std::vector<Object3d*> targets;
    if (!editor_) {
        return targets;
    }
    for (Object3d* object : editor_->GetSelectedObjects()) {
        if (object && ContainsText(object->GetName(), objectFilter_)) {
            targets.push_back(object);
        }
    }
    return targets;
}

std::vector<const EditorPropertyDescriptor*> PropertyMatrixWindow::CollectProperties(
    const std::vector<Object3d*>& targets) const {
    std::vector<const EditorPropertyDescriptor*> properties;
    for (const EditorPropertyDescriptor& property : EditorPropertyRegistry::GetInstance()->GetProperties()) {
        const bool applicable = std::any_of(targets.begin(), targets.end(), [&property](Object3d* object) {
            return EditorPropertyRegistry::GetInstance()->IsApplicable(object, property.path);
        });
        if (applicable && IsPropertyVisible(property)) {
            properties.push_back(&property);
        }
    }
    return properties;
}

void PropertyMatrixWindow::BeginActiveEdit(
    const std::string& editId,
    const std::vector<Object3d*>& targets) {
    if (!editor_ || editId.empty()) {
        return;
    }
    if (activeEditId_ == editId) {
        return;
    }
    CommitActiveEdit("Property Matrix Cell Edit");

    activeEditId_ = editId;
    activeEditSnapshots_.clear();
    activeEditSnapshots_.reserve(targets.size());
    for (Object3d* object : targets) {
        if (object) {
            activeEditSnapshots_.push_back({ object, editor_->CaptureObjectState(object) });
        }
    }
}

void PropertyMatrixWindow::CommitActiveEdit(const std::string& label) {
    if (!editor_ || activeEditSnapshots_.empty()) {
        activeEditId_.clear();
        activeEditSnapshots_.clear();
        return;
    }

    std::vector<DebugEditor::ObjectStateSnapshot> beforeStates;
    beforeStates.reserve(activeEditSnapshots_.size());
    for (const EditSnapshot& snapshot : activeEditSnapshots_) {
        beforeStates.push_back({ snapshot.object, snapshot.state });
    }
    editor_->RegisterObjectsEdited(beforeStates, label);
    activeEditId_.clear();
    activeEditSnapshots_.clear();
}

void PropertyMatrixWindow::RefreshBulkDraft(
    const std::vector<Object3d*>& targets,
    const EditorPropertyDescriptor* property,
    bool force) {
    if (!property || targets.empty()) {
        bulkValue_ = json();
        bulkSelectionSignature_.clear();
        return;
    }

    const std::string signature = BuildSelectionSignature(targets) + "|" + property->path;
    if (!force && signature == bulkSelectionSignature_) {
        return;
    }
    Object3d* sourceObject = nullptr;
    for (Object3d* object : targets) {
        if (EditorPropertyRegistry::GetInstance()->IsApplicable(object, property->path)) {
            sourceObject = object;
            break;
        }
    }
    bulkValue_ = EditorPropertyRegistry::GetInstance()->GetValue(sourceObject, property->path);
    bulkSelectionSignature_ = signature;
}

std::string PropertyMatrixWindow::BuildSelectionSignature(const std::vector<Object3d*>& targets) const {
    std::ostringstream stream;
    for (Object3d* object : targets) {
        stream << reinterpret_cast<std::uintptr_t>(object) << ';';
    }
    return stream.str();
}
