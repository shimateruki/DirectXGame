#include "InspectorWindow.h"

#include "DebugEditor.h"
#include "GimmickFactory.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawGimmickTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const auto types = GimmickFactory::GetInstance()->GetRegisteredTypes();
    const std::string currentType = selectedObject->GetGimmickType();
    const char* preview = currentType.empty() ? "(未設定)" : currentType.c_str();
    if (ImGui::BeginCombo("ギミックの種類 (Gimmick Type)", preview)) {
        if (types.empty()) {
            ImGui::TextDisabled("GimmickFactoryへの登録はありません");
        }
        for (const std::string& type : types) {
            const bool selected = currentType == type;
            if (ImGui::Selectable(type.c_str(), selected)) {
                selectedObject->SetClassName("Gimmick");
                selectedObject->SetGimmickType(type);
                selectedObject->SetName("Gimmick_" + type);
                if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                selectedObject->param_->gimmickType = type;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("GimmickFactoryへ登録されたゲーム側の型だけを表示します。");
    }
#endif
}
