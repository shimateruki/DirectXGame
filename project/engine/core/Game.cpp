#ifdef USE_IMGUI
#include "engine/editor/preview/SelectedActorSession.h"
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#define NOMINMAX
#include "Game.h"

#include "engine/editor/graphics/camera/CameraEditor.h"
#include "AudioPlayer.h"
#include "CameraManager.h"
#include "DebrisEffectManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "EditorCommandRegistry.h"
#include "game/system/settings/GameAudioSettings.h"
#include "game/system/progress/GameDataManager.h"
#include "game/system/settings/GameSettingsManager.h"
#include "InputManager.h"
#include "KeyConfig.h"
#include "LightManager.h"
#include "MeshEffectManager.h"
#include "ModelManager.h"
#include "Object3D.h"
#include "PostEffect.h"
#include "ProfilerManager.h"
#include "RenderStats.h"
#include "SceneFactory.h"
#include "game/scene/RegisterGameScenes.h"
#ifdef USE_IMGUI
#include "game/editor/RegisterGameEditorFactories.h"
#include "game/editor/BuiltInCreatePresetRegistry.h"
#include "game/actor/gimmick/combat/GimmickFireCannon.h"
#include "game/system/combat/BulletManager.h"
#endif
#include "SceneManager.h"
#include "engine/system/scene/LoadingProfiler.h"
#include "SrvManager.h"
#include "game/system/progress/StageManager.h"
#include "TextureManager.h"
#include "VfxSequencer.h"
#include "WinApp.h"
#include "GpuParticleManager.h"
#include "engine/graphics/postprocess/Fade.h"
#include "engine/graphics/core/ColorSpace.h"
#include "engine/graphics/3d/FoliageInteraction.h"
#include "json.hpp"

#include <Windows.h>
#include <array>
#include <chrono>
#include <fstream>
#ifdef USE_IMGUI
#include "tests/SceneTransitionRuntimeProbe.h"
#endif

namespace {
constexpr float kDeltaTimeClamp = 0.1f;
constexpr float kFixedDeltaTime = 1.0f / 60.0f;
constexpr int kMaxFixedStepsPerFrame = 4;
constexpr int kHistorySampleCount = 120;

// Post Effect設定から線形色空間の選択を読み、読込失敗時は現在値を維持します。
void LoadColorWorkflowSetting() {
	std::ifstream file("Resources/json/post_effect.json");

	if (!file.is_open()) {
		return;
	}

	try {
		nlohmann::json settings;
		file >> settings;

		if (settings.contains("linearWorkflowEnabled")) {
			engine::ColorSpace::WorkflowSettings::GetInstance().SetLinearWorkflowEnabled(
				settings["linearWorkflowEnabled"].get<bool>());
		}
	}
	catch (...) {
		// 設定ファイルが壊れている場合は従来方式を維持する。
	}
}

#ifdef USE_IMGUI
// 現在シーンの描画用カメラ情報を再反映し、プレビュー描画後の状態ずれを防ぐ。
void RefreshSceneRenderCameraData(SceneManager* sceneManager) {
	// プレビューのカメラ更新が通常シーンの描画定数へ残らないよう再反映する。
	if (!sceneManager) {
		return;
	}

	BaseScene* currentScene = sceneManager->GetCurrentScene();

	if (!currentScene) {
		return;
	}

	currentScene->RefreshRenderCameraData();
}
#endif
}

void Game::Initialize() {
	// エンジン基盤、シーン、ポストエフェクト、エディタ、初期プレイ状態を順番に準備する。
	StartupProfileScope startupScope("Game::Initialize");
	{
		StartupProfileScope phase("engine services");
		InitializeEngineServices();
	}

	{
		StartupProfileScope phase("post effects");
		InitializePostProcess();
	}

	{
		StartupProfileScope phase("first scene");
		InitializeScene();
	}

	{
		StartupProfileScope phase("editor tools");
		InitializeEditorTools();
	}

	// 基盤・シーン・各ツールが揃ってから、編集か通常再生かの入力状態を決める。
	ConfigureInitialPlayState();

	CameraEditor::GetInstance()->Initialize();
	GetDirectX()->CreateRenderTexture();
	lastTime_ = std::chrono::high_resolution_clock::now();
	LogStartupProfileCheckpoint("Game::Initialize complete");
}

void Game::InitializeEngineServices() {
	// ゲーム全体で使うマネージャやアセットを初期化し、シーン生成の準備を行う。
	{
		StartupProfileScope phase("Framework::Initialize call");
		engine::Framework::Initialize();
	}

	ProfilerManager::GetInstance()->Initialize();
	StageManager::GetInstance()->Initialize();
	GameDataManager::GetInstance()->Initialize();
	GameSettingsManager::GetInstance()->Initialize();
	GameAudioSettings::GetInstance()->Initialize();

	auto gameSceneFactory = std::make_unique<SceneFactory>();
	RegisterGameScenes(*gameSceneFactory);
#ifdef USE_IMGUI
	RegisterGameEditorFactories();
#endif
	sceneFactory_ = std::move(gameSceneFactory);
	sceneManager_ = std::make_unique<SceneManager>();
	sceneManager_->SetRenderCommonResources(GetRenderCommonResources());
}

