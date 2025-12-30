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

void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();
    sceneFactory_ = std::make_unique<SceneFactory>();
    // ★ SceneManager を作成して初期化
    sceneManager_ = std::make_unique<SceneManager>();
    sceneManager_->Initialize(sceneFactory_.get(), "TITLE");

    //  lastTime_ を「起動時」の時間で初期化
    lastTime_ = std::chrono::high_resolution_clock::now();
#ifdef USE_IMGUI
    spriteDebugEditor_ = std::make_unique<SpriteDebugEditor>();
    spriteDebugEditor_->Initialize(sceneManager_.get(), InputManager::GetInstance());
    ghostRecorder_ = std::make_unique<GhostRecorder>();
    ghostRecorder_->Initialize(sceneManager_.get());
    debugEditor_ = std::make_unique<DebugEditor>();
    debugEditor_->Initialize(sceneManager_.get(), dxCommon_);
    particleEditor_ = std::make_unique<ParticleEditor>();
    particleEditor_->Initialize(sceneManager_.get());
    lightEditor_ = std::make_unique<LightEditor>();
    lightEditor_->Initialize();
    DebugConsole::GetInstance()->Initialize();

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
#endif

    //フレームレート計算
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> duration = currentTime - lastTime_;
    float deltaTime = duration.count();
    lastTime_ = currentTime;
    if (deltaTime > 0.1f) { deltaTime = 1.0f / 60.0f; }
    float finalDeltaTime = deltaTime * timeScale_;
    bool isSpriteEditorBusy = false;
    bool is3DGizmoBusy = false;

#ifdef USE_IMGUI
    // --- ロジックの更新 ---
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Update();
        isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
    }
    if (debugEditor_) {
        debugEditor_->Update();
        is3DGizmoBusy = ImGuizmo::IsUsing();
    }
    if (particleEditor_) {
        particleEditor_->Update();
    }
    if (ghostRecorder_) {
        ghostRecorder_->Update();
    }

    // --- ImGui描画 (Master Editor) ---
    ImGui::Begin("Master Editor", nullptr, ImGuiWindowFlags_MenuBar);

    // fpsの可視化
    float fps = 1.0f / deltaTime;
    ImGui::Text("FPS: %.1f", fps);
    // マウスポジションの可視化
    Vector2 mousePos = InputManager::GetInstance()->GetMousePosition();
    ImGui::Text("Mouse: (%.0f, %.0f)", mousePos.x, mousePos.y);

    // object sprite数の可視化
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene) {
        ImGui::Text("Objects: %d", (int)currentScene->GetObjects().size());
        ImGui::Text("Sprites: %d", (int)currentScene->GetSprites().size());
    }

    ImGui::Separator(); // 区切り線

    // ★ 2. メニューバーに項目を追加
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Time Controller", NULL, &showTimeController_);
            ImGui::MenuItem("Light Editor", NULL, &showLightEditor_);      
            ImGui::Separator();
            ImGui::MenuItem("3D Editor", NULL, &showDebugWindows_);
            ImGui::MenuItem("Sprite Inspector", NULL, &showSpriteInspector_);
            ImGui::MenuItem("Particle Editor", NULL, &showParticleEditor_);
            ImGui::MenuItem("Ghost Recorder", NULL, &showGhostRecorder_);
            ImGui::MenuItem("Debug Console", NULL, &showDebugConsole_);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (showDebugWindows_) {
        if (ImGui::CollapsingHeader("3D Object Editor")) {
            debugEditor_->DrawImGui();
        }
    }

    if (showSpriteInspector_) {
        if (ImGui::CollapsingHeader("Sprite Inspector")) {
            spriteDebugEditor_->DrawImGui();
        }
    }
    if (showParticleEditor_) {
        if (ImGui::CollapsingHeader("Particle Editor")) {
            particleEditor_->DrawImGui();
        }
    }
    if (showGhostRecorder_) {
        if (ImGui::CollapsingHeader("Ghost Recorder")) {
            if (ghostRecorder_) {
                ghostRecorder_->DrawImGui();
            }
        }
    }
    if (showDebugConsole_) {
        if (ImGui::CollapsingHeader("Debug Console")) {
            DebugConsole::GetInstance()->DrawImGui();
        }
    }
    if (showLightEditor_ && lightEditor_) {
        if (ImGui::CollapsingHeader("Light")) {
            lightEditor_->DrawImGui();
        }
    }
    if (showTimeController_) {
        if (ImGui::CollapsingHeader("Deltatimer")) {
            // timeScale_ を 0.0f ～ 2.0f の範囲で操作
            ImGui::SliderFloat("Time Scale", &timeScale_, 0.0f, 2.0f);
            if (ImGui::Button("Reset (1.0x)")) { timeScale_ = 1.0f; }
            ImGui::SameLine();
            if (ImGui::Button("Slow (0.2x)")) { timeScale_ = 0.2f; }
        }
    }
    CameraEditor::GetInstance()->DrawImGui();
   
    ImGui::End();
 

#endif

    // --- 交通整理 ---
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        bool isEditorBusy = isSpriteEditorBusy || is3DGizmoBusy;
        camera->SetInputEnabled(!isEditorBusy);
    }

    // --- シーンの更新 ---
    if (sceneManager_) {
        float scaledDeltaTime = deltaTime * timeScale_;
        sceneManager_->Update(scaledDeltaTime);
    }

    LightManager::GetInstance()->Update();
#ifdef USE_IMGUI
    ImGuiManager::GetInstance()->EndFrame();
#endif
}


void Game::Draw() {
    // 描画前処理
    dxCommon_->PreDraw();

#ifdef USE_IMGUI
    if (debugEditor_) {
        debugEditor_->DrawDebug(dxCommon_->GetCommandList());
    }
#endif

    //SceneManager の描画処理を呼び出す
    if (sceneManager_) {
        sceneManager_->Draw();
    }

#ifdef USE_IMGUI
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Draw();
    }
    //ImGui の描画
    ImGuiManager::GetInstance()->Draw();
#endif
    // 描画後処理
    dxCommon_->PostDraw();
}