#include "Game.h"
#include "SceneManager.h" 
#include "ImguiManager.h"
#include "InputManager.h"
#include"DebugConsole.h"
#include "SceneFactory.h"
#include "LightManager.h"
#include"imgui.h"
#include "ImGuizmo.h" 
#include <chrono>
#include <ParticleManager.h>

void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();
    sceneFactory_ = std::make_unique<SceneFactory>();
    //  SceneManager を作成して初期化
    sceneManager_ = std::make_unique<SceneManager>();
    // =========================================================
    //  ローカル設定を見て開始シーンを決める
    // =========================================================
    std::string startScene = "TITLE"; // デフォルト

#ifdef USE_IMGUI
    std::string lastScene = sceneManager_->LoadLastSceneName();
    if (!lastScene.empty()) {
        startScene = lastScene;
    }
#endif
    currentSceneName_ = startScene;
    // 初期化 
    sceneManager_->Initialize(sceneFactory_.get(), startScene);
    //  lastTime_ を「起動時」の時間で初期化
    lastTime_ = std::chrono::high_resolution_clock::now();


    //  lastTime_ を「起動時」の時間で初期化
    lastTime_ = std::chrono::high_resolution_clock::now();
#ifdef USE_IMGUI
    spriteDebugEditor_ = std::make_unique<SpriteDebugEditor>();
    spriteDebugEditor_->Initialize(sceneManager_.get(), InputManager::GetInstance());
    ghostRecorder_ = std::make_unique<GhostRecorder>();
    ghostRecorder_->Initialize(sceneManager_.get());
    debugEditor_ = std::make_unique<DebugEditor>();
    debugEditor_->Initialize(sceneManager_.get(), dxCommon_);
    sceneManager_->SetDebugEditor(debugEditor_.get());
    particleEditor_ = std::make_unique<ParticleEditor>();
    particleEditor_->Initialize(sceneManager_.get());
    LightEditor::GetInstance()->Initialize();
    DebugConsole::GetInstance()->Initialize();
    if (auto currentScene = sceneManager_->GetCurrentScene()) {
        currentScene->SetDebugEditor(debugEditor_.get());
    }

#endif
#ifdef  USE_IMGUI
    isPlaying_ = false; // デバッグ時は停止状態（エディタ操作）から
#else
    isPlaying_ = true;  // リリース時は最初から再生
#endif
    CameraEditor::GetInstance()->Initialize();

}

void Game::Finalize() {
    // ★ SceneManager の終了処理
    if (sceneManager_) {
        sceneManager_->Finalize();
    }
#ifdef USE_IMGUI
    particleEditor_.reset(); 
    spriteDebugEditor_.reset();
    debugEditor_.reset();
    ghostRecorder_.reset();
   DebugConsole::GetInstance()->Finalize();
#endif

    // ★ 基底クラスの終了処理を呼ぶ
    Framework::Finalize();
}