void Game::InitializeScene() {
	// 開始シーン名を決め、SceneManagerへ初期シーンを生成させる。
	currentSceneName_ = ResolveStartSceneName();
	initialSceneOverridesPending_ = true;
	sceneManager_->Initialize(sceneFactory_.get(), currentSceneName_);
	ApplyInitialSceneOverrides();
}

std::string Game::ResolveStartSceneName() const {
	// ビルド設定やエディタの最終シーン情報から、起動時に開くシーン名を決める。
#ifdef USE_IMGUI
	if (SceneTransitionRuntimeProbe::Enabled())
		return "TITLE";
#endif
	std::string startScene = "TITLE";

#if defined(USE_IMGUI) && !defined(DD)
	if (sceneManager_) {
		std::string lastScene = sceneManager_->LoadLastSceneName();

		if (!lastScene.empty()) {
			startScene = lastScene;
		}
	}
#endif

	return startScene;
}

void Game::ApplyInitialSceneOverrides() {
	// 起動直後に必要なシーン内オブジェクトの初期上書きを適用する。
	if (!initialSceneOverridesPending_ || !sceneManager_ || sceneManager_->IsTransitioning()) {
		return;
	}

	BaseScene* currentScene = sceneManager_->GetCurrentScene();

	if (!currentScene) {
		return;
	}

	for (auto& object : currentScene->GetObjects()) {
		if (object && object->GetName() == "Skydome") {
			object->SetSelectedLighting(0);
#ifdef USE_IMGUI
			DebugConsole::GetInstance()->AddLog("Skydome settings have been overwritten.");
#endif
			break;
		}
	}

	initialSceneOverridesPending_ = false;
}

void Game::InitializePostProcess() {
	// ポストエフェクト、LUT、フェード、キー設定を初期化する。
	LoadColorWorkflowSetting();
	engine::LightManager::GetInstance()->ApplySceneClearColor();
	// 色空間とクリア色を先に読み、後処理の作成時点から同じ描画設定を使う。
	PostEffect::GetInstance()->Initialize(GetDirectX());
	PostEffect::GetInstance()->SetLUTTexturePath(
		"Resources/texture/lut/soft_adventure_lut.png");

	Fade::GetInstance()->Initialize();
#ifndef USE_IMGUI
	PostEffect::GetInstance()->ResetToNeutral();
#endif
	KeyConfig::GetInstance()->Initialize();
}

void Game::InitializeEditorTools() {
	// USE_IMGUI時に、ゲームビューや各種エディタを束ねるコントローラを初期化する。
#ifdef USE_IMGUI
	editorController_ = std::make_unique<GameEditorController>();
	editorController_->Initialize(sceneManager_.get(), GetDirectX());

	if (sceneManager_ && sceneManager_->GetCurrentScene()) {
		BuiltInCreatePresetRegistry::EnsureRegistered(sceneManager_->GetCurrentScene()->GetObject3DCommon());
	}
#endif
}

void Game::ConfigureInitialPlayState() {
	// エディタ起動か通常実行かに応じて、再生状態とカメラ入力モードを設定する。
#ifdef USE_IMGUI
	isPlaying_ = false;
	CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
#else
	isPlaying_ = true;
	WinApp::SetCursorVisibility(false);
	GetWindow()->SetCursorClipping(true);
	GetWindow()->SetCursorLocked(true);
	CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
#endif
}

void Game::Finalize() {
	// シーンとエディタを終了し、エンジン基盤を解放する。
	// シーンを閉じる前に保留中の進行データを確定し、基盤側の終了へ渡す。
	GameDataManager::GetInstance()->FlushPendingSave();

	if (sceneManager_) {
		sceneManager_->Finalize();
	}

	DebrisEffectManager::GetInstance()->Clear();

#ifdef USE_IMGUI
	if (editorController_) {
		editorController_->Finalize();
		editorController_.reset();
	}
#endif

	engine::Framework::Finalize();
}

