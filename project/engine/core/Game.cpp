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
#include <SrvManager.h>

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
    dxCommon_->CreateRenderTexture();

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

    // -------------------------------------------------------------------------
    // 1. Unity風の初期レイアウト（ドッキング）自動構築
    // -------------------------------------------------------------------------
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    static bool first_time = true;
    if (first_time) {
        first_time = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.22f, nullptr, &dock_main_id);
        ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
        ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.30f, nullptr, &dock_main_id);

        ImGui::DockBuilderDockWindow("Hierarchy", dock_left_id);
        ImGui::DockBuilderDockWindow("Inspector", dock_right_id);
        ImGui::DockBuilderDockWindow("Project (Assets)", dock_bottom_id);
        ImGui::DockBuilderDockWindow("録画", dock_bottom_id);
        ImGui::DockBuilderDockWindow("デバッグログ", dock_bottom_id);
        ImGui::DockBuilderDockWindow("Game View", dock_main_id);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    // 更新が必要なフラグ
    bool isSpriteEditorBusy = false;
    bool is3DGizmoBusy = false;

    // -------------------------------------------------------------------------
    // 2. Game View ウィンドウ (座標計算と各エディタへの通知)
    // -------------------------------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Game View");
    {
        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        const float targetAspect = 16.0f / 9.0f;
        ImVec2 displaySize;
        float containerAspect = windowSize.x / windowSize.y;

        if (containerAspect > targetAspect) {
            displaySize.y = windowSize.y;
            displaySize.x = displaySize.y * targetAspect;
        } else {
            displaySize.x = windowSize.x;
            displaySize.y = displaySize.x / targetAspect;
        }

        // 配置位置（上詰め・中央）
        ImVec2 offset = { (windowSize.x - displaySize.x) * 0.5f, 0.0f };
        ImGui::SetCursorPos(offset);

        // ゲーム画面のスクリーン座標（左上）を取得
        ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();

        if (displaySize.x > 0 && displaySize.y > 0) {
            // テクスチャ表示
            uint32_t texHandle = dxCommon_->GetRenderTextureSrvHandle();
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(texHandle);
            ImGui::Image((ImTextureID)gpuHandle.ptr, displaySize);

            bool isHovered = ImGui::IsItemHovered();
            ImVec2 mPos = ImGui::GetIO().MousePos;

            // --- A. 3Dデバッグエディタの更新 ---
            if (debugEditor_) {
                debugEditor_->SetGameViewRegion({ imageScreenPos.x, imageScreenPos.y }, { displaySize.x, displaySize.y });
                debugEditor_->SetGameViewMousePos({ mPos.x - imageScreenPos.x, mPos.y - imageScreenPos.y });
                debugEditor_->SetGameViewHovered(isHovered);
                debugEditor_->Update();
                is3DGizmoBusy = ImGuizmo::IsUsing();
            }

            // --- B. スプライトエディタの更新 (マウス座標の補正) ---
            if (showSpriteInspector_) {
                // ウィンドウ内の相対座標を計算
                float localX = mPos.x - imageScreenPos.x;
                float localY = mPos.y - imageScreenPos.y;

                // スプライトの基準解像度 (1280x720) に合わせて座標をスケール変換
                // これにより、Game View を縮小していても正しくクリックできる
                float gameResW = 1280.0f;
                float gameResH = 720.0f;
                Vector2 spriteLocalPos = {
                    localX * (gameResW / displaySize.x),
                    localY * (gameResH / displaySize.y)
                };

                spriteDebugEditor_->Update(spriteLocalPos, isHovered);
                isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
            }

            // --- C. 録画プレビュー (GhostRecorder) の可視化 ---
            if (ghostRecorder_ && !isPlaying_) {
                Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
                if (camera) {
                    ghostRecorder_->DrawPreview(
                        camera->GetViewProjectionMatrix(),
                        Vector2{ imageScreenPos.x, imageScreenPos.y },
                        Vector2{ displaySize.x, displaySize.y }
                    );
                }
            }

            // カメラのアスペクト比を Game View に合わせる
            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            if (camera) {
                camera->SetAspectRatio(targetAspect);
                camera->UpdateProjectionMatrix();
            }

            // デバッグ情報
            ImGui::SetCursorScreenPos(ImVec2(imageScreenPos.x + 10, imageScreenPos.y + 10));
            ImGui::Text("Hovered: %s", isHovered ? "TRUE" : "FALSE");
            ImGui::Text("Game View Size: %.0f x %.0f", displaySize.x, displaySize.y);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // -------------------------------------------------------------------------
    // 3. メインメニューバー
    // -------------------------------------------------------------------------
    if (ImGui::BeginMainMenuBar()) {
        static bool prevIsPlaying = isPlaying_;
        if (isPlaying_) {
            if (ImGui::Button("■ 停止")) isPlaying_ = false;
        } else {
            if (ImGui::Button("▶ 再生")) isPlaying_ = true;
        }

        if (prevIsPlaying != isPlaying_ && !isPlaying_) {
            sceneManager_->ChangeScene(currentSceneName_);
            CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
        }
        prevIsPlaying = isPlaying_;

        ImGui::Text(isPlaying_ ? " | 実行中" : " | 編集モード");

        if (ImGui::BeginMenu("表示")) {
            ImGui::MenuItem("Hierarchy / Inspector", NULL, &showDebugWindows_);
            ImGui::MenuItem("スプライト", NULL, &showSpriteInspector_);
            ImGui::MenuItem("パーティクル", NULL, &showParticleEditor_);
            ImGui::MenuItem("録画", NULL, &showGhostRecorder_);
            ImGui::MenuItem("カメラ", NULL, &showCameraEditor);
            ImGui::Separator();
            ImGui::MenuItem("ライティング", NULL, &showLightEditor_);
            ImGui::MenuItem("デバッグログ", NULL, &showDebugConsole_);
            ImGui::MenuItem("ステータス", NULL, &showTimeController_);
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

    // --- deltaTime計算 ---
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> duration = currentTime - lastTime_;
    float deltaTime = (duration.count() > 0.1f) ? 1.0f / 60.0f : duration.count();
    lastTime_ = currentTime;

    float finalDeltaTime = isPlaying_ ? (deltaTime * timeScale_) : 0.0f;

#ifdef USE_IMGUI
    // -------------------------------------------------------------------------
    // 4. 各種エディタウィンドウの描画 (UI部分)
    // -------------------------------------------------------------------------
    if (showDebugWindows_) {
        debugEditor_->DrawImGui();
    }
    if (showSpriteInspector_) {
        // SpriteDebugEditor::Update は GameView 内で行ったので、ここでは描画のみ
        spriteDebugEditor_->DrawImGui();
    }
    if (showParticleEditor_) {
        particleEditor_->Update();
        particleEditor_->DrawImGui();
    }
    if (showGhostRecorder_) {
        ImGui::Begin("録画", &showGhostRecorder_);
        ghostRecorder_->Update();
        ghostRecorder_->DrawImGui();
        ImGui::End();
    }
    if (showLightEditor_) LightEditor::GetInstance()->DrawImGui();
    if (showCameraEditor) CameraEditor::GetInstance()->DrawImGui();
    if (showDebugConsole_) DebugConsole::GetInstance()->DrawImGui();
    if (showTimeController_) {
        ImGui::Begin("ステータス", &showTimeController_);
        ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
        ImGui::SliderFloat("時間倍率", &timeScale_, 0.0f, 2.0f);
        ImGui::End();
    }


    // ギズモ操作中はカメラ入力をオフにする
    Camera* mainCam = CameraManager::GetInstance()->GetActiveCamera();
    if (mainCam) { mainCam->SetInputEnabled(!(isSpriteEditorBusy || is3DGizmoBusy)); }
#endif
    if (sceneManager_) { sceneManager_->Update(finalDeltaTime); }
    LightManager::GetInstance()->Update();
}



void Game::Draw() {
#ifdef USE_IMGUI
    // =================================================================
    // パターンA: エディタモード (Develop / Debug)
    // =================================================================
    // 1. まず「レンダーテクスチャ（ゲーム画面用）」をクリアして描画先にセット
    dxCommon_->PreDrawRenderTexture();

    // 2. ゲームの中身をテクスチャに描画
    if (sceneManager_) {
        sceneManager_->Draw();
    }

    // 3. デバッグ表示（グリッドやコライダー枠など）もテクスチャに描画
    if (debugEditor_) {
        debugEditor_->DrawDebug(dxCommon_->GetCommandList());
    }



    // 4. テクスチャへの描画終了
    dxCommon_->PostDrawRenderTexture();

    // ---------------------------------------------------------------

    // 5. 次に「本物の画面（バックバッファ）」をクリアして描画先にセット
    //    ※ここでImGuiのウィンドウなどを描画します
    dxCommon_->PreDrawBackBuffer();

    // 6. ImGuiの描画 (さっき作ったテクスチャがGameViewウィンドウ内に表示される)
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Draw();
    }
    ImGuiManager::GetInstance()->Draw();

    // 7. 描画終了 (画面フリップ)
    dxCommon_->PostDraw();
    ImGuiManager::GetInstance()->EndFrame();

#else
    // =================================================================
    // パターンB: ゲームモード (Release)
    // =================================================================
    // 1. いきなり「本物の画面（バックバッファ）」をクリアして描画先にセット
    //    ※余計な切り替えは一切しません
    dxCommon_->PreDraw();

    // 2. ゲームの中身を直接画面に描画
    if (sceneManager_) {
        sceneManager_->Draw();
    }

    // (Releaseではデバッグ表示やImGui描画はスキップ)

    // 3. 描画終了 (画面フリップ)
    dxCommon_->PostDraw();

#endif

    // FPS固定処理
    dxCommon_->UpdateFixFPS();
}