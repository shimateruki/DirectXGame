#include "InspectorWindow.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawSpawnerSettings() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    ImGui::Separator();
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Spawner Config ]");

    if (!selectedObject->param_.has_value()) {
        selectedObject->param_.emplace();
    }
    auto& p = selectedObject->param_.value();

    static char typeBuf[64] = "";
    if (typeBuf[0] == '\0') {
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    const char* enemyTypes[] = { "Slime", "Bomb", "Bomber", "Mushroom", "GiantSlime", "PrismSlime", "MagmaSlime", "FireSlime", "ThunderSlime", "WindSlime", "Bat", "BeamDrone" };
    int currentTypeIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (p.enemyType == enemyTypes[i]) currentTypeIndex = i;
    }

    if (ImGui::Combo("Spawn Type", &currentTypeIndex, enemyTypes, IM_ARRAYSIZE(enemyTypes))) {
        p.enemyType = enemyTypes[currentTypeIndex];
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    ImGui::DragFloat("Interval (sec)", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
    ImGui::InputInt("Max Count", &p.maxCount);

    ImGui::Unindent();
#endif
}