void Game::Update() {
	// 入力、終了要求、リサイズ、エディタ、ゲームシステムを1フレーム分更新する。
	engine::InputManager::GetInstance()->Update();

	if (engine::InputManager::GetInstance()->IsKeyTriggered(DIK_ESCAPE)) {
		WinApp::RequestClose();
	}

	// ウィンドウとEscapeからの要求を一本化し、ImGuiが使える場合は保存確認をエディターに渡す。
	if (WinApp::ConsumeCloseRequest()) {
#ifdef USE_IMGUI
		if (editorController_) {
			editorController_->RequestExit();
		}
		else {
			WinApp::CloseNow();
		}
#else
		WinApp::CloseNow();
#endif
	}

	if (GetDirectX()) {
		GetDirectX()->ProcessPendingResize();
	}

	// 音声回収・振動・ヒットストップの時計は実時間で進めてから、Scene用の時間に換算する。
	AudioEventSystem::GetInstance()->Update();
	float deltaTime = CalculateDeltaTime();
	engine::InputManager::GetInstance()->UpdateRumble(deltaTime);
	VfxSequencer::UpdateFeedbackRuntime(deltaTime);
	const bool sceneTransitioning = sceneManager_ && sceneManager_->IsTransitioning();

	if (!sceneTransitioning) {
		GpuParticleManager::GetInstance()->BeginFrame();
		MeshEffectManager::GetInstance()->BeginFrame();
	}

	if (isPlaying_) {
		GpuParticleManager::GetInstance()->SetTimeScale(1.0f);
	}

#ifdef USE_IMGUI
	UpdateEditorFrame(deltaTime);
	if (AttackLifecycleRuntimeProbe::WantsPlay(sceneManager_.get()) || RaftRuntimeProbe::WantsPlay(sceneManager_.get())) {
		// 検証起動ではScene側も同じフレームから再生へ揃える。
		// Editorの停止状態だけが残ると、物理検証中にRecorderが停止し続ける。
		isPlaying_ = true;
		sceneManager_->SetIsPlaying(true);
	}
#endif

#ifdef USE_IMGUI
    if (GoalPresentationRuntimeProbe::Enabled() || StarCoinRuntimeProbe::Enabled() || EnemyProjectileRuntimeProbe::Enabled()) { isPlaying_ = true; }
    if (GateEntryRuntimeProbe::Enabled()) {
        isPlaying_ = GateEntryRuntimeProbe::WantsPlay();
        sceneManager_->SetIsPlaying(isPlaying_);
        CameraEditor::GetInstance()->SetMode(isPlaying_ ? CameraEditor::Mode::Game : CameraEditor::Mode::Editor);
    }
#endif
#ifdef USE_IMGUI
    if (TutorialExitGuideRuntimeProbe::Enabled()) {
        isPlaying_ = TutorialExitGuideRuntimeProbe::WantsPlay();
        sceneManager_->SetIsPlaying(isPlaying_);
    }
#endif
	const float feedbackTimeScale = VfxSequencer::GetGameplayTimeScale();
	// 実時間からゲーム時間を作る。停止中は0秒、再生中は編集側の倍率とヒットストップ側の倍率を掛ける。
	float finalDeltaTime = isPlaying_ ? deltaTime * timeScale_ * feedbackTimeScale : 0.0f;
#ifdef USE_IMGUI
	if (AttackLifecycleRuntimeProbe::WantsPlay(sceneManager_.get()) || RaftRuntimeProbe::WantsPlay(sceneManager_.get()))
		finalDeltaTime = 1.0f / 60.0f;
	if (RaftRuntimeProbe::WantsPlay(sceneManager_.get()) && GetTickCount64() < RaftRuntimeProbe::pauseUntil)
		finalDeltaTime = 0.0f;
	if (editorController_ && editorController_->IsSimulationPaused())
		finalDeltaTime = 0.0f;
#endif
	UpdateGameSystems(deltaTime, finalDeltaTime);

	// コインの連続取得を短時間まとめ、シーン切替中は保留分を確定する。
	GameDataManager::GetInstance()->TickAutosave(deltaTime);

	if (sceneManager_ && sceneManager_->IsTransitioning()) {
		GameDataManager::GetInstance()->FlushPendingSave();
	}

	ApplyInitialSceneOverrides();
}

float Game::CalculateDeltaTime() {
	// 前フレームからの経過時間を求め、極端に大きい値は固定値へ丸める。
	auto currentTime = std::chrono::high_resolution_clock::now();
	// 秒単位へ変換し、ウィンドウ移動などで一時的に止まった後の大きな時間は固定刻みへ置き換える。
	std::chrono::duration<float> duration = currentTime - lastTime_;
	float deltaTime = duration.count() > kDeltaTimeClamp ? kFixedDeltaTime : duration.count();
	lastTime_ = currentTime;
	return deltaTime;
}

void Game::UpdateEditorFrame(float deltaTime) {
	// エディタUI、ゲームビュー、ツールウィンドウ、カメラ入力状態を更新する。
#ifdef USE_IMGUI
	if (!editorController_) {
		return;
	}

	if (engine::InputManager::GetInstance()->IsKeyTriggered(DIK_F10)) {
		editorController_->ExecuteEditorCommand(EditorCommandId::ViewPortfolio);
	}

	// 撮影表示では通常のメニューバーを組み立てず、ツール更新と入力状態だけを維持する。
	if (editorController_->IsPortfolioCaptureMode()) {
		editorController_->UpdateTools(deltaTime, isPlaying_, timeScale_);
		editorFrameState_ = {};
		editorController_->ApplyCameraInputState(editorFrameState_, isPlaying_);
		return;
	}

	editorController_->BeginFrame();
	editorFrameState_ = editorController_->DrawGameView(sceneManager_.get(), isPlaying_);

	if (sceneManager_ && !sceneManager_->GetCurrentSceneName().empty()) {
		currentSceneName_ = sceneManager_->GetCurrentSceneName();
	}

	editorController_->DrawMainMenuBar(sceneManager_.get(), isPlaying_, currentSceneName_);
	editorController_->UpdateTools(deltaTime, isPlaying_, timeScale_);
	editorController_->DrawToolWindows(
		timeScale_,
		sceneUpdateTimeMs_,
		cpuCmdTimeMs_,
		drawTimeMs_,
		updateTimeHistory_,
		drawTimeHistory_,
		timeHistoryIndex_);
	if (!sceneManager_ || !sceneManager_->IsTransitioning()) {
		editorController_->ApplyCameraInputState(editorFrameState_, isPlaying_);
	}
	else {
		editorFrameState_ = {};
	}
#else
	(void)deltaTime;
#endif
}

