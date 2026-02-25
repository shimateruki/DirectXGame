#include "Game.h"
#include "SceneManager.h" 
#include "ImguiManager.h"
#include "InputManager.h"
#include"DebugConsole.h"
#include "SceneFactory.h"
#include "LightManager.h"
#include"WinApp.h"
#include"imgui.h"
#include "ImGuizmo.h" 
#include <chrono>
#include <ParticleManager.h>
#include <SrvManager.h>
#include"EditorManager.h"
#include"ModelManager.h"

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
    postEffect_ = std::make_unique<PostEffect>();
    postEffect_->Initialize(dxCommon_);
    uint32_t lutHandle = TextureManager::GetInstance()->Load("Resources/sprite/particle.png");
    postEffect_->SetLUTTexture(lutHandle);
    postEffectEditor_ = std::make_unique<PostEffectEditor>();
    postEffectEditor_->Initialize(postEffect_.get());
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
        ImGui::DockBuilderDockWindow("Inspector", dock_right_id); // ★右側はこれ1つだけ！

        ImGui::DockBuilderDockWindow("Project (Assets)", dock_bottom_id);
        ImGui::DockBuilderDockWindow("デバッグログ", dock_bottom_id);
        ImGui::DockBuilderDockWindow("Game View", dock_main_id);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    bool isSpriteEditorBusy = false;
    bool is3DGizmoBusy = false;

    // -------------------------------------------------------------------------
    // 2. Game View ウィンドウ (余白なし・タブバー非表示設定)
    // -------------------------------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowClass window_class;
    window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&window_class);

    ImGui::Begin("Game View", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        ImVec2 displaySize = windowSize;
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();

        if (displaySize.x > 0 && displaySize.y > 0) {
            uint32_t texHandle = postEffect_->GetSRVHandle(1);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(texHandle);
            ImGui::Image((ImTextureID)gpuHandle.ptr, displaySize);

            bool isHovered = ImGui::IsItemHovered();
            ImVec2 mPos = ImGui::GetIO().MousePos;

            // --- A. 3DデバッグエディタのGameView連携 ---
            if (debugEditor_) {
                debugEditor_->SetGameViewRegion({ imageScreenPos.x, imageScreenPos.y }, { displaySize.x, displaySize.y });
                debugEditor_->SetGameViewMousePos({ mPos.x - imageScreenPos.x, mPos.y - imageScreenPos.y });
                debugEditor_->SetGameViewHovered(isHovered);
                debugEditor_->Update();
                is3DGizmoBusy = ImGuizmo::IsUsing();
            }

            // --- B. スプライトエディタ連携 ---
            if (spriteDebugEditor_) {
                float localX = mPos.x - imageScreenPos.x;
                float localY = mPos.y - imageScreenPos.y;
                float gameResW = WinApp::kClientWidth;
                float gameResH = WinApp::kClientHeight;
                Vector2 spriteLocalPos = { localX * (gameResW / displaySize.x), localY * (gameResH / displaySize.y) };

                spriteDebugEditor_->Update(spriteLocalPos, isHovered);
                isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
            }

            // --- C. ゴーストレコーダー連携 ---
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

            // カメラのアスペクト比を画面に追従
            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            if (camera) {
                camera->SetAspectRatio(displaySize.x / displaySize.y);
                camera->UpdateProjectionMatrix();
            }
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
            ImGui::MenuItem("Hierarchy / Inspector 表示", NULL, &showDebugWindows_);
            ImGui::Separator();

            // =========================================================
            // ★ 各エディタを Inspector に呼び出すためのボタン！
            // =========================================================
            ImGui::TextDisabled("Inspectorに表示:");
            if (ImGui::MenuItem("スプライトエディタ")) {
                EditorManager::GetInstance()->SetSelectedObject(spriteDebugEditor_.get());
                showDebugWindows_ = true;
            }
            if (ImGui::MenuItem("パーティクルエディタ")) {
                EditorManager::GetInstance()->SetSelectedObject(particleEditor_.get());
                showDebugWindows_ = true;
            }
            if (ImGui::MenuItem("カメラエディタ")) {
                EditorManager::GetInstance()->SetSelectedObject(CameraEditor::GetInstance());
                showDebugWindows_ = true;
            }
            if (ImGui::MenuItem("ライティングエディタ")) {
                EditorManager::GetInstance()->SetSelectedObject(LightEditor::GetInstance());
                showDebugWindows_ = true;
            }
            if (ImGui::MenuItem("ポストエフェクトエディタ")) {
                EditorManager::GetInstance()->SetSelectedObject(postEffectEditor_.get());
                showDebugWindows_ = true;
            }
            if (ImGui::MenuItem("ゴーストレコーダー (パス生成)")) {
                EditorManager::GetInstance()->SetSelectedObject(ghostRecorder_.get());
                showDebugWindows_ = true;
            }

            ImGui::Separator();
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
    // 4. エディタ描画の総仕上げ！
    // -------------------------------------------------------------------------
    if (showDebugWindows_) {
        // ① 左パネル (Hierarchy) の描画
        if (debugEditor_) debugEditor_->DrawHierarchy();

        // ② 右パネル (Inspector) の描画
        EditorManager::GetInstance()->DrawInspector();
    }

    // 独立したウィンドウ (ログとステータス)
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
    postEffect_->GetParams()->time += deltaTime;

}
void Game::Draw() {
#ifdef USE_IMGUI
    // =================================================================
    // パターンA: エディタモード (Develop / Debug)
    // =================================================================

    // ---------------------------------------------------------------
    // 1. シーンレンダリング (オフスクリーン描画)
    // ---------------------------------------------------------------
    dxCommon_->PreDrawRenderTexture();

    if (sceneManager_) { sceneManager_->Draw(); }
    if (debugEditor_) { debugEditor_->DrawDebug(dxCommon_->GetCommandList()); }

    dxCommon_->PostDrawRenderTexture();

    // ---------------------------------------------------------------
    // 2. ポストエフェクト・マルチパス (ブルーム生成)
    // ---------------------------------------------------------------
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    uint32_t texA_Handle = dxCommon_->GetRenderTextureSrvHandle();

    // [Pass 1] 高輝度抽出 (抽出用テクスチャへ)
    postEffect_->PreDrawScene(commandList, 2);
    postEffect_->Draw(commandList, texA_Handle, 2);
    postEffect_->TransitionToSRV(commandList, 2);

    // [Pass 2-4] 段階的ダウンサンプリング (ぼかしの生成)
    postEffect_->PreDrawScene(commandList, 3);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(2), 3);
    postEffect_->TransitionToSRV(commandList, 3);

    postEffect_->PreDrawScene(commandList, 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(3), 3);
    postEffect_->TransitionToSRV(commandList, 4);

    postEffect_->PreDrawScene(commandList, 5);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(4), 3);
    postEffect_->TransitionToSRV(commandList, 5);

    // [Pass 5] ベース画像の複写
    postEffect_->PreDrawScene(commandList, 0);
    postEffect_->Draw(commandList, texA_Handle, 0);

    // [Pass 6] ブルーム加算合成 (各解像度のぼかしを元絵に乗せる)
    postEffect_->PreDrawScene(commandList, 0, false);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(2), 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(3), 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(4), 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(5), 4);
    postEffect_->TransitionToSRV(commandList, 0);

    // [Pass 7] 最終カラーグレーディング・トーンマッピング
    postEffect_->PreDrawScene(commandList, 1);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(0), 1);

    // ---------------------------------------------------------------
    // 3. ゲームUI描画 (SDR合成後のテクスチャへ描き込み)
    // ---------------------------------------------------------------
    if (sceneManager_) {
        sceneManager_->DrawUI();
    }

    // ImGui(GameView)での表示用にリソースを変換
    postEffect_->TransitionToSRV(commandList, 1);

    // ---------------------------------------------------------------
    // 4. バックバッファ描画 & エディタUI合成
    // ---------------------------------------------------------------
    dxCommon_->PreDrawBackBuffer();

    if (spriteDebugEditor_) { spriteDebugEditor_->Draw(); }
    ImGuiManager::GetInstance()->Draw();

    dxCommon_->PostDraw();
    ImGuiManager::GetInstance()->EndFrame();

