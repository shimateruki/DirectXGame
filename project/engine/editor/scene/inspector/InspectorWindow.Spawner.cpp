#include "engine/editor/scene/inspector/InspectorWindow.h"
#include "DebugEditor.h"
#include "Object3D.h"
#include "EditorObjectFactoryRegistry.h"
#include "imgui.h"

void InspectorWindow::DrawSpawnerSettings() {
    // スポナーの種別と発生条件を、選択中オブジェクトのパラメータへ反映します。
#ifdef USE_IMGUI
    engine::Object3D* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject)
        return;

    ImGui::Separator();
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Spawner Config ]");

    if (!selectedObject->GetParameters().has_value()) {
        selectedObject->ResetParameters();
    }

    auto p = selectedObject->GetParameters().value();

    using Registry = EditorObjectFactoryRegistry;
    const auto types = Registry::GetTypes(Registry::Kind::Enemy, Registry::TypeList::Spawner);
    // 選択中IDは候補外でもそのまま表示し、開いただけで保存値を置き換えない。
    std::string preview = p.enemyType.empty() ? "(未設定)" : p.enemyType;
    for (const auto& type : Registry::GetTypes(Registry::Kind::Enemy)) {
        if (type.id == p.enemyType) { preview = type.displayName; break; }
    }
    if (ImGui::BeginCombo("Spawn Type", preview.c_str())) {
        for (const auto* type : types) {
            const bool selected = p.enemyType == type->id;
            ImGui::PushID(type->id.c_str());
            if (ImGui::Selectable(type->displayName.c_str(), selected))
                p.enemyType = type->id;
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::DragFloat("Interval (sec)", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
    ImGui::InputInt("Max Count", &p.maxCount);

    selectedObject->SetParameters(p);
    ImGui::Unindent();
#endif
}

