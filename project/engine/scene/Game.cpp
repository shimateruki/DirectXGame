#include "Game.h"
#include "engine/scene/GamePlayScene.h" 
#include"engine//io//ImguiManager.h"
void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();

    // ゲームプレイシーンを作成して初期化
    gameScene_ = std::make_unique<GamePlayScene>();
    gameScene_->Initialize();
    // デバッグビルドの場合のみ、デバッグエディタを初期化
#ifdef _DEBUG
    debugEditor_ = std::make_unique<DebugEditor>();
    debugEditor_->Initialize(gameScene_.get()); // gameScene_ のポインタを渡す
#endif
}

void Game::Update() {
    // 入力とImGuiのフレーム開始は、シーンの更新前に行う
    InputManager::GetInstance()->Update(); 

      // ImGuiフレーム開始
        ImGuiManager::GetInstance()->BeginFrame();
#ifdef _DEBUG
        if (debugEditor_) {
            debugEditor_->Update();
        }
#endif
    // ゲームプレイシーンの更新処理を呼び出す
    if (gameScene_) {
        gameScene_->Update();
    }
    // ImGuiフレーム終了
    ImGuiManager::GetInstance()->EndFrame();

	
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
#ifdef _DEBUG
    if (debugEditor_) {
        debugEditor_->Finalize();
        debugEditor_.reset(); // unique_ptrを解放
    }
#endif
    // ゲームプレイシーンの終了処理
    if (gameScene_) {
        gameScene_->Finalize();
    }

    // Frameworkの終了処理
    Framework::Finalize();
}