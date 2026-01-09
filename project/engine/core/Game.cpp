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
    sceneManager_->SetDebugEditor(debugEditor_.get());
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

    // --- フレームレート計算 ---
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> duration = currentTime - lastTime_;
    float deltaTime = duration.count();
    lastTime_ = currentTime;

    // 極端なラグ（ブレークポイントでの停止など）が発生した場合、dtを固定値にして物理崩壊を防ぐ
    if (deltaTime > 0.1f) { deltaTime = 1.0f / 60.0f; }

    // ゲーム内時間の進行速度を適用
    float finalDeltaTime = deltaTime * timeScale_;

    // エディタ操作中フラグ
    bool isSpriteEditorBusy = false;
    bool is3DGizmoBusy = false;

#ifdef USE_IMGUI
    // =================================================================
    //  エディタの更新処理 (Update Logic)
    // =================================================================
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Update();
        isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
    }
    if (debugEditor_) {
        debugEditor_->Update();
        // ImGuizmoを使っているか（ギズモ操作中はカメラを動かさないため）
        is3DGizmoBusy = ImGuizmo::IsUsing();
    }
    if (particleEditor_) {
        particleEditor_->Update();
    }
    if (ghostRecorder_) {
        ghostRecorder_->Update();
    }

    // =================================================================
    //  Master Editor (メイン管理ウィンドウ)
    // =================================================================
    // ImGuiWindowFlags_MenuBar を入れてメニューバーを使えるようにする
    ImGui::Begin("デバッグメニュー (Master Editor)", nullptr, ImGuiWindowFlags_MenuBar);

    // --- ステータス表示 ---
    // FPSによって色を変えると負荷に気づきやすくなります
    float fps = 1.0f / deltaTime;
    ImVec4 fpsColor = (fps >= 55.0f) ? ImVec4(0, 1, 0, 1) : ((fps >= 30.0f) ? ImVec4(1, 1, 0, 1) : ImVec4(1, 0, 0, 1));
    ImGui::TextColored(fpsColor, "FPS: %.1f", fps);

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // マウスポジション
    Vector2 mousePos = InputManager::GetInstance()->GetMousePosition();
    ImGui::Text("Mouse: (%.0f, %.0f)", mousePos.x, mousePos.y);

    // オブジェクト数表示
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene) {
        ImGui::TextDisabled("Objects: %d | Sprites: %d",
            (int)currentScene->GetObjects().size(),
            (int)currentScene->GetSprites().size());
    }

    ImGui::Separator();

    // --- メニューバー (ウィンドウの表示切り替え) ---
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("表示 (View)")) {
            // 各エディタの表示トグル
            ImGui::MenuItem("時間操作 (Time)", NULL, &showTimeController_);
            ImGui::MenuItem("ライト設定 (Light)", NULL, &showLightEditor_);
            ImGui::Separator();
            ImGui::MenuItem("3Dエディタ", NULL, &showDebugWindows_);
            ImGui::MenuItem("スプライト", NULL, &showSpriteInspector_);
            ImGui::MenuItem("パーティクル", NULL, &showParticleEditor_);
            ImGui::MenuItem("アニメ録画 (Ghost)", NULL, &showGhostRecorder_);
            ImGui::MenuItem("カメラ設定", NULL, &showCameraEditor);
            ImGui::Separator();
            ImGui::MenuItem("デバッグログ", NULL, &showDebugConsole_);

            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // 

    // --- 各サブウィンドウの描画 ---
    if (showDebugWindows_) {
        if (ImGui::CollapsingHeader("3Dオブジェクト (Object Editor)")) {
            debugEditor_->DrawImGui();
        }
    }

    if (showSpriteInspector_) {
        if (ImGui::CollapsingHeader("スプライト (Sprite Inspector)")) {
            spriteDebugEditor_->DrawImGui();
        }
    }

    if (showParticleEditor_) {
        if (ImGui::CollapsingHeader("パーティクル (Particle Editor)")) {
            particleEditor_->DrawImGui();
        }
    }

    if (showGhostRecorder_) {
        if (ImGui::CollapsingHeader("録画 (Ghost Recorder)")) {
            if (ghostRecorder_) {
                ghostRecorder_->DrawImGui();
            }
        }
    }

    if (showLightEditor_ && lightEditor_) {
        if (ImGui::CollapsingHeader("ライト環境 (Lighting)")) {
            lightEditor_->DrawImGui();
        }
    }

    if (showCameraEditor) {
        if (ImGui::CollapsingHeader("カメラ (Camera)")) {
            CameraEditor::GetInstance()->DrawImGui();
        }
    }

    if (showDebugConsole_) {
        if (ImGui::CollapsingHeader("ログ (Console)")) {
            DebugConsole::GetInstance()->DrawImGui();
        }
    }

    // --- 時間操作パネル  ---
    if (showTimeController_) {
        ImGui::Separator();
        ImGui::Text("時間制御 (Time Scale)");

        // スライダー
        ImGui::SliderFloat("##TimeScale", &timeScale_, 0.0f, 2.0f, "速度: %.2fx");

        // プリセットボタン
        if (ImGui::Button("一時停止 (0.0)")) timeScale_ = 0.0f;
        ImGui::SameLine();
        if (ImGui::Button("スロー (0.1)"))   timeScale_ = 0.1f;
        ImGui::SameLine();
        if (ImGui::Button("標準 (1.0)"))     timeScale_ = 1.0f;
        ImGui::SameLine();
        if (ImGui::Button("高速 (2.0)"))     timeScale_ = 2.0f;
    }

    ImGui::End(); // Master Editor End

#endif

    // =================================================================
    //  交通整理 (Input Blocking)
    // =================================================================
    // エディタ（Gizmoやスプライト調整）を操作している間は、
    // ゲーム内カメラが勝手に回らないように入力を遮断する
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        bool isEditorBusy = isSpriteEditorBusy || is3DGizmoBusy;
        // エディタが忙しくない(true)なら、カメラ入力を有効(true)にする
        camera->SetInputEnabled(!isEditorBusy);
    }

    // =================================================================
    //  シーン更新
    // =================================================================
    if (sceneManager_) {
        // 時間操作の影響を受けた delta time を渡す
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
#endif

    if (sceneManager_) {
        sceneManager_->Draw();
    }

#ifdef USE_IMGUI
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Draw();
    }
    //ImGui の描画
  // 1. ここで ImGui::Render() が呼ばれる
    ImGuiManager::GetInstance()->Draw();

    // 2. Render() が終わった直後の「ここ」で EndFrame を呼ぶ！
    ImGuiManager::GetInstance()->EndFrame();
#endif

    // 描画後処理
    dxCommon_->PostDraw();
}