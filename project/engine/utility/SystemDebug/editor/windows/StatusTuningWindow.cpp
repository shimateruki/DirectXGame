#include "StatusTuningWindow.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void StatusTuningWindow::Initialize(DebugEditor* editor, SceneManager* sceneManager) {
    editor_ = editor;
    sceneManager_ = sceneManager;
}

void StatusTuningWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("ステータス拡張は未登録です。");
    ImGui::TextWrapped("ゲームを作り始める際に、必要なステータス定義と編集UIをこのウィンドウへ追加してください。");
#endif
}
