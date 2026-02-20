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
    postEffect_ = std::make_unique<PostEffect>();
    postEffect_->Initialize(dxCommon_);
    postEffectEditor_ = std::make_unique<PostEffectEditor>();
    postEffectEditor_->Initialize(postEffect_.get());
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
            uint32_t texHandle = postEffect_->GetSRVHandle();
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

                float gameResW = WinApp::kClientWidth;
                float gameResH = WinApp::kClientHeight;
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
            ImGui::MenuItem("ポストエフェクト", NULL, &showPostEffectEditor_);
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
    if (showPostEffectEditor_) {
        postEffectEditor_->DrawImGui();
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

    // ---------------------------------------------------------------
    // 1. シーン描画フェーズ
    //    描画先：dxCommon内の「レンダーテクスチャA」
    // ---------------------------------------------------------------
    dxCommon_->PreDrawRenderTexture(); // TextureA を RTV(描画先) にセット

    // ゲームの中身を描画
    if (sceneManager_) {
        sceneManager_->Draw();
    }

    // デバッグ表示（グリッドやコライダー枠など）
    if (debugEditor_) {
        debugEditor_->DrawDebug(dxCommon_->GetCommandList());
    }

    // TextureA への描画終了 -> SRV(画像) に変換
    dxCommon_->PostDrawRenderTexture();


    // ---------------------------------------------------------------
    // 2. ポストエフェクトフェーズ (バケツリレー)
    //    描画先：postEffect内の「レンダーテクスチャB」
    //    入力  ：さっき描いた「レンダーテクスチャA」
    // ---------------------------------------------------------------
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // 描画先を TextureB に切り替え & クリア
    postEffect_->PreDrawScene(commandList);

    // TextureA を元画像として渡し、板ポリゴンを描画 (コピー or 加工)
    uint32_t texA_Handle = dxCommon_->GetRenderTextureSrvHandle();
    postEffect_->Draw(commandList, texA_Handle);

    // TextureB への描画終了 -> SRV(画像) に変換
    // (PostEffectクラスにPostDrawを作っていないので、ここで手動バリアを張ります)
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = postEffect_->GetRenderTexture();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);


    // ---------------------------------------------------------------
    // 3. UI合成 & 画面表示フェーズ
    //    描画先：バックバッファ (本物の画面)
    // ---------------------------------------------------------------
    dxCommon_->PreDrawBackBuffer();

    // スプライトエディタの描画
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Draw();
    }

    // ImGuiを描画
    // ※ Game::Update内で ImGui::Image に渡しているのは TextureB (postEffect_->GetSRVHandle())
    ImGuiManager::GetInstance()->Draw();

    // 描画終了 (フリップ)
    dxCommon_->PostDraw();
    ImGuiManager::GetInstance()->EndFrame();

#else
    // =================================================================
    // パターンB: ゲームモード (Release)
    // =================================================================
    dxCommon_->PreDraw();

    if (sceneManager_) {
        sceneManager_->Draw();
    }

    dxCommon_->PostDraw();

#endif

    // FPS固定処理
    dxCommon_->UpdateFixFPS();
}