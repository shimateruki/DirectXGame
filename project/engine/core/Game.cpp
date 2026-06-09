#define NOMINMAX
#include "Game.h"

#include "CameraEditor.h"
#include "DebrisEffectManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "GameDataManager.h"
#include "InputManager.h"
#include "KeyConfig.h"
#include "LightManager.h"
#include "MeshEffectManager.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "PostEffect.h"
#include "ProfilerManager.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "SrvManager.h"
#include "StageManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "GPUParticleManager.h"
#include "engine/graphics/postprocess/Fade.h"

#include <Windows.h>
#include <chrono>

namespace {
constexpr float kDeltaTimeClamp = 0.1f;
constexpr float kFixedDeltaTime = 1.0f / 60.0f;
constexpr int kHistorySampleCount = 120;
}

void Game::Initialize() {
	InitializeEngineServices();
	InitializeScene();
	InitializePostProcess();
	InitializeEditorTools();
	ConfigureInitialPlayState();

	CameraEditor::GetInstance()->Initialize();
	dxCommon_->CreateRenderTexture();
	lastTime_ = std::chrono::high_resolution_clock::now();
}

void Game::InitializeEngineServices() {
	Framework::Initialize();

	ProfilerManager::GetInstance()->Initialize();
	TextureManager::GetInstance()->LoadAllTexture("Resources/sprite/");
	TextureManager::GetInstance()->LoadAllTexture("Resources/texture/PBR/");
	ModelManager::GetInstance()->LoadAllModels();
	StageManager::GetInstance()->Initialize();
	GameDataManager::GetInstance()->Initialize();

	sceneFactory_ = std::make_unique<SceneFactory>();
	sceneManager_ = std::make_unique<SceneManager>();
}

void Game::InitializeScene() {
	currentSceneName_ = ResolveStartSceneName();
	sceneManager_->Initialize(sceneFactory_.get(), currentSceneName_);
	ApplyInitialSceneOverrides();
}

