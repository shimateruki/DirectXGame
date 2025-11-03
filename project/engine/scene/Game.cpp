#include "Game.h"
#include "SceneManager.h" 
#include "ImguiManager.h"
#include "InputManager.h"   
#include <chrono>

void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();

    // ★ SceneManager を作成して初期化
    sceneManager_ = std::make_unique<SceneManager>();
    sceneManager_->Initialize();
    //  lastTime_ を「起動時」の時間で初期化
    lastTime_ = std::chrono::high_resolution_clock::now();
#ifdef _DEBUG
    spriteDebugEditor_ = std::make_unique<SpriteDebugEditor>();
    spriteDebugEditor_->Initialize(sceneManager_.get(), InputManager::GetInstance());

    debugEditor_ = std::make_unique<DebugEditor>();
    debugEditor_->Initialize(sceneManager_.get(), dxCommon_);
#endif
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
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> duration = currentTime - lastTime_; // メンバ変数の lastTime_ を使う
    float deltaTime = duration.count();
    lastTime_ = currentTime; // メンバ変数の lastTime_ を更新する

    bool isSpriteEditorBusy = false; 

 
#ifdef _DEBUG
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Update();
        isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy(); // ★ 交通整理のため状態取得
    }
    if (debugEditor_) {
        debugEditor_->Update();
    }
#endif

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        // ギズモがビジー(true)なら、カメラ入力は無効(false)にする
        camera->SetInputEnabled(!isSpriteEditorBusy);
    }
    // ★ SceneManager の更新処理を呼び出す
    if (sceneManager_) {
        sceneManager_->Update(deltaTime);
    }

    // ImGuiフレーム終了
    ImGuiManager::GetInstance()->EndFrame();
}

void Game::Draw() {
    // 描画前処理
    dxCommon_->PreDraw();

#ifdef _DEBUG
    if (debugEditor_) {
        debugEditor_->DrawDebug(dxCommon_->GetCommandList());
    }
#endif

    // ★ SceneManager の描画処理を呼び出す
    if (sceneManager_) {
        sceneManager_->Draw();
    }

#ifdef _DEBUG
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Draw();
    }
#endif


    // ★ ImGui の描画
    ImGuiManager::GetInstance()->Draw();

    // 描画後処理
    dxCommon_->PostDraw();
}