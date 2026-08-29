#include "InspectorWindow.h"

#include "DebugEditor.h"
#include "ItemFactory.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawItemTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const auto types = ItemFactory::GetInstance()->GetRegisteredTypes();
    const std::string currentType = selectedObject->GetItemType();
    const char* preview = currentType.empty() ? "(未設定)" : currentType.c_str();
    if (ImGui::BeginCombo("アイテムの種類 (Item Type)", preview)) {
        if (types.empty()) ImGui::TextDisabled("ItemFactoryへの登録はありません");
        for (const std::string& type : types) {
            const bool selected = currentType == type;
            if (ImGui::Selectable(type.c_str(), selected)) {
                selectedObject->SetClassName("Item");
                selectedObject->SetItemType(type);
                selectedObject->SetName("Item_" + type);
                if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                selectedObject->param_->itemType = type;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
#endif
}

void InspectorWindow::DrawAttributeSelector(const char* label, uint32_t* attribute) {
#ifdef USE_IMGUI
    if (ImGui::TreeNode(label)) {
        int flags = static_cast<int>(*attribute);
        ImGui::CheckboxFlags("Player", &flags, 1 << 0);
        ImGui::CheckboxFlags("Enemy", &flags, 1 << 1);
        ImGui::CheckboxFlags("Ground", &flags, 1 << 2);
        ImGui::CheckboxFlags("Bullet", &flags, 1 << 3);
        ImGui::CheckboxFlags("Trigger", &flags, 1 << 4);
        ImGui::CheckboxFlags("PlayerAttack", &flags, 1 << 6);
        ImGui::CheckboxFlags("EnemyAttack", &flags, 1 << 7);
        *attribute = static_cast<uint32_t>(flags);
        ImGui::TreePop();
    }
#endif
}
