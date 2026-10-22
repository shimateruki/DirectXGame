#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "SceneManager.h"
#ifdef USE_IMGUI
#include "imgui.h"
#include <algorithm>
#endif

void InspectorWindow::DrawGimmickTypeSelector() {
#ifdef USE_IMGUI
    auto* object = editor_->GetSelectedObject();
    if (!object) return;
    using Registry = EditorObjectFactoryRegistry;
    const auto& types = Registry::GetTypes(Registry::Kind::Gimmick);
    const auto current = std::find_if(types.begin(), types.end(), [&](const auto& type) {
        return type.id == object->GetGimmickType();
    });
    const std::string preview = current != types.end() ? current->displayName : object->GetGimmickType();
    ImGui::BeginDisabled(editor_->GetSceneManager()->IsPlaying());
    if (current != types.end() && current->matches && !current->matches(*object)) {
        ImGui::TextDisabled("種類名と実クラスが一致していません。");
        if (ImGui::Button("保存値を保って実クラスを更新"))
            editor_->RequestObjectTypeChange(object, Registry::Kind::Gimmick, current->id);
    }
    if (ImGui::BeginCombo("ギミックの種類", preview.c_str())) {
        for (const auto& type : types) {
            if (ImGui::Selectable(type.displayName.c_str(), type.id == object->GetGimmickType()))
                // 描画中の借用参照を保ち、次のEditor更新で実クラスを交換します。
                editor_->RequestObjectTypeChange(object, Registry::Kind::Gimmick, type.id);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
#endif
}