void Game::UpdateGameSystems(float deltaTime, float finalDeltaTime) {
	// シーン、ライト、エフェクト、フェード、ポストエフェクト時刻を更新する。
	auto startUpdate = std::chrono::high_resolution_clock::now();
	bool simulationPaused = false;
	const bool sceneTransitioning = sceneManager_ && sceneManager_->IsTransitioning();
#ifdef USE_IMGUI
	if (editorController_) {
		simulationPaused = editorController_->IsSimulationPaused();
	}
#endif

	// 固定刻みの物理更新を先に消化し、そのあと現在のSceneへフレーム分の更新を渡す。
	RunFixedUpdates(finalDeltaTime, simulationPaused, sceneTransitioning);

	{
		PROFILE_SCOPE("シーン");

		if (sceneManager_) {
			// 遷移画面は停止やヒットストップから独立した時間で進むため、元の経過秒を使う。
			const float sceneDeltaTime = sceneManager_->IsTransitioning() ? deltaTime : finalDeltaTime;

			if (!simulationPaused || sceneManager_->IsTransitioning()) {
				sceneManager_->Update(sceneDeltaTime);
			}
		}
	}
#ifdef USE_IMGUI
    // 編集中は水と砲台プレビューだけ実時間で進め、他のゲーム挙動は停止させます。
    if (!isPlaying_ && !simulationPaused && !sceneTransitioning && sceneManager_ &&
        !(SelectedActorSession::Current() && SelectedActorSession::Current()->IsActive())) {
        if (BaseScene* scene = sceneManager_->GetCurrentScene()) {
            for (auto& object : scene->GetObjects()) {
                if (auto* cannon = finalDeltaTime == 0.0f && object && !object->IsReplayRemoved()
                    ? dynamic_cast<GimmickFireCannon*>(object.get()) : nullptr) {
                    cannon->Update(deltaTime);
                }

                if (object && object->GetMaterialType() == 8 && object->GetMeshRenderer()) {
                    object->GetMeshRenderer()->AdvanceWaterAnimation(deltaTime);
                }

                if (object && (object->GetMaterialType() == 30 || object->GetMaterialType() == 31)
                    && object->GetMeshRenderer()) {
                    object->GetMeshRenderer()->AdvanceMaterialTime(deltaTime);
                }
            }
            if (finalDeltaTime == 0.0f) {
                BulletManager::GetInstance()->UpdatePreview(deltaTime);
                if (BulletManager::GetInstance()->HasPreviewBullets()) {
                    // シーン側の時間0更新後でも、このプレビューだけは粒子の寿命を進めます。
                    GpuParticleManager::GetInstance()->UpdateEditorPreviewStep(deltaTime);
                }
            }
        }
    }
#endif
#ifdef USE_IMGUI
    if (editorController_) editorController_->UpdateActorTools(deltaTime);
#endif
    // シーン変更時は草への接触履歴を消し、現プレイヤーだけ反映する。
    {
        static const BaseScene* foliageScene = nullptr;
        BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;

        if (scene != foliageScene || sceneTransitioning) {
            FoliageInteraction::Reset();
            foliageScene = scene;
        }

        bool found = false;

        if (scene && !sceneTransitioning) {
            for (auto& object : scene->GetObjects()) {
                if (object && object->GetClassName() == "Player") {
                    const auto pos = object->GetWorldPosition();
                    FoliageInteraction::Update(pos.x, pos.y, pos.z,
                        simulationPaused ? 0.0f : (isPlaying_ ? finalDeltaTime : deltaTime));
                    found = true;
                    break;
                }
            }
        }

        if (!found)
            FoliageInteraction::Reset();
    }

	if (!sceneTransitioning) {
		PROFILE_SCOPE("ライト");
		engine::LightManager::GetInstance()->Update();
	}

	if (!sceneTransitioning) {
		PROFILE_SCOPE("パーティクル");
		const float effectDeltaTime = isPlaying_ && !simulationPaused ? finalDeltaTime : 0.0f;
		VfxSequencer::UpdateOneShots(effectDeltaTime);
		GpuParticleManager::GetInstance()->Update(effectDeltaTime);
#ifndef USE_IMGUI
		MeshEffectManager::GetInstance()->Update(effectDeltaTime);
#endif
	}

	if (sceneManager_ && !sceneManager_->IsTransitioning()) {
		if (BaseScene* currentScene = sceneManager_->GetCurrentScene()) {
			DebrisEffectManager::GetInstance()->Initialize(currentScene->GetObject3DCommon());
		}

		#ifdef USE_IMGUI
        if (!(SelectedActorSession::Current() && SelectedActorSession::Current()->IsActive()))
#endif
        DebrisEffectManager::GetInstance()->Update(simulationPaused ? 0.0f : deltaTime);
	}

	if (!sceneManager_ || !sceneManager_->IsSceneInitializingAsync()) {
		PROFILE_SCOPE("フェード");
		Fade::GetInstance()->Update(simulationPaused ? 0.0f : deltaTime);
	}

	// 後処理の見た目の時計は実時間で進めるが、シミュレーション停止時は保持する。
	PostEffect::GetInstance()->GetParams()->time += simulationPaused ? 0.0f : deltaTime;

	if (sceneManager_) {
		sceneManager_->SetIsPlaying(isPlaying_);
	}

#ifdef USE_IMGUI
	if (editorController_ && (!sceneManager_ || !sceneManager_->IsTransitioning())) {
		editorController_->ApplyCameraOverrides();
	}
#endif

	RecordUpdateProfile(startUpdate);
}

