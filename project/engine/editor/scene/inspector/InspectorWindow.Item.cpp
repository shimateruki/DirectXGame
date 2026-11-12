#include "engine/editor/scene/inspector/InspectorWindow.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include "imgui.h"

#include <string>

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
        if (ImGui::Button("カテゴリを追加"))
            ImGui::OpenPopup("AddCollisionCategory");
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
            if (isPlaying)
                ImGui::TextDisabled("Playを停止してから追加してください。");
            if (!error.empty())
                ImGui::TextWrapped("%s", error.c_str());
            ImGui::EndPopup();
        }

        ImGui::TreePop();
    }
#endif
}
