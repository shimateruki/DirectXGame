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
#include "GhostDirector.h"
#include"BossCore.h"
#include "KeyConfig.h"
#include "StageManager.h"
#include <IconsFontAwesome5.h>
#include <MeshEffectManager.h>
#include "GameDataManager.h"
#include "engine/graphics/postprocess/Fade.h"
#include "ProfilerManager.h"

void Game::Initialize() {
    // Frameworkの初期化処理
    Framework::Initialize();
    ProfilerManager::GetInstance()->Initialize();
    TextureManager::GetInstance()->LoadAllTexture("Resources/sprite/");
    TextureManager::GetInstance()->LoadAllTexture("Resources/texture/PBR/");
    ModelManager::GetInstance()->LoadAllModels();
    StageManager::GetInstance()->Initialize();
    GameDataManager::GetInstance()->Initialize();
    sceneFactory_ = std::make_unique<SceneFactory>();
    //  SceneManager を作成して初期化
    sceneManager_ = std::make_unique<SceneManager>();
    // =========================================================
    //  ローカル設定を見て開始シーンを決める
    // =========================================================
    std::string startScene = "GAMEPLAY"; // デフォルト

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

    // =========================================================
    // 天球（Skydome）の値をプログラムで上書きする！
    // =========================================================
    if (auto currentScene = sceneManager_->GetCurrentScene()) {
        // シーン内の全オブジェクトをループして天球を探す
        for (auto& obj : currentScene->GetObjects()) {
            if (obj && obj->GetName() == "Skydome") { // エディターでの名前に合わせてください

                obj->SetSelectedLighting(0);

                DebugConsole::GetInstance()->AddLog("🌌 Skydome settings have been overwritten!");
                break; // 見つかったらループ終了
            }
        }
    }


    lastTime_ = std::chrono::high_resolution_clock::now();
    PostEffect::GetInstance()->Initialize(dxCommon_);
    uint32_t lutHandle = TextureManager::GetInstance()->Load("Resources/sprite/particle.png");
    PostEffect::GetInstance()->SetLUTTexture(lutHandle);

    postEffectEditor_ = std::make_unique<PostEffectEditor>();
    postEffectEditor_->Initialize(PostEffect::GetInstance());
  
    Fade::GetInstance()->Initialize();
    KeyConfig::GetInstance()->Initialize();
#ifdef USE_IMGUI

    postEffectEditor_->Initialize(PostEffect::GetInstance());
    spriteDebugEditor_ = std::make_unique<SpriteDebugEditor>();
    spriteDebugEditor_->Initialize(sceneManager_.get(), InputManager::GetInstance());
    ghostRecorder_ = std::make_unique<GhostRecorder>();
    ghostRecorder_->Initialize(sceneManager_.get());
    debugEditor_ = std::make_unique<DebugEditor>();
    debugEditor_->Initialize(sceneManager_.get(), dxCommon_);
    sceneManager_->SetDebugEditor(debugEditor_.get());
    particleEditor_ = std::make_unique<ParticleEditor>();
    particleEditor_->Initialize(sceneManager_.get());
    gpuParticleEditor_ = std::make_unique<GPUParticleEditor>();
    gpuParticleEditor_->Initialize();
    vfxSequencerEditor_ = std::make_unique<VFXSequencerEditor>();
    vfxSequencerEditor_->Initialize();
    meshEffectEditor_ = std::make_unique<MeshEffectEditor>();
    meshEffectEditor_->Initialize(sceneManager_.get());
    trailEmitterEditor_ = std::make_unique<TrailEmitterEditor>();
    trailEmitterEditor_->Initialize(sceneManager_.get());
    LightEditor::GetInstance()->Initialize();
    DebugConsole::GetInstance()->Initialize();
    ghostDirector_ = std::make_unique<GhostDirector>();
    ghostDirector_->Initialize(sceneManager_.get());
    if (auto currentScene = sceneManager_->GetCurrentScene()) {
        currentScene->SetDebugEditor(debugEditor_.get());
    }
    if (debugEditor_) {
        debugEditor_->SetEditors(
            postEffectEditor_.get(),
            spriteDebugEditor_.get(),
            particleEditor_.get(),
            gpuParticleEditor_.get(),
            vfxSequencerEditor_.get(),
            ghostRecorder_.get(),
            ghostDirector_.get(),
			LightEditor::GetInstance(),
            meshEffectEditor_.get(),
            trailEmitterEditor_.get()
        );
    }

#endif
#ifdef  USE_IMGUI
    isPlaying_ = false; // デバッグ時は停止状態（エディタ操作）から
#else
    isPlaying_ = true;  // リリース時は最初から再生
    WinApp::SetCursorVisibility(false);
	winApp_->SetCursorClipping(true);
    winApp_->SetCursorLocked(true);
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
#endif
#ifdef USE_IMGUI
    CameraEditor::GetInstance()->SetMode(isPlaying_ ? CameraEditor::Mode::Game : CameraEditor::Mode::Editor);
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
    ghostDirector_.reset();
    gpuParticleEditor_.reset();
    vfxSequencerEditor_.reset();
   DebugConsole::GetInstance()->Finalize();
#endif

    // ★ 基底クラスの終了処理を呼ぶ
    Framework::Finalize();
}



