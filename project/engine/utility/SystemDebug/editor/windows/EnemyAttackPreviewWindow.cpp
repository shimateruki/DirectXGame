#include "EnemyAttackPreviewWindow.h"

#include "EnemyFactory.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void EnemyAttackPreviewWindow::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
}

void EnemyAttackPreviewWindow::Finalize() {
    sceneManager_ = nullptr;
}

void EnemyAttackPreviewWindow::Update(float deltaTime) {
    (void)deltaTime;
}

void EnemyAttackPreviewWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("Enemy Factoryに敵タイプが登録されていません。");
    ImGui::TextWrapped(
        "新しい敵クラスをBaseEnemyから作成し、EnemyFactory::Registerで登録すると、"
        "このPreviewをその敵向けに拡張できます。");
#endif
}

void EnemyAttackPreviewWindow::DrawTimelineWindow() {
}
