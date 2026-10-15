#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.h"
#include "DebugEditor.h"
#include "game/actor/item/DroppedCopyStar.h"
#include "Object3d.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include "imgui.h"

// 一時ドロップの星は実体の寿命だけを調整し、通常アイテムは初回選択時に必要な外観と判定を補います。
void InspectorWindow::DrawItemTypeSelector() {
#ifdef USE_IMGUI
    // 一時ドロップ星は通常アイテムと設定項目が異なるため先に分岐する。
    engine::Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    if (auto* star = dynamic_cast<DroppedCopyStar*>(selectedObject)) {
        ImGui::TextUnformatted("コピー解除で落とした星（一度だけ取得）");
        float lifetime = star->GetLifetime();
        float blink = star->GetBlinkDuration();
        if (ImGui::SliderFloat("消滅までの秒数", &lifetime, 1.0f, 60.0f, "%.1f 秒"))
            star->SetLifetime(lifetime);
        if (ImGui::SliderFloat("点滅を始める残り秒数", &blink, 0.0f, lifetime, "%.1f 秒"))
            star->SetBlinkDuration(blink);
        ImGui::Text("経過: %.1f 秒", star->GetElapsed());
        ImGui::TextUnformatted("この調整は選択中の星だけに適用されます。");
        return;
    }

    const char* itemTypes[] = { "Heal" };
    const char* itemTypeLabels[] = { "体力回復" };
    std::string currentType = selectedObject->GetItemType();
    if (currentType.empty()) {
        // 旧データで種類が空なら、回復アイテムとしてモデルと判定を補完する。
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
        selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
        selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
        selectedObject->SetStatic(false);

        engine::Object3d::ColliderConfig colConfig;
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
        // 種類だけでなく見た目とColliderも同時に更新し、編集内容との不一致を防ぐ。
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
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
            selectedObject->SetStatic(false);

            selectedObject->param_->healAmount = 1.0f;

            engine::Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.2f, 1.2f, 1.2f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.2f);
        }
    }

    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成されるアイテムクラスを指定します。");
#endif
}

// JSONのカテゴリ一覧で既知ビットを編集し、未登録ビットは変更せず保持します。
void InspectorWindow::DrawAttributeSelector(const char* label, uint32_t* attribute) {
#ifdef USE_IMGUI
    // 定義済みビットだけをUIへ並べ、既存の属性値をCheckboxFlagsで編集する。
    if (ImGui::TreeNode(label)) {
        if (!CollisionCategories::IsDefinitionLoaded()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                "カテゴリ定義JSONを読めません。既存カテゴリで一時的に動作しています。");
        }
        int flags = static_cast<int>(*attribute);
        for (const auto& category : CollisionCategories::GetDefinitions()) {
            const std::string itemLabel = category.displayName + " (" + category.id + ")##" + category.id;
            ImGui::CheckboxFlags(itemLabel.c_str(), &flags, static_cast<int>(category.bit));
        }
        *attribute = static_cast<uint32_t>(flags);
        if (ImGui::Button("カテゴリを追加")) ImGui::OpenPopup("AddCollisionCategory");
        if (ImGui::BeginPopup("AddCollisionCategory")) {
            static char idBuffer[64]{};
            static char nameBuffer[128]{};
            static std::string error;
            ImGui::InputText("ID (英小文字)", idBuffer, sizeof(idBuffer));
            ImGui::InputText("表示名", nameBuffer, sizeof(nameBuffer));
            const bool isPlaying = SceneManager::GetInstance() && SceneManager::GetInstance()->IsPlaying();
            // Play中はJSONを書き換えず、停止後に新しいカテゴリを登録させる。
            ImGui::BeginDisabled(isPlaying);
            if (ImGui::Button("追加してJSONへ保存")) {
                if (CollisionCategories::Add(idBuffer, nameBuffer, &error)) {
                    idBuffer[0] = nameBuffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            if (isPlaying) ImGui::TextDisabled("Playを停止してから追加してください。");
            if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
            ImGui::EndPopup();
        }
        ImGui::TreePop();
    }
#endif
}