void Game::RunFixedUpdates(float finalDeltaTime, bool simulationPaused, bool sceneTransitioning) {
	// 停止・切替中の余った時間は破棄し、再開したシーンへ前のシーンの固定更新を持ち越さない。
	if (!sceneManager_ || simulationPaused || sceneTransitioning || finalDeltaTime <= 0.0f) {
		fixedUpdateAccumulator_ = 0.0f;
		return;
	}

	BaseScene* currentScene = sceneManager_->GetCurrentScene();

	if (!currentScene) {
		fixedUpdateAccumulator_ = 0.0f;
		return;
	}

	// ゲーム時間を秒で貯め、固定刻みを一回分確保できるたびに物理更新へ渡す。
	fixedUpdateAccumulator_ += (std::min)(finalDeltaTime, kDeltaTimeClamp);
	int stepCount = 0;

	while (fixedUpdateAccumulator_ >= kFixedDeltaTime && stepCount < kMaxFixedStepsPerFrame) {
		currentScene->FixedUpdate(kFixedDeltaTime);
		fixedUpdateAccumulator_ -= kFixedDeltaTime;
		++stepCount;
	}

	// 長い停止後に古い時間を持ち越さず、次フレームの処理急増を防ぎます。
	if (stepCount == kMaxFixedStepsPerFrame) {
		fixedUpdateAccumulator_ = (std::min)(fixedUpdateAccumulator_, kFixedDeltaTime);
	}
}

void Game::RecordUpdateProfile(const std::chrono::high_resolution_clock::time_point& startUpdate) {
	// 更新処理にかかったCPU時間を履歴とProfilerManagerへ記録する。
	auto endUpdate = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float, std::milli> updateDuration = endUpdate - startUpdate;

	sceneUpdateTimeMs_ = updateDuration.count();
	updateTimeHistory_[timeHistoryIndex_] = sceneUpdateTimeMs_;
	timeHistoryIndex_ = (timeHistoryIndex_ + 1) % kHistorySampleCount;

	ProfilerManager::GetInstance()->RecordCpuTime("更新処理", sceneUpdateTimeMs_);
}

void Game::Draw() {
	// GPUプロファイルを読み戻し、エディタまたは通常実行の描画フローを実行する。
	PostEffect* postEffect = PostEffect::GetInstance();
	// 描画指示を記録する前に、GPUから回収できる計測結果を更新する。
	GetDirectX()->ReadAllGpuProfiles();
#ifdef USE_IMGUI
	// 環境変数が指定されたときだけ描画区間の実測値をCSVに記録します。
	static bool gpuTraceInitialized = false;
	static std::ofstream gpuTrace;
	static unsigned gpuTraceFrame = 0;

	if (!gpuTraceInitialized) {
		gpuTraceInitialized = true;
		char tracePath[1024] = {};
		const DWORD pathLength = GetEnvironmentVariableA("CG2_GPU_PROFILE_CSV", tracePath, sizeof(tracePath));

		if (pathLength > 0 && pathLength < sizeof(tracePath)) {
			gpuTrace.open(tracePath, std::ios::out | std::ios::trunc);

			if (gpuTrace) {
				gpuTrace << "frame,total_ms,thumbnail_ms,thumbnail_cpu_ms,shadow_ms,scene_ms,game_ui_ms,debug_ms,post_ms,editor_ui_ms\n";
			}
		}
	}

	if (gpuTrace && ++gpuTraceFrame % 30 == 0) {
		const auto* profiler = ProfilerManager::GetInstance();
		gpuTrace << gpuTraceFrame << ','
			<< profiler->GetLatestGpuTime("Total") << ','
			<< profiler->GetLatestGpuTime("プリセット撮影") << ','
			<< profiler->GetLatestCpuTime("プリセット撮影 CPU") << ','
			<< profiler->GetLatestGpuTime("影描画") << ','
			<< profiler->GetLatestGpuTime("  3Dシーン") << ','
			<< profiler->GetLatestGpuTime("  ゲームUI") << ','
			<< profiler->GetLatestGpuTime("  デバッグ") << ','
			<< profiler->GetLatestGpuTime("後処理") << ','
			<< profiler->GetLatestGpuTime("エディタUI") << '\n';
		gpuTrace.flush();
	}
#endif
	engine::RenderStats* renderStats = engine::RenderStats::GetInstance();
	renderStats->BeginFrame();

	if (!sceneManager_ || !sceneManager_->IsTransitioning()) {
		renderStats->SetActiveLightCounts(
			engine::LightManager::GetInstance()->GetActivePointLightCount(),
			engine::LightManager::GetInstance()->GetActiveSpotLightCount());
	}
	else {
		renderStats->SetActiveLightCounts(0, 0);
	}

	auto startDraw = std::chrono::high_resolution_clock::now();

#ifdef USE_IMGUI
    if (TutorialExitGuideRuntimeProbe::Enabled())
        TutorialExitGuideRuntimeProbe::PrepareCaptureCamera(sceneManager_.get());
	DrawEditorFrame(postEffect);
#else
	DrawRuntimeFrame(postEffect);
#endif

	renderStats->EndFrame();
	RecordDrawProfile(startDraw);
	RecordFixedFpsProfile();
#ifdef USE_IMGUI
	SceneTransitionRuntimeProbe::AfterDraw(sceneManager_.get(), editorController_ ? editorController_->GetDebugEditor() : nullptr);
#endif
}

