#include "Game.h"
// #include "GamePlayScene.h" // <- SceneManagerが管理
#include "SceneManager.h"    // ★ 追加
#include "ImguiManager.h"
#include "InputManager.h"    // ★ Updateで使うため

void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();

    // ★ SceneManager を作成して初期化
    sceneManager_ = std::make_unique<SceneManager>();
    sceneManager_->Initialize();
}

void Game::Finalize() {
    // ★ SceneManager の終了処理
    if (sceneManager_) {
        sceneManager_->Finalize();
    }


    // ★ 基底クラスの終了処理を呼ぶ
    Framework::Finalize();
}

void Game::Update() {
    // 入力とImGuiのフレーム開始は、シーンの更新前に行う
    InputManager::GetInstance()->Update();
    ImGuiManager::GetInstance()->BeginFrame();


    // ★ SceneManager の更新処理を呼び出す
    if (sceneManager_) {
        sceneManager_->Update();
    }

    // ImGuiフレーム終了
    ImGuiManager::GetInstance()->EndFrame();
}

void Game::Draw() {
    // 描画前処理
    dxCommon_->PreDraw();


    // ★ SceneManager の描画処理を呼び出す
    if (sceneManager_) {
        sceneManager_->Draw();
    }

    // ★ ImGui の描画
    ImGuiManager::GetInstance()->Draw();

    // 描画後処理
    dxCommon_->PostDraw();
}