std::string Game::ResolveStartSceneName() const {
	std::string startScene = "TITLE";

#ifdef USE_IMGUI
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
	if (!sceneManager_) {
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
}

void Game::InitializePostProcess() {
	PostEffect::GetInstance()->Initialize(dxCommon_);
	uint32_t lutHandle = TextureManager::GetInstance()->Load("Resources/sprite/particle.png");
	PostEffect::GetInstance()->SetLUTTexture(lutHandle);

	Fade::GetInstance()->Initialize();
	KeyConfig::GetInstance()->Initialize();
}

void Game::InitializeEditorTools() {
#ifdef USE_IMGUI
	editorController_ = std::make_unique<GameEditorController>();
	editorController_->Initialize(sceneManager_.get(), dxCommon_);
#endif
}

void Game::ConfigureInitialPlayState() {
#ifdef USE_IMGUI
	isPlaying_ = false;
	CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
#else
	isPlaying_ = true;
	WinApp::SetCursorVisibility(false);
	winApp_->SetCursorClipping(true);
	winApp_->SetCursorLocked(true);
	CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
#endif
}

void Game::Finalize() {
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

	Framework::Finalize();
}

void Game::Update() {
	InputManager::GetInstance()->Update();
	if (InputManager::GetInstance()->IsKeyTriggered(DIK_ESCAPE)) {
		PostQuitMessage(0);
		return;
	}

	float deltaTime = CalculateDeltaTime();

#ifdef USE_IMGUI
	UpdateEditorFrame(deltaTime);
#endif

	float finalDeltaTime = isPlaying_ ? deltaTime * timeScale_ : 0.0f;
	UpdateGameSystems(deltaTime, finalDeltaTime);
}

float Game::CalculateDeltaTime() {
	auto currentTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float> duration = currentTime - lastTime_;
	float deltaTime = duration.count() > kDeltaTimeClamp ? kFixedDeltaTime : duration.count();
	lastTime_ = currentTime;
	return deltaTime;
}

void Game::UpdateEditorFrame(float deltaTime) {
#ifdef USE_IMGUI
	if (!editorController_) {
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
	editorController_->ApplyCameraInputState(editorFrameState_, isPlaying_);
#else
	(void)deltaTime;
#endif
}

void Game::UpdateGameSystems(float deltaTime, float finalDeltaTime) {
	auto startUpdate = std::chrono::high_resolution_clock::now();

	{
		PROFILE_SCOPE("シーン");
		if (sceneManager_) {
			sceneManager_->Update(finalDeltaTime);
		}
	}
	{
		PROFILE_SCOPE("ライト");
		LightManager::GetInstance()->Update();
	}
	{
		PROFILE_SCOPE("パーティクル");
		GPUParticleManager::GetInstance()->Update(deltaTime);
	}
	if (sceneManager_) {
		if (BaseScene* currentScene = sceneManager_->GetCurrentScene()) {
			DebrisEffectManager::GetInstance()->Initialize(currentScene->GetObject3dCommon());
		}
	}
	DebrisEffectManager::GetInstance()->Update(deltaTime);
	{
		PROFILE_SCOPE("フェード");
		Fade::GetInstance()->Update(deltaTime);
	}

	PostEffect::GetInstance()->GetParams()->time += deltaTime;

	if (sceneManager_) {
		sceneManager_->SetIsPlaying(isPlaying_);
	}

#ifdef USE_IMGUI
	if (editorController_) {
		editorController_->ApplyCameraOverrides();
	}
#endif

	RecordUpdateProfile(startUpdate);
}

void Game::RecordUpdateProfile(const std::chrono::high_resolution_clock::time_point& startUpdate) {
	auto endUpdate = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float, std::milli> updateDuration = endUpdate - startUpdate;

	sceneUpdateTimeMs_ = updateDuration.count();
	updateTimeHistory_[timeHistoryIndex_] = sceneUpdateTimeMs_;
	timeHistoryIndex_ = (timeHistoryIndex_ + 1) % kHistorySampleCount;

	ProfilerManager::GetInstance()->RecordCpuTime("更新処理", sceneUpdateTimeMs_);
}

void Game::Draw() {
	PostEffect* postEffect = PostEffect::GetInstance();
	dxCommon_->ReadAllGpuProfiles();

	auto startDraw = std::chrono::high_resolution_clock::now();

#ifdef USE_IMGUI
	DrawEditorFrame(postEffect);
#else
	DrawRuntimeFrame(postEffect);
#endif

	RecordDrawProfile(startDraw);
	RecordFixedFpsProfile();
}

void Game::DrawEditorFrame(PostEffect* postEffect) {
#ifdef USE_IMGUI
	DrawSceneToRenderTexture(true);
	ApplyPostEffectPipeline(postEffect, true);

	dxCommon_->StartGpuProfile("エディタUI");
	dxCommon_->PreDrawBackBuffer();
	if (editorController_) {
		editorController_->DrawBackBufferUi();
	}
	dxCommon_->EndGpuProfile("エディタUI");

	dxCommon_->EndGpuProfile("Total");
	prePostDrawTime_ = std::chrono::high_resolution_clock::now();
	dxCommon_->PostDraw();

	if (editorController_) {
		editorController_->EndFrame();
	}
#else
	(void)postEffect;
#endif
}

void Game::DrawRuntimeFrame(PostEffect* postEffect) {
	DrawSceneToRenderTexture(false);
	ApplyPostEffectPipeline(postEffect, false);

	dxCommon_->EndGpuProfile("Total");
	prePostDrawTime_ = std::chrono::high_resolution_clock::now();
	dxCommon_->PostDraw();
}

void Game::DrawSceneToRenderTexture(bool editorMode) {
	dxCommon_->PreDrawRenderTexture();

#ifdef USE_IMGUI
	if (editorMode && editorController_) {
		editorController_->CapturePendingThumbnails();
	}
#else
	(void)editorMode;
#endif

	dxCommon_->StartGpuProfile("Total");
	dxCommon_->PreDrawShadow();
	SRVManager::GetInstance()->SetDescriptorHeaps(dxCommon_->GetCommandList());

	dxCommon_->StartGpuProfile("影描画");
	if (sceneManager_) {
		sceneManager_->DrawShadow();
	}
	dxCommon_->EndGpuProfile("影描画");
	dxCommon_->PostDrawShadow();

	dxCommon_->StartGpuProfile("メイン描画");

#ifdef USE_IMGUI
	if (editorMode) {
		dxCommon_->StartGpuProfile("  3Dシーン");
		ID3D12Resource* pointLight = LightManager::GetInstance()->GetPointLightResource();
		ID3D12Resource* spotLight = LightManager::GetInstance()->GetSpotLightResource();
		if (sceneManager_) {
			sceneManager_->Draw();
		}
		if (editorController_) {
			editorController_->DrawScenePreview(pointLight, spotLight);
		}
		DebrisEffectManager::GetInstance()->Draw(pointLight, spotLight);
		dxCommon_->EndGpuProfile("  3Dシーン");

		dxCommon_->StartGpuProfile("  ゲームUI");
		if (sceneManager_) {
			sceneManager_->DrawUI();
		}
		dxCommon_->EndGpuProfile("  ゲームUI");

		dxCommon_->StartGpuProfile("  エフェクト");
		MeshEffectManager::GetInstance()->Draw(pointLight, spotLight);
		dxCommon_->EndGpuProfile("  エフェクト");

		dxCommon_->StartGpuProfile("  デバッグ");
		if (editorController_) {
			editorController_->DrawSceneDebug(dxCommon_->GetCommandList());
		}
		dxCommon_->EndGpuProfile("  デバッグ");
	} else
#endif
	{
		if (sceneManager_) {
			sceneManager_->Draw();
			ID3D12Resource* pointLight = LightManager::GetInstance()->GetPointLightResource();
			ID3D12Resource* spotLight = LightManager::GetInstance()->GetSpotLightResource();
			DebrisEffectManager::GetInstance()->Draw(pointLight, spotLight);
			sceneManager_->DrawUI();
		}
	}

	dxCommon_->EndGpuProfile("メイン描画");
	dxCommon_->PostDrawRenderTexture();
}

void Game::ApplyPostEffectPipeline(PostEffect* postEffect, bool outputForEditorGameView) {
	dxCommon_->StartGpuProfile("後処理");
	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
	uint32_t renderTextureHandle = dxCommon_->GetRenderTextureSrvHandle();

	postEffect->PreDrawScene(commandList, 2);
	postEffect->Draw(commandList, renderTextureHandle, 2);
	postEffect->TransitionToSRV(commandList, 2);

	postEffect->PreDrawScene(commandList, 3);
	postEffect->Draw(commandList, postEffect->GetSRVHandle(2), 3);
	postEffect->TransitionToSRV(commandList, 3);

	postEffect->PreDrawScene(commandList, 4);
	postEffect->Draw(commandList, postEffect->GetSRVHandle(3), 3);
	postEffect->TransitionToSRV(commandList, 4);

	postEffect->PreDrawScene(commandList, 5);
	postEffect->Draw(commandList, postEffect->GetSRVHandle(4), 3);
	postEffect->TransitionToSRV(commandList, 5);

	postEffect->PreDrawScene(commandList, 0);
	postEffect->Draw(commandList, renderTextureHandle, 0);

	postEffect->PreDrawScene(commandList, 0, false);
	postEffect->Draw(commandList, postEffect->GetSRVHandle(2), 4);
	postEffect->Draw(commandList, postEffect->GetSRVHandle(3), 4);
	postEffect->Draw(commandList, postEffect->GetSRVHandle(4), 4);
	postEffect->Draw(commandList, postEffect->GetSRVHandle(5), 4);
	postEffect->TransitionToSRV(commandList, 0);

	if (outputForEditorGameView) {
		postEffect->PreDrawScene(commandList, 1);
		postEffect->Draw(commandList, postEffect->GetSRVHandle(0), 1);
		postEffect->TransitionToSRV(commandList, 1);
	} else {
		dxCommon_->PreDraw();
		postEffect->Draw(commandList, postEffect->GetSRVHandle(0), 1);
	}

	dxCommon_->EndGpuProfile("後処理");
}

void Game::RecordDrawProfile(const std::chrono::high_resolution_clock::time_point& startDraw) {
	auto endDraw = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float, std::milli> drawDuration = endDraw - startDraw;

	drawTimeMs_ = drawDuration.count();
	drawTimeHistory_[timeHistoryIndex_] = drawTimeMs_;

	ProfilerManager::GetInstance()->RecordCpuTime("描画 (合計)", drawTimeMs_);

	std::chrono::duration<float, std::milli> cmdDuration = prePostDrawTime_ - startDraw;
	float cmdTimeMs = cmdDuration.count();
	cpuCmdTimeMs_ = cmdTimeMs;

	if (cmdTimeMs > 0.0f && cmdTimeMs < drawTimeMs_) {
		ProfilerManager::GetInstance()->RecordCpuTime("  命令発行", cmdTimeMs);
		ProfilerManager::GetInstance()->RecordCpuTime("  GPU待機", drawTimeMs_ - cmdTimeMs);
	}
}

void Game::RecordFixedFpsProfile() {
	auto preFixFPS = std::chrono::high_resolution_clock::now();
	dxCommon_->UpdateFixFPS();
	auto postFixFPS = std::chrono::high_resolution_clock::now();

	static std::chrono::high_resolution_clock::time_point lastFrameStart;
	float frameDelta = std::chrono::duration<float, std::milli>(postFixFPS - lastFrameStart).count();
	lastFrameStart = postFixFPS;
	if (frameDelta > 0.0f && frameDelta < 200.0f) {
		ProfilerManager::GetInstance()->RecordCpuTime("フレーム間隔", frameDelta);
	}

	float fixFpsWait = std::chrono::duration<float, std::milli>(postFixFPS - preFixFPS).count();
	ProfilerManager::GetInstance()->RecordCpuTime("  FPS固定待ち", fixFpsWait);
}