void Game::Update() {
    InputManager::GetInstance()->Update();
    if (InputManager::GetInstance()->IsKeyTriggered(DIK_ESCAPE)) {
        PostQuitMessage(0); // Windowsに「アプリを終了して！」とメッセージを送る
        return;             // 今回のフレームの更新はここで打ち切る
    }
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

        // ★修正: 下部パネルを作成 (高さ30%)
        ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.30f, nullptr, &dock_main_id);

        ImGuiID dock_bottom_left_id = ImGui::DockBuilderSplitNode(dock_bottom_id, ImGuiDir_Left, 0.60f, nullptr, &dock_bottom_id);
        ImGuiID dock_bottom_right_id = dock_bottom_id; // 残りが右側になる

        ImGui::DockBuilderDockWindow("Hierarchy", dock_left_id);
        // ★追加: 3D用と同じ "dock_left_id" に入れることで自動的にタブ化される！
        ImGui::DockBuilderDockWindow(ICON_FA_LIST_UL " Sprite Hierarchy", dock_left_id);

        ImGui::DockBuilderDockWindow("Inspector", dock_right_id);
        // ★追加: 3D用と同じ "dock_right_id" に入れることで自動的にタブ化される！
        ImGui::DockBuilderDockWindow(ICON_FA_INFO_CIRCLE " Sprite Inspector", dock_right_id);

        // ★下部・左側にアセットを配置
        ImGui::DockBuilderDockWindow("Project (Assets)", dock_bottom_left_id);
        // ★追加: 3D用と同じ "dock_bottom_left_id" に入れることで自動的にタブ化される！
        ImGui::DockBuilderDockWindow(ICON_FA_FOLDER_OPEN " Sprite Assets", dock_bottom_left_id);
    

        // ★下部・左側にアセットを配置
        ImGui::DockBuilderDockWindow("Project (Assets)", dock_bottom_left_id);


        ImGui::DockBuilderDockWindow("Debug Console", dock_bottom_right_id);
        ImGui::DockBuilderDockWindow("ステータス", dock_bottom_right_id);

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
            uint32_t texHandle = PostEffect::GetInstance()->GetSRVHandle(1);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(texHandle);

            // ★ ここで描画される画像が、ドラッグ＆ドロップの「的（ターゲット）」になる！
            ImGui::Image((ImTextureID)gpuHandle.ptr, displaySize);

            // =======================================================
            // ★ Game View へのドラッグ＆ドロップ統合受け取り口
            // =======================================================
            if (ImGui::BeginDragDropTarget()) {

                // [A] スプライト画像が落ちてきた場合
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
                    const char* droppedFilename = (const char*)payload->Data;

                    if (sceneManager_) {
                        BaseScene* currentScene = sceneManager_->GetCurrentScene();
                        if (currentScene) {
                            SpriteCommon* spriteCommon = currentScene->GetSpriteCommon();
                            auto& sprites = currentScene->GetSprites();

                            ImVec2 mousePos = ImGui::GetIO().MousePos;
                            float localX = mousePos.x - imageScreenPos.x;
                            float localY = mousePos.y - imageScreenPos.y;
                            float gameResW = float(WinApp::kClientWidth);
                            float gameResH = float(WinApp::kClientHeight);
                            Vector2 dropPos = { localX * (gameResW / displaySize.x), localY * (gameResH / displaySize.y) };

                            std::string fullPath = "Resources/sprite/" + std::string(droppedFilename);
                            auto newSprite = std::make_unique<Sprite>();
                            uint32_t handle = TextureManager::GetInstance()->Load(fullPath);

                            newSprite->Initialize(spriteCommon, handle);
                            newSprite->SetName(droppedFilename);
                            newSprite->SetTextureName(droppedFilename);
                            newSprite->SetPosition(dropPos);

                            sprites.push_back(std::move(newSprite));

                            if (spriteDebugEditor_) {
                                spriteDebugEditor_->SetSelectedSprite(sprites.back().get());
                            }
                            DebugConsole::GetInstance()->AddLog("Dropped Sprite: " + std::string(droppedFilename));
                        }
                    }
                }

                // [B] 3Dモデルが落ちてきた場合
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                    const char* droppedModelName = (const char*)payload->Data;

                    if (debugEditor_) {
                        // ドロップした瞬間の最新マウス座標を渡して、完璧な位置に配置させる！
                        ImVec2 mPos = ImGui::GetIO().MousePos;
                        debugEditor_->SetGameViewMousePos({ mPos.x - imageScreenPos.x, mPos.y - imageScreenPos.y });

                        debugEditor_->InstantiateModelAtCursor(droppedModelName);
                    }
                }

                // [C] プリセットが落ちてきた場合
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                    const char* droppedPresetName = (const char*)payload->Data;

                    if (debugEditor_) {
                        ImVec2 mPos = ImGui::GetIO().MousePos;
                        debugEditor_->SetGameViewMousePos({ mPos.x - imageScreenPos.x, mPos.y - imageScreenPos.y });

                        debugEditor_->InstantiatePresetAtCursor(droppedPresetName);
                    }
                }

                ImGui::EndDragDropTarget();
            }
            // =======================================================

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
                float gameResW = float(WinApp::kClientWidth);
                float gameResH = float(WinApp::kClientHeight);
                Vector2 spriteLocalPos = { localX * (gameResW / displaySize.x), localY * (gameResH / displaySize.y) };

                spriteDebugEditor_->Update(spriteLocalPos, isHovered);
                isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
            }

            // --- C. ゴーストレコーダー連携 ---
            if (ghostRecorder_ && !isPlaying_) {
                if (EditorManager::GetInstance()->GetSelectedObject() == ghostRecorder_.get()) {
                    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
                    if (camera) {
                        ghostRecorder_->DrawPreview(
                            camera->GetViewProjectionMatrix(),
                            Vector2{ imageScreenPos.x, imageScreenPos.y },
                            Vector2{ displaySize.x, displaySize.y }
                        );
                    }
                }
            }

            if (ghostDirector_ && !isPlaying_) {
                if (EditorManager::GetInstance()->GetSelectedObject() == ghostDirector_.get()) {
                    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
                    if (camera) {
                        ghostDirector_->DrawPreview(
                            camera->GetViewProjectionMatrix(),
                            Vector2{ imageScreenPos.x, imageScreenPos.y },
                            Vector2{ displaySize.x, displaySize.y }
                        );
                    }
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
            if (ImGui::Button ("■ 停止")) {
                isPlaying_ = false;
                if (sceneManager_) { sceneManager_->SetIsPlaying (false); }
            }
        } else {
            if (ImGui::Button ("▶ 再生")) {
                SaveAllEditors ();
                sceneManager_->ChangeScene (currentSceneName_);
                // Play開始時: シーン再生成で common_ が無効になる前にエフェクトをクリア
                MeshEffectManager::GetInstance()->Clear();
                isPlaying_ = true;
                if (sceneManager_) { sceneManager_->SetIsPlaying (true); }
                CameraEditor::GetInstance ()->SetMode (CameraEditor::Mode::Game);
            }
        }

        if (prevIsPlaying != isPlaying_ && !isPlaying_) {
            // Stop時: シーン再生成で common_ が無効になる前にエフェクトをクリア
            MeshEffectManager::GetInstance()->Clear();
            sceneManager_->ChangeScene(currentSceneName_);
            CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
        }
        prevIsPlaying = isPlaying_;
        ImGui::Text(isPlaying_ ? " | 実行中" : " | 編集モード");

        if (ImGui::BeginMenu("表示")) {
            ImGui::MenuItem("Hierarchy / Inspector 表示", NULL, &showDebugWindows_);
            ImGui::Separator();

          

            ImGui::MenuItem("デバッグログ", NULL, &showDebugConsole_);
            ImGui::MenuItem("ステータス", NULL, &showTimeController_);
            ImGui::MenuItem("ボスロジックデバッグ", NULL, &showBossDebug_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("シーン切り替え")) {
            const char* sceneNames[] = { "TITLE", "SELECT", "GAMEPLAY", "GAMEOVER", "GAMECLEAR", "PREVIEW" };
            for (int i = 0; i < _countof(sceneNames); i++) {
                if (ImGui::MenuItem(sceneNames[i])) sceneManager_->ChangeScene(sceneNames[i]);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("ヘルプ")) {
            if (ImGui::MenuItem(ICON_FA_BOOK " エンジン説明書")) {
                engineManualWindow_.Open();
            }
            if (ImGui::MenuItem(ICON_FA_CHART_BAR " システムプロファイラ")) {
                ProfilerManager::GetInstance()->Open();
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
    if (gpuParticleEditor_) {
        gpuParticleEditor_->Update(deltaTime);
    }
    if (vfxSequencerEditor_) {
        vfxSequencerEditor_->Update(deltaTime);
    }
    if (meshEffectEditor_) {
        meshEffectEditor_->Update(deltaTime);
    }
    if (trailEmitterEditor_) {
        trailEmitterEditor_->Update(deltaTime);
    }
    // エディタ停止中は各エディタがUpdate()を担当するが、
    // Play中はエディタがスキップするためGame側で明示的に呼ぶ
    if (isPlaying_) {
        MeshEffectManager::GetInstance()->Update(deltaTime * timeScale_);
        GPUParticleManager::GetInstance()->Update(deltaTime * timeScale_);
    }
    // -------------------------------------------------------------------------
    // 4. エディタ描画の総仕上げ！
    // -------------------------------------------------------------------------
    if (showDebugWindows_) {
        // ① 左パネル (Hierarchy) の描画
        if (debugEditor_) debugEditor_->DrawHierarchy();

        // ② 右パネル (Inspector) の描画
        EditorManager::GetInstance()->DrawInspector();
        if (spriteDebugEditor_) {
            spriteDebugEditor_->DrawHierarchyWindow();
            spriteDebugEditor_->DrawInspectorWindow();
            spriteDebugEditor_->DrawProjectWindow();
        }
    }

    // 独立したウィンドウ (ログとステータス)
    if (showDebugConsole_) DebugConsole::GetInstance()->DrawImGui();
    if (showTimeController_) {
        ImGui::Begin("ステータス", &showTimeController_);
        ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
        ImGui::SliderFloat("時間倍率", &timeScale_, 0.0f, 2.0f);
        ImGui::Separator();
        ImGui::Text("--- CPU パフォーマンス ---");
        float cpuTotalWorkMs = sceneUpdateTimeMs_ + cpuCmdTimeMs_;
        ImGui::Text("CPU 稼働合計: %.3f ms", cpuTotalWorkMs);
        ImGui::ProgressBar(cpuTotalWorkMs / 16.66f, ImVec2(0.f, 0.f));
        
        if (ImGui::TreeNode("詳細内訳 (CPU)")) {
            ImGui::Text("  シーン更新  : %.3f ms", sceneUpdateTimeMs_);
            ImGui::Text("  描画命令発行: %.3f ms", cpuCmdTimeMs_);
            ImGui::TreePop();
        }
        
        ImGui::PlotLines("##UpdateGraph", updateTimeHistory_, 120, timeHistoryIndex_,
            "CPU負荷推移 (ms)", 0.0f, 16.66f, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));

        ImGui::Separator();
        ImGui::Text("--- GPU パフォーマンス ---");
        float gpuTotalMs = dxCommon_->GetGpuDrawTimeMs();
        ImGui::Text("GPU 稼働合計: %.3f ms", gpuTotalMs);
        ImGui::ProgressBar(gpuTotalMs / 16.66f, ImVec2(0.f, 0.f));
        
        // GPU待機時間は「CPUが何もできずに待っている時間」
        float gpuWaitMs = drawTimeMs_ - cpuCmdTimeMs_;
        ImGui::Text("CPU 待機時間 : %.3f ms (VSync待ち含む)", gpuWaitMs > 0 ? gpuWaitMs : 0.0f);
        
        ImGui::PlotLines("##DrawGraph", drawTimeHistory_, 120, timeHistoryIndex_,
            "フレーム全体 (ms)", 0.0f, 16.66f, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));
        ImGui::End();
    }
    engineManualWindow_.Draw();
    ProfilerManager::GetInstance()->DrawImGui();
    // ギズモ操作中はカメラ入力をオフにする
    Camera* mainCam = CameraManager::GetInstance()->GetActiveCamera();
    if (mainCam) { mainCam->SetInputEnabled(!(isSpriteEditorBusy || is3DGizmoBusy)); }
#endif

    // ↓ 計測開始
    auto startUpdate = std::chrono::high_resolution_clock::now();

    {
        PROFILE_SCOPE("  シーン");
        if (sceneManager_) { sceneManager_->Update(finalDeltaTime); }
    }
    {
        PROFILE_SCOPE("  ライト");
        LightManager::GetInstance()->Update();
    }
    {
        PROFILE_SCOPE("  パーティクル");
        GPUParticleManager::GetInstance()->Update(deltaTime);
    }
    {
        PROFILE_SCOPE("  フェード");
        Fade::GetInstance()->Update(deltaTime);
    }
    PostEffect::GetInstance()->GetParams()->time += deltaTime;

    if (sceneManager_) {
        sceneManager_->SetIsPlaying(isPlaying_);
    }

    // ↓ 計測終了
    auto endUpdate = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> updateDuration = endUpdate - startUpdate;
    sceneUpdateTimeMs_ = updateDuration.count();
    updateTimeHistory_[timeHistoryIndex_] = sceneUpdateTimeMs_;
    timeHistoryIndex_ = (timeHistoryIndex_ + 1) % 120;

    // プロファイラにCPU Update時間を送信
    ProfilerManager::GetInstance()->RecordCpuTime("更新処理", sceneUpdateTimeMs_);
}

void Game::Draw() {
    PostEffect* postEffect_ = PostEffect::GetInstance();
    // ★ 前フレームのGPUの計測結果を読み取る
    dxCommon_->ReadAllGpuProfiles();

    // CPUプロファイラ開始
    auto startDraw = std::chrono::high_resolution_clock::now();

#ifdef USE_IMGUI
    // =================================================================
    // パターンA: エディタモード (Develop / Debug)
    // =================================================================



    // ---------------------------------------------------------------
    // 1. シーンレンダリング (オフスクリーン描画)
    // ---------------------------------------------------------------
    dxCommon_->PreDrawRenderTexture();
#ifdef USE_IMGUI
    if (debugEditor_ && debugEditor_->GetProjectWindow()) {
        debugEditor_->GetProjectWindow()->CapturePendingThumbnails();
    }
#endif
    dxCommon_->StartGpuProfile("Total");
    dxCommon_->PreDrawShadow();
    SRVManager::GetInstance()->SetDescriptorHeaps(dxCommon_->GetCommandList());
    // ★ GPUストップウォッチ開始！
    dxCommon_->StartGpuProfile("影描画");
    if (sceneManager_) {
        sceneManager_->DrawShadow();
    }
    dxCommon_->EndGpuProfile("影描画");
    dxCommon_->PostDrawShadow();

    dxCommon_->StartGpuProfile("メイン描画");
    
    dxCommon_->StartGpuProfile("  3Dシーン");
    if (sceneManager_) { sceneManager_->Draw(); }
    dxCommon_->EndGpuProfile("  3Dシーン");
    
    dxCommon_->StartGpuProfile("  ゲームUI");
    if (sceneManager_) {
        sceneManager_->DrawUI();
    }
    dxCommon_->EndGpuProfile("  ゲームUI");
    
    // VFXSequencerが生成したエフェクトを描画
    dxCommon_->StartGpuProfile("  エフェクト");
    {
        ID3D12Resource* pLight = LightManager::GetInstance()->GetPointLightResource();
        ID3D12Resource* sLight = LightManager::GetInstance()->GetSpotLightResource();
        MeshEffectManager::GetInstance()->Draw(pLight, sLight);
    }
    dxCommon_->EndGpuProfile("  エフェクト");

    dxCommon_->StartGpuProfile("  デバッグ");
    if (debugEditor_) { debugEditor_->DrawDebug(dxCommon_->GetCommandList()); }
    if (meshEffectEditor_ && EditorManager::GetInstance()->GetSelectedObject() == meshEffectEditor_.get()) {
        meshEffectEditor_->Draw();
    }
    dxCommon_->EndGpuProfile("  デバッグ");
    
    dxCommon_->EndGpuProfile("メイン描画");
    dxCommon_->PostDrawRenderTexture();

    // ---------------------------------------------------------------
    // 2. ポストエフェクト・マルチパス (ブルーム生成)
    // ---------------------------------------------------------------
    dxCommon_->StartGpuProfile("後処理");
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

    // ImGui(GameView)での表示用にリソースを変換
    postEffect_->TransitionToSRV(commandList, 1);

    dxCommon_->EndGpuProfile("後処理");

    // ---------------------------------------------------------------
    // 4. バックバッファ描画 & エディタUI合成
    // ---------------------------------------------------------------
    dxCommon_->StartGpuProfile("エディタUI");
    dxCommon_->PreDrawBackBuffer();

    if (spriteDebugEditor_) { spriteDebugEditor_->Draw(); }
    ImGuiManager::GetInstance()->Draw();
    dxCommon_->EndGpuProfile("エディタUI");

    dxCommon_->EndGpuProfile("Total");

    prePostDrawTime_ = std::chrono::high_resolution_clock::now();
    dxCommon_->PostDraw();
    ImGuiManager::GetInstance()->EndFrame();

#else
    // =================================================================
    // パターンB: ゲームモード (Release)
    // =================================================================

    // 1. シーンレンダリング
    dxCommon_->PreDrawRenderTexture();

    dxCommon_->StartGpuProfile("Total");
    dxCommon_->PreDrawShadow();
    SRVManager::GetInstance()->SetDescriptorHeaps(dxCommon_->GetCommandList());

    // ★ GPUストップウォッチ開始！
    dxCommon_->StartGpuProfile("影描画");

    if (sceneManager_) {
        sceneManager_->DrawShadow();
    }
    dxCommon_->EndGpuProfile("影描画");
    dxCommon_->PostDrawShadow();

    // メイン画面の描画
    dxCommon_->StartGpuProfile("メイン描画");
    if (sceneManager_) { sceneManager_->Draw(); }
    // ゲームUIのオーバーレイ描画
    if (sceneManager_) {
        sceneManager_->DrawUI();
    }
    dxCommon_->EndGpuProfile("メイン描画");

    dxCommon_->PostDrawRenderTexture();

    // 2. ポストエフェクト・マルチパス (ブルーム生成)
    dxCommon_->StartGpuProfile("後処理");
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

    dxCommon_->EndGpuProfile("後処理");

    dxCommon_->EndGpuProfile("Total");

    prePostDrawTime_ = std::chrono::high_resolution_clock::now();
    dxCommon_->PostDraw();
#endif

    // CPU側のプロファイリング終了
    auto endDraw = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> drawDuration = endDraw - startDraw;
    drawTimeMs_ = drawDuration.count();

    // 履歴の保存 (Draw用)
    drawTimeHistory_[timeHistoryIndex_] = drawTimeMs_;

    // プロファイラに分割して送信
    ProfilerManager::GetInstance()->RecordCpuTime("描画 (合計)", drawTimeMs_);
    std::chrono::duration<float, std::milli> cmdDuration = prePostDrawTime_ - startDraw;
    float cmdTimeMs = cmdDuration.count();
    cpuCmdTimeMs_ = cmdTimeMs; // ステータスウィンドウ用に保存
    if (cmdTimeMs > 0.0f && cmdTimeMs < drawTimeMs_) {
        ProfilerManager::GetInstance()->RecordCpuTime("  命令発行", cmdTimeMs);
        ProfilerManager::GetInstance()->RecordCpuTime("  GPU待機", drawTimeMs_ - cmdTimeMs);
    }

    // ★ フレーム間隔の計測（前フレームから今フレームまでの実時間）
    auto preFixFPS = std::chrono::high_resolution_clock::now();
    // FPS固定処理
    dxCommon_->UpdateFixFPS();

    auto postFixFPS = std::chrono::high_resolution_clock::now();

    // フレーム全体の時間をプロファイラに送信
    static std::chrono::high_resolution_clock::time_point lastFrameStart;
    float frameDelta = std::chrono::duration<float, std::milli>(postFixFPS - lastFrameStart).count();
    lastFrameStart = postFixFPS;
    if (frameDelta > 0.0f && frameDelta < 200.0f) { // 初回や異常値を除外
        ProfilerManager::GetInstance()->RecordCpuTime("フレーム間隔", frameDelta);
    }

    // FixFPS の待ち時間だけを分離
    float fixFpsWait = std::chrono::duration<float, std::milli>(postFixFPS - preFixFPS).count();
    ProfilerManager::GetInstance()->RecordCpuTime("  FPS固定待ち", fixFpsWait);
}

void Game::SaveAllEditors() {
#ifdef USE_IMGUI
    DebugConsole::GetInstance()->AddLog("--- Auto Saving All Editor Data... ---");

    // ① Object3D (Scene) の保存
    //if (debugEditor_) {
    //    debugEditor_->SaveScene();
    //}

    // ② カメラの保存 (実装済みなら追加)
    // CameraEditor::GetInstance()->Save();

    // ③ ライティングの保存 (実装済みなら追加)
    // LightEditor::GetInstance()->Save();

    // ④ パーティクルやスプライトの保存 (実装済みなら追加)
    // if (spriteDebugEditor_) spriteDebugEditor_->Save();

    // ⑤ ゴーストディレクターのシナリオ保存
    // (※必要であればGhostDirector側に現在開いているシナリオを保存するSaveCurrent()のような関数を作って呼ぶ)

    DebugConsole::GetInstance()->AddLog("--- Save Complete! ---");
#endif
}