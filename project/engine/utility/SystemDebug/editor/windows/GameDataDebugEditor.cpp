#include "GameDataDebugEditor.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void GameDataDebugEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
}

void GameDataDebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("ゲーム進行データは未登録です。");
    ImGui::TextWrapped("セーブスロットやステージ進行が必要になった時点で、ゲーム側のデータ管理をこのウィンドウへ接続してください。");
#endif
}
