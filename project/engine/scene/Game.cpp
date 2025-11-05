#include "Game.h"
#include "SceneManager.h" 
#include "ImguiManager.h"
#include "InputManager.h"
#include"DebugConsole.h"
#include "SceneFactory.h"
#include"imgui.h"
#include "ImGuizmo.h" 
#include <chrono>

void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();
    sceneFactory_ = std::make_unique<SceneFactory>();
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
    particleEditor_ = std::make_unique<ParticleEditor>();
    particleEditor_->Initialize(sceneManager_.get());

    DebugConsole::GetInstance()->Initialize();

#endif
}

void Game::Finalize() {
    // ★ SceneManager の終了処理
    if (sceneManager_) {
        sceneManager_->Finalize();
    }
#ifdef _DEBUG
    particleEditor_.reset(); 
    spriteDebugEditor_.reset();
    debugEditor_.reset();
   DebugConsole::GetInstance()->Finalize();
#endif

    // ★ 基底クラスの終了処理を呼ぶ
    Framework::Finalize();
}
void Game::Update() {
    InputManager::GetInstance()->Update();

    ImGuiManager::GetInstance()->BeginFrame();
    ImGuizmo::BeginFrame();

    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> duration = currentTime - lastTime_;
    float deltaTime = duration.count();
    lastTime_ = currentTime;
    if (deltaTime > 0.1f) { deltaTime = 1.0f / 60.0f; }

    bool isSpriteEditorBusy = false;
    bool is3DGizmoBusy = false;

#ifdef _DEBUG
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

    // --- ImGui描画 (Master Editor) ---
    ImGui::Begin("Master Editor", nullptr, ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("3D Editor", NULL, &showDebugWindows_);
            ImGui::MenuItem("Sprite Inspector", NULL, &showSpriteInspector_);
            ImGui::MenuItem("Particle Editor", NULL, &showParticleEditor_);
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
    if (showDebugConsole_) {
        if (ImGui::CollapsingHeader("Debug Console")) {
            DebugConsole::GetInstance()->DrawImGui();
        }
    }
    ImGui::End(); // "Master Editor"

#endif

    // --- 交通整理 ---
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        bool isEditorBusy = isSpriteEditorBusy || is3DGizmoBusy;
        camera->SetInputEnabled(!isEditorBusy);
    }

    // --- シーンの更新 ---
    if (sceneManager_) {
        sceneManager_->Update(deltaTime);
    }

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