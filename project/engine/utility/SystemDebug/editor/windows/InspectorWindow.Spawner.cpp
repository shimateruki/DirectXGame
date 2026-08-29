#include "InspectorWindow.h"

#include "DebugEditor.h"
#include "EnemyFactory.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawSpawnerSettings() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;
    if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
    auto& parameters = selectedObject->param_.value();

    ImGui::Separator();
    ImGui::TextUnformatted("Spawner Config");
    const auto types = EnemyFactory::GetInstance()->GetRegisteredTypes();
    const char* preview = parameters.enemyType.empty() ? "(未設定)" : parameters.enemyType.c_str();
    if (ImGui::BeginCombo("Spawn Type", preview)) {
        if (types.empty()) ImGui::TextDisabled("EnemyFactoryへの登録はありません");
        for (const std::string& type : types) {
            const bool selected = parameters.enemyType == type;
            if (ImGui::Selectable(type.c_str(), selected)) parameters.enemyType = type;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::DragFloat("Interval (sec)", &parameters.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
    ImGui::InputInt("Max Count", &parameters.maxCount);
#endif
}