void Game::DrawEditorFrame(PostEffect* postEffect) {
	// エディタ用にシーン、ポストエフェクト、カメラプレビュー、ImGuiを描画する。
#ifdef USE_IMGUI
	if (editorController_ && editorController_->IsPortfolioCaptureMode()) {
		DrawRuntimeFrame(postEffect);
		return;
	}

	// 先にゲーム画面を別のテクスチャで完成させ、バックバッファではその画像とEditor UIを合成する。
	DrawSceneToRenderTexture(true);
	ApplyPostEffectPipeline(postEffect, true);

	if (!sceneManager_ || !sceneManager_->IsTransitioning()) {
		DrawCameraEditorPreview(postEffect);
	}

	GetDirectX()->StartGpuProfile("エディタUI");
	GetDirectX()->PreDrawBackBuffer();

	if (editorController_) {
		editorController_->DrawBackBufferUi();
	}

	GetDirectX()->EndGpuProfile("エディタUI");

	GetDirectX()->EndGpuProfile("Total");
	prePostDrawTime_ = std::chrono::high_resolution_clock::now();
	GetDirectX()->PostDraw();

	if (editorController_) {
		editorController_->ExportCapturedHudPortraits();
		editorController_->EndFrame();
	}
#else
	(void)postEffect;
#endif
}

void Game::DrawRuntimeFrame(PostEffect* postEffect) {
	// 通常実行用にシーンとポストエフェクトを描画し、バックバッファへ表示する。
	DrawSceneToRenderTexture(false);
	ApplyPostEffectPipeline(postEffect, false);

	GetDirectX()->EndGpuProfile("Total");
	prePostDrawTime_ = std::chrono::high_resolution_clock::now();
	GetDirectX()->PostDraw();
}

void Game::DrawSceneToRenderTexture(bool editorMode) {
	// 影、3Dシーン、UI、フェードをレンダーテクスチャへまとめて描画する。
	GetDirectX()->PreDrawRenderTexture();

	// サムネイル撮影も同じフレームのGPU仕事なので、Total計測に含めます。
	GetDirectX()->StartGpuProfile("Total");

#ifdef USE_IMGUI
	if (editorMode && editorController_ && (!sceneManager_ || !sceneManager_->IsTransitioning())) {
		GetDirectX()->StartGpuProfile("プリセット撮影");
		const auto thumbnailStart = std::chrono::high_resolution_clock::now();
		editorController_->CapturePendingThumbnails();
		const auto thumbnailEnd = std::chrono::high_resolution_clock::now();
		ProfilerManager::GetInstance()->RecordCpuTime("プリセット撮影 CPU",
			std::chrono::duration<float, std::milli>(thumbnailEnd - thumbnailStart).count());
		GetDirectX()->EndGpuProfile("プリセット撮影");
	}
#else
	(void)editorMode;
#endif
	GetDirectX()->PreDrawShadow();
	SrvManager::GetInstance()->SetDescriptorHeaps(GetDirectX()->GetCommandList());

	GetDirectX()->StartGpuProfile("影描画");
	{
		engine::ScopedRenderPass renderPass(engine::RenderPass::Shadow);

		if (sceneManager_) {
			sceneManager_->DrawShadow();
		}
	}

	GetDirectX()->EndGpuProfile("影描画");
	// 影マップの描画を終了し、続く3D描画では結果を参照する側へ切り替える。
	GetDirectX()->PostDrawShadow();

	GetDirectX()->StartGpuProfile("メイン描画");

#ifdef USE_IMGUI
	if (editorMode) {
		// Editorではゲーム画面・UI・操作ハンドルを別パスに分け、表示先のテクスチャを保つ。
		GetDirectX()->StartGpuProfile("  3Dシーン");
		{
			engine::ScopedRenderPass renderPass(engine::RenderPass::MainScene);
			const bool sceneTransitioning = sceneManager_ && sceneManager_->IsTransitioning();
			ID3D12Resource* pointLight = sceneTransitioning ? nullptr : engine::LightManager::GetInstance()->GetPointLightResource();
			ID3D12Resource* spotLight = sceneTransitioning ? nullptr : engine::LightManager::GetInstance()->GetSpotLightResource();

			if (sceneManager_) {
				sceneManager_->Draw();
			}

			if (editorController_ && !sceneTransitioning) {
				editorController_->DrawScenePreview(pointLight, spotLight);
			}

			if (!sceneManager_ || !sceneManager_->IsTransitioning()) {
				DebrisEffectManager::GetInstance()->Draw(pointLight, spotLight);

				if (MeshEffectManager::GetInstance()->RequiresSceneColorCopy()) {
					GetDirectX()->UpdateGrabTexture();
				}

				MeshEffectManager::GetInstance()->Draw(pointLight, spotLight);
			}
		}

		GetDirectX()->EndGpuProfile("  3Dシーン");

		GetDirectX()->StartGpuProfile("  ゲームUI");
		{
			engine::ScopedRenderPass renderPass(engine::RenderPass::GameUi);

			if (sceneManager_) {
				sceneManager_->DrawUi();
			}
		}

		GetDirectX()->EndGpuProfile("  ゲームUI");

		GetDirectX()->StartGpuProfile("  デバッグ");
		{
			engine::ScopedRenderPass renderPass(engine::RenderPass::EditorOverlay);

			if (editorController_ && (!sceneManager_ || !sceneManager_->IsTransitioning())) {
				editorController_->DrawSceneDebug(GetDirectX()->GetCommandList());
			}
		}

		GetDirectX()->EndGpuProfile("  デバッグ");
	} else
#endif
	{
		if (sceneManager_) {
			// 配布実行時も描画順は3D→UI→Fade。Editor専用の重ね描きだけ省く。
			{
				engine::ScopedRenderPass renderPass(engine::RenderPass::MainScene);
				sceneManager_->Draw();
				ID3D12Resource* pointLight = nullptr;
				ID3D12Resource* spotLight = nullptr;

				if (!sceneManager_->IsTransitioning()) {
					pointLight = engine::LightManager::GetInstance()->GetPointLightResource();
					spotLight = engine::LightManager::GetInstance()->GetSpotLightResource();
					DebrisEffectManager::GetInstance()->Draw(pointLight, spotLight);

					if (MeshEffectManager::GetInstance()->RequiresSceneColorCopy()) {
						GetDirectX()->UpdateGrabTexture();
					}

					MeshEffectManager::GetInstance()->Draw(pointLight, spotLight);
				}
			}

			{
				engine::ScopedRenderPass renderPass(engine::RenderPass::GameUi);
				sceneManager_->DrawUi();
			}
		}
	}

	GetDirectX()->StartGpuProfile("Fade");

	if (!sceneManager_ || !sceneManager_->IsSceneInitializingAsync()) {
		engine::ScopedRenderPass renderPass(engine::RenderPass::GameUi);
		Fade::GetInstance()->Draw();
	}

	GetDirectX()->EndGpuProfile("Fade");

	GetDirectX()->EndGpuProfile("メイン描画");
	GetDirectX()->PostDrawRenderTexture();
}

