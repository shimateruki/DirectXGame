#include "EditorManager.h"
#include <imgui.h>

void EditorManager::DrawInspector() {
#ifdef USE_IMGUI


    // 右パネルの "Inspector" という1つの箱だけを描画する
    ImGui::Begin("Inspector");

    if (selectedObject_ != nullptr) {
        // 何か選ばれている場合：名前を表示して線を引く
        ImGui::Text("Selected: %s", selectedObject_->GetName().c_str());
        ImGui::Separator();
        ImGui::Spacing();
        // ポストエフェクトならポストエフェクトのUIがここに描画される！
        selectedObject_->DrawImGui();

    } else {
        // 何も選ばれていない場合
        ImGui::Text("オブジェクトが選択されていません");
    }

    ImGui::End();
#endif // USE_IMGUI

}