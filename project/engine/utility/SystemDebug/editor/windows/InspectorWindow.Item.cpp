#include "InspectorWindow.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawItemTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* itemTypes[] = { "Heal" };
    const char* itemTypeLabels[] = { "体力回復" };
    std::string currentType = selectedObject->GetItemType();
    if (currentType.empty()) {
        currentType = "Heal";
        selectedObject->SetItemType(currentType);
        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->itemType = currentType;
        selectedObject->param_->healAmount = 1.0f;
        selectedObject->SetName("Item_Heal");
        selectedObject->SetModel("Item/heart.gltf");
        selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
        selectedObject->SetEmissive(1.8f);
        selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
        selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
        selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
        selectedObject->SetStatic(false);

        Object3d::ColliderConfig colConfig;
        colConfig.type = ColliderType::kSphere;
        colConfig.size = { 1.2f, 1.2f, 1.2f };
        selectedObject->SetColliderConfig(colConfig);
        selectedObject->SetCollisionRadius(1.2f);
    }

    int currentIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(itemTypes); i++) {
        if (currentType == itemTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    if (ImGui::Combo("アイテムの種類", &currentIndex, itemTypeLabels, IM_ARRAYSIZE(itemTypeLabels))) {
        std::string selectedItemType = itemTypes[currentIndex];
        selectedObject->SetClassName("Item");
        selectedObject->SetItemType(selectedItemType);

        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->itemType = selectedItemType;

        if (selectedItemType == "Heal") {
            selectedObject->SetName("Item_Heal");
            selectedObject->SetModel("Item/heart.gltf");
            selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            selectedObject->param_->healAmount = 1.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.2f, 1.2f, 1.2f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.2f);
        }
    }

    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成されるアイテムクラスを指定します。");
#endif
}

void InspectorWindow::DrawAttributeSelector(const char* label, uint32_t* attribute) {
#ifdef USE_IMGUI
    if (ImGui::TreeNode(label)) {
        int flags = static_cast<int>(*attribute);
        ImGui::CheckboxFlags("プレイヤー (Player)", &flags, 1 << 0);
        ImGui::CheckboxFlags("敵 (Enemy)", &flags, 1 << 1);
        ImGui::CheckboxFlags("床・地形 (Ground)", &flags, 1 << 2);
        ImGui::CheckboxFlags("弾 (Bullet)", &flags, 1 << 3);
        ImGui::CheckboxFlags("トリガー (Trigger)", &flags, 1 << 4);
        ImGui::CheckboxFlags("プレイヤー攻撃 (PlayerAttack)", &flags, 1 << 6);
        ImGui::CheckboxFlags("敵攻撃 (EnemyAttack)", &flags, 1 << 7);
        *attribute = static_cast<uint32_t>(flags);
        ImGui::TreePop();
    }
#endif
}