#else
    // =================================================================
    // パターンB: ゲームモード (Release)
    // =================================================================

    // 1. シーンレンダリング
    dxCommon_->PreDrawRenderTexture();
    if (sceneManager_) { sceneManager_->Draw(); }
    dxCommon_->PostDrawRenderTexture();

    // 2. ポストエフェクト・マルチパス (ブルーム生成)
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    uint32_t texA_Handle = dxCommon_->GetRenderTextureSrvHandle();

    postEffect_->PreDrawScene(commandList, 2);
    postEffect_->Draw(commandList, texA_Handle, 2);
    postEffect_->TransitionToSRV(commandList, 2);

    postEffect_->PreDrawScene(commandList, 3);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(2), 3);
    postEffect_->TransitionToSRV(commandList, 3);

    postEffect_->PreDrawScene(commandList, 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(3), 3);
    postEffect_->TransitionToSRV(commandList, 4);

    postEffect_->PreDrawScene(commandList, 5);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(4), 3);
    postEffect_->TransitionToSRV(commandList, 5);

    postEffect_->PreDrawScene(commandList, 0);
    postEffect_->Draw(commandList, texA_Handle, 0);

    postEffect_->PreDrawScene(commandList, 0, false);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(2), 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(3), 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(4), 4);
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(5), 4);
    postEffect_->TransitionToSRV(commandList, 0);

    // 3. 最終出力 (バックバッファへトーンマップ適用 & UI描画)
    dxCommon_->PreDraw();

    // HDRからSDRへの変換と最終エフェクトの適用
    postEffect_->Draw(commandList, postEffect_->GetSRVHandle(0), 1);

    // ゲームUIのオーバーレイ描画
    if (sceneManager_) {
        sceneManager_->DrawUI();
    }

    dxCommon_->PostDraw();

#endif

    // FPS固定処理
    dxCommon_->UpdateFixFPS();
}