void Game::Update() {
    InputManager::GetInstance()->Update();

#ifdef USE_IMGUI
    ImGuiManager::GetInstance()->BeginFrame();
    ImGuizmo::BeginFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // --- メインメニューバー ---
    if (ImGui::BeginMainMenuBar()) {
        ImGui::Separator();

        // 再生・停止の状態変化をチェック
        static bool prevIsPlaying = isPlaying_;
        if (isPlaying_) {
            if (ImGui::Button("■ 停止")) isPlaying_ = false;
        } else {
            if (ImGui::Button("▶ 再生")) isPlaying_ = true;
        }

// 停止した瞬間に「最後にロード/選択したシーン」でリロードする
        if (prevIsPlaying != isPlaying_ && !isPlaying_) {
            sceneManager_->ChangeScene(currentSceneName_); // ここで currentSceneName_ を使用
            CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
            prevIsPlaying = isPlaying_;
        } else {
            prevIsPlaying = isPlaying_;
        }

        ImGui::Text(isPlaying_ ? " | 実行中" : " | 編集モード");

        if (ImGui::BeginMenu("表示")) {
            ImGui::MenuItem("3Dオブジェクト / ヒエラルキー", NULL, &showDebugWindows_);
            ImGui::MenuItem("スプライトインスペクター", NULL, &showSpriteInspector_);
            ImGui::MenuItem("パーティクルエディタ", NULL, &showParticleEditor_);
            ImGui::MenuItem("録画 (Ghost Recorder)", NULL, &showGhostRecorder_);
            ImGui::MenuItem("カメラ設定", NULL, &showCameraEditor);
            ImGui::Separator();
            ImGui::MenuItem("ライティング", NULL, &showLightEditor_);
            ImGui::MenuItem("デバッグログ", NULL, &showDebugConsole_);
            ImGui::MenuItem("時間操作", NULL, &showTimeController_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("シーン切り替え")) {
            const char* sceneNames[] = { "TITLE", "GAMEPLAY", "GAMEOVER", "GAMECLEAR" };
            for (int i = 0; i < _countof(sceneNames); i++) {
                if (ImGui::MenuItem(sceneNames[i])) sceneManager_->ChangeScene(sceneNames[i]);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
#endif

    // --- フレームレート計算 ---
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> duration = currentTime - lastTime_;
    float deltaTime = duration.count();
    lastTime_ = currentTime;

    if (deltaTime > 0.1f) { deltaTime = 1.0f / 60.0f; }

    // 停止中なら時間は進めない
    float finalDeltaTime = isPlaying_ ? (deltaTime * timeScale_) : 0.0f;

    bool isSpriteEditorBusy = false;
    bool is3DGizmoBusy = false;

#ifdef USE_IMGUI
    if (showDebugWindows_) {
        ImGui::Begin("ヒエラルキー", &showDebugWindows_);
        debugEditor_->Update();
        debugEditor_->DrawImGui();
        is3DGizmoBusy = ImGuizmo::IsUsing();
        ImGui::End();
    }
    if (showSpriteInspector_) {
        ImGui::Begin("スプライト", &showSpriteInspector_);
        spriteDebugEditor_->Update();
        spriteDebugEditor_->DrawImGui();
        isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
        ImGui::End();
    }
    if (showParticleEditor_) {
        ImGui::Begin("パーティクル", &showParticleEditor_);
        particleEditor_->Update();
        particleEditor_->DrawImGui();
        ImGui::End();
    }
    if (showGhostRecorder_) {
        ImGui::Begin("録画", &showGhostRecorder_);
        ghostRecorder_->Update();
        ghostRecorder_->DrawImGui();
        ImGui::End();
    }
    if (showLightEditor_) {
        ImGui::Begin("ライト", &showLightEditor_);
        LightEditor::GetInstance()->DrawImGui();
        ImGui::End();
    }
    if (showCameraEditor) {
        ImGui::Begin("カメラ", &showCameraEditor);
        CameraEditor::GetInstance()->DrawImGui();
        ImGui::End();
    }
    if (showDebugConsole_) DebugConsole::GetInstance()->DrawImGui();

    if (showTimeController_) {
        ImGui::Begin("ステータス", &showTimeController_);
        float fps = 1.0f / deltaTime;
        ImVec4 fpsColor = (fps >= 55.0f) ? ImVec4(0, 1, 0, 1) : ((fps >= 30.0f) ? ImVec4(1, 1, 0, 1) : ImVec4(1, 0, 0, 1));
        ImGui::TextColored(fpsColor, "FPS: %.1f", fps);
        ImGui::Separator();
        ImGui::SliderFloat("時間倍率", &timeScale_, 0.0f, 2.0f, "速度: %.2fx");
        if (ImGui::Button("一時停止")) timeScale_ = 0.0f; ImGui::SameLine();
        if (ImGui::Button("標準")) timeScale_ = 1.0f;
        ImGui::End();
    }
#endif

    // 入力遮断管理
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetInputEnabled(!(isSpriteEditorBusy || is3DGizmoBusy));
    }

    // シーン・マネージャ更新
    if (sceneManager_) {
        sceneManager_->Update(finalDeltaTime);
    }
    LightManager::GetInstance()->Update();
}


void Game::Draw() {
    // 描画前処理
    dxCommon_->PreDraw();

#ifdef USE_IMGUI
    if (debugEditor_) {
        debugEditor_->DrawDebug(dxCommon_->GetCommandList());
    }
    if (ghostRecorder_) {
        // カメラを取得して ViewProjection行列 を計算
        Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        if (camera) {
            Matrix4x4 view = camera->GetViewMatrix();
            Matrix4x4 proj = camera->GetProjectionMatrix();
            Matrix4x4 viewProj = Math::Multiply(view, proj); 

            // プレビュー描画実行
            ghostRecorder_->DrawPreview(viewProj);
        }
    }
#endif

    if (sceneManager_) {
        sceneManager_->Draw();
    }

#ifdef USE_IMGUI
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Draw();
    }

    // 1. ImGuiの描画コマンドを積む
    ImGuiManager::GetInstance()->Draw();
#endif


    dxCommon_->PostDraw();

#ifdef USE_IMGUI
 
    ImGuiManager::GetInstance()->EndFrame();
#endif

    dxCommon_->UpdateFixFPS();

}