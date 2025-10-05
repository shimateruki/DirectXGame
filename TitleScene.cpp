#include "TitleScene.h"
#include "Framework.h" // InputManagerを取得するためにインクルード
#include "externals/imgui/imgui.h"
#include "Game.h"
// GameクラスからInputManagerのポインタを受け取るための前方宣言


void TitleScene::Initialize() {
    // GameクラスからInputManagerのポインタを取得
    // ※注意：この方法は一時的なもので、より良い設計は依存性の注入(DI)コンテナなどを使うことです
    if (auto* game = dynamic_cast<Game*>(Game::GetInstance())) {
        inputManager_ = game->GetInputManager();
    }
}

void TitleScene::Finalize() {
    // このシーンで確保したメモリがあれば解放する
}

void TitleScene::Update() {
    // スペースキーが押されたらゲームプレイシーンへ
    if (inputManager_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
        RequestNextScene("GAMEPLAY");
    }
}

void TitleScene::Draw() {
    // ImGuiを使ったシンプルなタイトル表示
    ImGui::Begin("Title");
    ImGui::Text("This is Title Scene.");
    ImGui::Text("Press SPACE to Start!");
    ImGui::End();
}