void Game::DrawCameraEditorPreview(PostEffect* postEffect) {
	// カメラエディタ用の別視点プレビューを専用テクスチャへ描画する。
#ifdef USE_IMGUI
	if (!postEffect || !sceneManager_) {
		return;
	}

	CameraEditor* cameraEditor = CameraEditor::GetInstance();

	if (!cameraEditor) {
		return;
	}

	const bool renderEditorPreview = cameraEditor->ShouldRenderCameraPreview();
	const bool renderSceneCameraPreview = cameraEditor->ShouldRenderSceneCameraPreview();

	if (!renderEditorPreview && !renderSceneCameraPreview) {
		return;
	}

	ID3D12GraphicsCommandList* commandList = GetDirectX()->GetCommandList();
	engine::ScopedRenderPass renderPass(engine::RenderPass::CameraPreview);
	GetDirectX()->SetCameraPreviewRendering(true);

	// 専用テクスチャへ描画し、SRVに戻してからエディタへ描画完了を通知する。
	const auto drawPreview = [&](engine::Camera* camera, int textureIndex, int viewIndex) {
		if (!camera) {
			return; // カメラを準備できない場合は既存のプレビューを保持する。
		}

		postEffect->PreDrawSceneWithDepth(commandList, textureIndex, true);
		if (BaseScene* currentScene = sceneManager_->GetCurrentScene()) {
			currentScene->DrawCameraPreview(camera, viewIndex);
		}
		postEffect->TransitionToSrv(commandList, textureIndex);
		cameraEditor->NotifyCameraPreviewRendered(viewIndex == 1);
	};

	if (renderEditorPreview) {
		drawPreview(cameraEditor->PreparePreviewCamera(16.0f / 9.0f), PostEffect::kCameraPreviewTextureIndex, 0);
	}
	if (renderSceneCameraPreview) {
		drawPreview(cameraEditor->PrepareCinematicPreviewCamera(16.0f / 9.0f), PostEffect::kCinematicCameraPreviewTextureIndex, 1);
	}

	// 二つのプレビューで使った描画フラグを戻し、後続のバックバッファ描画へ持ち越さない。
	GetDirectX()->SetCameraPreviewRendering(false);
#else
	(void)postEffect;
#endif
}

