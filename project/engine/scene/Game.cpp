#include "Game.h"
#include "engine/scene/GamePlayScene.h" 
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();

    // ゲームプレイシーンを作成して初期化
    gameScene_ = std::make_unique<GamePlayScene>();
    gameScene_->Initialize();
}

void Game::Update() {
    // 入力とImGuiのフレーム開始は、シーンの更新前に行う
    InputManager::GetInstance()->Update(); 


    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // ゲームプレイシーンの更新処理を呼び出す
    if (gameScene_) {
        gameScene_->Update();
    }
}

void Game::Draw() {
    // 描画前処理
    dxCommon_->PreDraw();

    // ゲームプレイシーンの描画処理を呼び出す
    if (gameScene_) {
        gameScene_->Draw();
    }

    // 描画後処理
    dxCommon_->PostDraw();
}

void Game::Finalize() {
    // ゲームプレイシーンの終了処理
    if (gameScene_) {
        gameScene_->Finalize();
    }

    // Frameworkの終了処理
    Framework::Finalize();
}