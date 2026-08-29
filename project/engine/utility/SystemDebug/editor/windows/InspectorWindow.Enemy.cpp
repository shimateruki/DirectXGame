#include "InspectorWindow.h"

#include "DebugEditor.h"
#include "EnemyFactory.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawEnemyTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const auto types = EnemyFactory::GetInstance()->GetRegisteredTypes();
    const std::string currentType = selectedObject->GetEnemyType();
    const char* preview = currentType.empty() ? "(未設定)" : currentType.c_str();
    if (ImGui::BeginCombo("敵の種類 (Enemy Type)", preview)) {
        if (types.empty()) {
            ImGui::TextDisabled("EnemyFactoryへの登録はありません");
        }
        for (const std::string& type : types) {
            const bool selected = currentType == type;
            if (ImGui::Selectable(type.c_str(), selected)) {
                selectedObject->SetClassName("Enemy");
                selectedObject->SetEnemyType(type);
                selectedObject->SetName("Enemy_" + type);
                if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                selectedObject->param_->enemyType = type;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("EnemyFactoryへ登録されたゲーム側の型だけを表示します。");
    }
#endif
}