void Game::ApplyPostEffectPipeline(PostEffect* postEffect, bool outputForEditorGameView) {
	// レンダーテクスチャへ複数段のポストエフェクトを適用し、最終出力先へ渡す。
	GetDirectX()->StartGpuProfile("後処理");
	engine::ScopedRenderPass renderPass(engine::RenderPass::PostProcess);
	ID3D12GraphicsCommandList* commandList = GetDirectX()->GetCommandList();
	const uint32_t renderTextureHandle = GetDirectX()->GetRenderTextureSrvHandle();

	auto drawFinalComposite = [&](uint32_t sourceHandle) {
		// EditorはGame Viewの参照用テクスチャ、配布実行はウィンドウのバックバッファを最終出力にする。
		if (outputForEditorGameView) {
			postEffect->PreDrawScene(commandList, 1);
			postEffect->Draw(commandList, sourceHandle, PostEffect::CompositeHdrPipeline);
			postEffect->TransitionToSrv(commandList, 1);
		} else {
			GetDirectX()->PreDraw();
			// 黒帯は後処理を掛けず、完成したゲーム映像だけを等比で配置します。
			const auto area = WinApp::CalculateGameViewRect(WinApp::GetWindowWidth(), WinApp::GetWindowHeight());
			D3D12_VIEWPORT viewport{area.x, area.y, area.width, area.height, 0.0f, 1.0f};
			commandList->RSSetViewports(1, &viewport);
			postEffect->Draw(commandList, sourceHandle, PostEffect::CompositeBackBufferPipeline);
		}
	};

	// Bloomが無効、または強度が実質0なら、抽出・縮小・加算をすべて省略する。
	// Tone Mappingや色調補正などは最終Compositeで引き続き適用される。
	if (!postEffect->IsBloomActive()) {
		drawFinalComposite(renderTextureHandle);
		GetDirectX()->EndGpuProfile("後処理");
		return;
	}

	constexpr std::array<int, 4> kBloomTextureIndices = { 2, 3, 4, 5 };
	// 有効な段数分だけ、直前に縮小した明部テクスチャから次のレベルを作る。
	const int bloomLevelCount = postEffect->GetBloomLevelCount();

	postEffect->PreDrawScene(commandList, kBloomTextureIndices[0]);
	postEffect->Draw(commandList, renderTextureHandle, PostEffect::ExtractPipeline);
	postEffect->TransitionToSrv(commandList, kBloomTextureIndices[0]);

	for (int level = 1; level < bloomLevelCount; ++level) {
		const int sourceIndex = kBloomTextureIndices[level - 1];
		const int targetIndex = kBloomTextureIndices[level];
		postEffect->PreDrawScene(commandList, targetIndex);
		postEffect->Draw(commandList, postEffect->GetSrvHandle(sourceIndex), PostEffect::DownsamplePipeline);
		postEffect->TransitionToSrv(commandList, targetIndex);
	}

	postEffect->PreDrawScene(commandList, 0);
	postEffect->Draw(commandList, renderTextureHandle, PostEffect::CopyPipeline);

	// シーン色を入れた合成先をクリアせず、各段の明部を加算するために描画先として開き直す。
	postEffect->PreDrawScene(commandList, 0, false);

	for (int level = 0; level < bloomLevelCount; ++level) {
		postEffect->Draw(
			commandList,
			postEffect->GetSrvHandle(kBloomTextureIndices[level]),
			PostEffect::AddPipeline);
	}

	postEffect->TransitionToSrv(commandList, 0);

	// 明部を加算した画像を入力にして、トーンマッピングと最終出力を行う。
	drawFinalComposite(postEffect->GetSrvHandle(0));

	GetDirectX()->EndGpuProfile("後処理");
}

void Game::RecordDrawProfile(const std::chrono::high_resolution_clock::time_point& startDraw) {
	// 描画全体、CPUコマンド発行、GPU待ち時間をProfilerManagerへ記録する。
	auto endDraw = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float, std::milli> drawDuration = endDraw - startDraw;

	drawTimeMs_ = drawDuration.count();
	drawTimeHistory_[timeHistoryIndex_] = drawTimeMs_;

	ProfilerManager::GetInstance()->RecordCpuTime("描画 (合計)", drawTimeMs_);

	std::chrono::duration<float, std::milli> cmdDuration = prePostDrawTime_ - startDraw;
	// PostDrawの直前までを命令発行とし、Presentや後続UI処理を含む残り時間を「GPU待機」欄へ記録する。
	float cmdTimeMs = cmdDuration.count();
	cpuCmdTimeMs_ = cmdTimeMs;

	if (cmdTimeMs > 0.0f && cmdTimeMs < drawTimeMs_) {
		ProfilerManager::GetInstance()->RecordCpuTime("  命令発行", cmdTimeMs);
		ProfilerManager::GetInstance()->RecordCpuTime("  GPU待機", drawTimeMs_ - cmdTimeMs);
	}
}

void Game::RecordFixedFpsProfile() {
	// 固定FPS待機時間とフレーム間隔を計測し、負荷表示に反映する。
	auto preFixFps = std::chrono::high_resolution_clock::now();
	GetDirectX()->UpdateFixedFps();
	auto postFixFps = std::chrono::high_resolution_clock::now();

	static std::chrono::high_resolution_clock::time_point lastFrameStart;
	float frameDelta = std::chrono::duration<float, std::milli>(postFixFps - lastFrameStart).count();
	lastFrameStart = postFixFps;

	if (frameDelta > 0.0f && frameDelta < 200.0f) {
		ProfilerManager::GetInstance()->RecordCpuTime("フレーム間隔", frameDelta);
	}

	float fixFpsWait = std::chrono::duration<float, std::milli>(postFixFps - preFixFps).count();
	ProfilerManager::GetInstance()->RecordCpuTime("  FPS固定待ち", fixFpsWait);
}
