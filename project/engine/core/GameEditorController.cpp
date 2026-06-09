#define NOMINMAX
#include "GameEditorController.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "Camera.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "DebrisEffectEditor.h"
#include "DebrisEffectManager.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "DirectXCommon.h"
#include "EditorManager.h"
#include "EngineManualWindow.h"
#include "GhostDirector.h"
#include "GhostRecorder.h"
#include "GPUParticleEditor.h"
#include "GPUParticleManager.h"
#include "ImguiManager.h"
#include "InputManager.h"
#include "LightEditor.h"
#include "MeshEffectEditor.h"
#include "MeshEffectManager.h"
#include "ParticleEditor.h"
#include "PostEffect.h"
#include "PostEffectEditor.h"
#include "ProfilerManager.h"
#include "ProjectWindow.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteDebugEditor.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "TrailEmitterEditor.h"
#include "VFXSequencerEditor.h"
#include "WinApp.h"
#include "IconsFontAwesome5.h"
#include "ImGuizmo.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>

GameEditorController::GameEditorController() = default;
GameEditorController::~GameEditorController() = default;

void GameEditorController::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
	postEffectEditor_ = std::make_unique<PostEffectEditor>();
	postEffectEditor_->Initialize(PostEffect::GetInstance());

	spriteDebugEditor_ = std::make_unique<SpriteDebugEditor>();
	spriteDebugEditor_->Initialize(sceneManager, InputManager::GetInstance());

	ghostRecorder_ = std::make_unique<GhostRecorder>();
	ghostRecorder_->Initialize(sceneManager);

	debugEditor_ = std::make_unique<DebugEditor>();
	debugEditor_->Initialize(sceneManager, dxCommon);
	if (sceneManager) {
		sceneManager->SetDebugEditor(debugEditor_.get());
	}

	particleEditor_ = std::make_unique<ParticleEditor>();
	particleEditor_->Initialize(sceneManager);

	gpuParticleEditor_ = std::make_unique<GPUParticleEditor>();
	gpuParticleEditor_->Initialize();

	vfxSequencerEditor_ = std::make_unique<VFXSequencerEditor>();
	vfxSequencerEditor_->Initialize();

	meshEffectEditor_ = std::make_unique<MeshEffectEditor>();
	meshEffectEditor_->Initialize(sceneManager);

	debrisEffectEditor_ = std::make_unique<DebrisEffectEditor>();
	debrisEffectEditor_->Initialize(sceneManager);

	trailEmitterEditor_ = std::make_unique<TrailEmitterEditor>();
	trailEmitterEditor_->Initialize(sceneManager);

	LightEditor::GetInstance()->Initialize();
	DebugConsole::GetInstance()->Initialize();

	ghostDirector_ = std::make_unique<GhostDirector>();
	ghostDirector_->Initialize(sceneManager);

	engineManualWindow_ = std::make_unique<EngineManualWindow>();

	if (sceneManager) {
		if (BaseScene* currentScene = sceneManager->GetCurrentScene()) {
			currentScene->SetDebugEditor(debugEditor_.get());
		}
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
			debrisEffectEditor_.get(),
			trailEmitterEditor_.get());
	}
}

void GameEditorController::Finalize() {
	trailEmitterEditor_.reset();
	debrisEffectEditor_.reset();
	meshEffectEditor_.reset();
	vfxSequencerEditor_.reset();
	gpuParticleEditor_.reset();
	ghostDirector_.reset();
	ghostRecorder_.reset();
	particleEditor_.reset();
	spriteDebugEditor_.reset();
	debugEditor_.reset();
	postEffectEditor_.reset();
	engineManualWindow_.reset();
	DebugConsole::GetInstance()->Finalize();
}

void GameEditorController::BeginFrame() {
	ImGuiManager::GetInstance()->BeginFrame();
	ImGuizmo::BeginFrame();
	SetupDefaultDockspace();
}

void GameEditorController::SetupDefaultDockspace() {
	ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(
		0,
		ImGui::GetMainViewport(),
		ImGuiDockNodeFlags_PassthruCentralNode);

	if (dockspaceInitialized_) {
		return;
	}

	dockspaceInitialized_ = true;
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

	ImGuiID dockMainId = dockspaceId;
	ImGuiID dockLeftId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.22f, nullptr, &dockMainId);
	ImGuiID dockRightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
	ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.30f, nullptr, &dockMainId);
	ImGuiID dockBottomLeftId = ImGui::DockBuilderSplitNode(dockBottomId, ImGuiDir_Left, 0.60f, nullptr, &dockBottomId);
	ImGuiID dockBottomRightId = dockBottomId;

	ImGui::DockBuilderDockWindow("Hierarchy", dockLeftId);
	ImGui::DockBuilderDockWindow(ICON_FA_LIST_UL " Sprite Hierarchy", dockLeftId);
	ImGui::DockBuilderDockWindow("Inspector", dockRightId);
	ImGui::DockBuilderDockWindow(ICON_FA_INFO_CIRCLE " Sprite Inspector", dockRightId);
	ImGui::DockBuilderDockWindow("Project (Assets)", dockBottomLeftId);
	ImGui::DockBuilderDockWindow(ICON_FA_FOLDER_OPEN " Sprite Assets", dockBottomLeftId);
	ImGui::DockBuilderDockWindow("Debug Console", dockBottomRightId);
	ImGui::DockBuilderDockWindow("ステータス", dockBottomRightId);
	ImGui::DockBuilderDockWindow("Game View", dockMainId);
	ImGui::DockBuilderFinish(dockspaceId);
}

EditorFrameState GameEditorController::DrawGameView(SceneManager* sceneManager, bool isPlaying) {
	EditorFrameState frameState;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGuiWindowClass windowClass;
	windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
	ImGui::SetNextWindowClass(&windowClass);

	ImGui::Begin("Game View", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		ImVec2 displaySize = ImGui::GetContentRegionAvail();
		ImGui::SetCursorPos(ImVec2(0, 0));
		ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();

		if (displaySize.x > 0.0f && displaySize.y > 0.0f) {
			uint32_t texHandle = PostEffect::GetInstance()->GetSRVHandle(1);
			D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(texHandle);
			ImGui::Image((ImTextureID)gpuHandle.ptr, displaySize);

			GameViewArea area{
				imageScreenPos.x,
				imageScreenPos.y,
				displaySize.x,
				displaySize.y,
			};

			HandleGameViewDropTargets(sceneManager, area);

			bool isHovered = ImGui::IsItemHovered();
			ImVec2 mousePos = ImGui::GetIO().MousePos;

			if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
				camera->SetAspectRatio(area.width / area.height);
				camera->UpdateProjectionMatrix();
			}

			if (debugEditor_) {
				debugEditor_->SetGameViewRegion({ area.screenX, area.screenY }, { area.width, area.height });
				debugEditor_->SetGameViewMousePos({ mousePos.x - area.screenX, mousePos.y - area.screenY });
				debugEditor_->SetGameViewHovered(isHovered);
				debugEditor_->Update();
				if (!isPlaying && isHovered && !ImGuizmo::IsOver() && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
					debugEditor_->SetGameViewMousePos({ mousePos.x - area.screenX, mousePos.y - area.screenY });
					debugEditor_->OpenGameViewCreateContextMenu();
				}
				debugEditor_->DrawGameViewCreateContextMenu();
				frameState.gizmoBusy = ImGuizmo::IsUsing();
			}

			if (spriteDebugEditor_) {
				float localX = mousePos.x - area.screenX;
				float localY = mousePos.y - area.screenY;
				Vector2 spriteLocalPos = {
					localX * (static_cast<float>(WinApp::kClientWidth) / area.width),
					localY * (static_cast<float>(WinApp::kClientHeight) / area.height),
				};

				spriteDebugEditor_->Update(spriteLocalPos, isHovered);
				frameState.spriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
			}

			DrawGhostPreview(isPlaying, area);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();

	return frameState;
}

void GameEditorController::HandleGameViewDropTargets(SceneManager* sceneManager, const GameViewArea& area) {
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
		const char* droppedFilename = static_cast<const char*>(payload->Data);
		if (sceneManager && droppedFilename) {
			if (BaseScene* currentScene = sceneManager->GetCurrentScene()) {
				SpriteCommon* spriteCommon = currentScene->GetSpriteCommon();
				auto& sprites = currentScene->GetSprites();

				ImVec2 mousePos = ImGui::GetIO().MousePos;
				Vector2 dropPos = {
					(mousePos.x - area.screenX) * (static_cast<float>(WinApp::kClientWidth) / area.width),
					(mousePos.y - area.screenY) * (static_cast<float>(WinApp::kClientHeight) / area.height),
				};

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

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
		const char* droppedModelName = static_cast<const char*>(payload->Data);
		if (debugEditor_ && droppedModelName) {
			ImVec2 mousePos = ImGui::GetIO().MousePos;
			debugEditor_->SetGameViewMousePos({ mousePos.x - area.screenX, mousePos.y - area.screenY });
			debugEditor_->InstantiateModelAtCursor(droppedModelName);
		}
	}

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
		const char* droppedPresetName = static_cast<const char*>(payload->Data);
		if (debugEditor_ && droppedPresetName) {
			ImVec2 mousePos = ImGui::GetIO().MousePos;
			debugEditor_->SetGameViewMousePos({ mousePos.x - area.screenX, mousePos.y - area.screenY });
			debugEditor_->InstantiatePresetAtCursor(droppedPresetName);
		}
	}

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PARTICLE_ASSET")) {
		const char* droppedParticleName = static_cast<const char*>(payload->Data);
		if (debugEditor_ && droppedParticleName) {
			ImVec2 mousePos = ImGui::GetIO().MousePos;
			debugEditor_->SetGameViewMousePos({ mousePos.x - area.screenX, mousePos.y - area.screenY });
			debugEditor_->InstantiateParticleAtCursor(droppedParticleName);
		}
	}

	ImGui::EndDragDropTarget();
}

void GameEditorController::DrawGhostPreview(bool isPlaying, const GameViewArea& area) {
	if (isPlaying) {
		return;
	}

	Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
	if (!camera) {
		return;
	}

	if (ghostRecorder_ && EditorManager::GetInstance()->GetSelectedObject() == ghostRecorder_.get()) {
		ghostRecorder_->DrawPreview(
			camera->GetViewProjectionMatrix(),
			Vector2{ area.screenX, area.screenY },
			Vector2{ area.width, area.height });
	}

	if (ghostDirector_ && EditorManager::GetInstance()->GetSelectedObject() == ghostDirector_.get()) {
		ghostDirector_->DrawPreview(
			camera->GetViewProjectionMatrix(),
			Vector2{ area.screenX, area.screenY },
			Vector2{ area.width, area.height });
	}
}

void GameEditorController::DrawMainMenuBar(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName) {
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	if (isPlaying) {
		if (ImGui::Button(ICON_FA_STOP " 停止")) {
			isPlaying = false;
			if (sceneManager) {
				sceneManager->SetIsPlaying(false);
			}
		}
	} else {
		if (ImGui::Button(ICON_FA_PLAY " 再生")) {
			SaveAllEditors();
			if (sceneManager) {
				sceneManager->ChangeScene(currentSceneName);
			}
			MeshEffectManager::GetInstance()->Clear();
			isPlaying = true;
			if (sceneManager) {
				sceneManager->SetIsPlaying(true);
			}
			CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
		}
	}

	if (previousPlayingState_ != isPlaying && !isPlaying) {
		MeshEffectManager::GetInstance()->Clear();
		DebrisEffectManager::GetInstance()->Clear();
		if (sceneManager) {
			sceneManager->ChangeScene(currentSceneName);
		}
		CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
	}
	previousPlayingState_ = isPlaying;

	ImGui::Text(isPlaying ? " | 実行中" : " | 編集モード");

	if (ImGui::BeginMenu("表示")) {
		ImGui::MenuItem("Hierarchy / Inspector 表示", nullptr, &showDebugWindows_);
		ImGui::Separator();
		ImGui::MenuItem("デバッグログ", nullptr, &showDebugConsole_);
		ImGui::MenuItem("ステータス", nullptr, &showTimeController_);
		ImGui::MenuItem("ボスロジックデバッグ", nullptr, &showBossDebug_);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("シーン切り替え")) {
		const char* sceneNames[] = { "TITLE", "SELECT", "GAMEPLAY", "GAMEOVER", "GAMECLEAR", "PREVIEW", "TUTORIAL" };
		for (const char* sceneName : sceneNames) {
			if (ImGui::MenuItem(sceneName) && sceneManager) {
				sceneManager->ChangeScene(sceneName);
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("ヘルプ")) {
		if (ImGui::MenuItem(ICON_FA_BOOK " エンジン説明書") && engineManualWindow_) {
			engineManualWindow_->Open();
		}
		if (ImGui::MenuItem(ICON_FA_CHART_BAR " システムプロファイラ")) {
			ProfilerManager::GetInstance()->Open();
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void GameEditorController::UpdateTools(float deltaTime, bool isPlaying, float timeScale) {
	if (gpuParticleEditor_) {
		gpuParticleEditor_->Update(deltaTime);
	}
	if (vfxSequencerEditor_) {
		vfxSequencerEditor_->Update(deltaTime);
	}
	if (meshEffectEditor_) {
		meshEffectEditor_->Update(deltaTime);
	}
	if (debrisEffectEditor_) {
		debrisEffectEditor_->Update(deltaTime);
	}
	if (trailEmitterEditor_) {
		trailEmitterEditor_->Update(deltaTime);
	}

	if (isPlaying) {
		MeshEffectManager::GetInstance()->Update(deltaTime * timeScale);
		GPUParticleManager::GetInstance()->Update(deltaTime * timeScale);
	}
}

void GameEditorController::DrawToolWindows(
	float& timeScale,
	float sceneUpdateTimeMs,
	float cpuCmdTimeMs,
	float drawTimeMs,
	float* updateTimeHistory,
	float* drawTimeHistory,
	int timeHistoryIndex) {
	if (showDebugWindows_) {
		if (debugEditor_) {
			debugEditor_->DrawHierarchy();
		}

		EditorManager::GetInstance()->DrawInspector();

		if (spriteDebugEditor_) {
			spriteDebugEditor_->DrawHierarchyWindow();
			spriteDebugEditor_->DrawInspectorWindow();
			spriteDebugEditor_->DrawProjectWindow();
		}
	}

	if (showDebugConsole_) {
		DebugConsole::GetInstance()->DrawImGui();
	}
	if (showTimeController_) {
		DrawStatusWindow(timeScale, sceneUpdateTimeMs, cpuCmdTimeMs, drawTimeMs, updateTimeHistory, drawTimeHistory, timeHistoryIndex);
	}
	if (engineManualWindow_) {
		engineManualWindow_->Draw();
	}
	ProfilerManager::GetInstance()->DrawImGui();
}

void GameEditorController::DrawStatusWindow(
	float& timeScale,
	float sceneUpdateTimeMs,
	float cpuCmdTimeMs,
	float drawTimeMs,
	float* updateTimeHistory,
	float* drawTimeHistory,
	int timeHistoryIndex) {
	ImGui::Begin("ステータス", &showTimeController_);

	float fps = drawTimeMs > 0.001f ? 1000.0f / drawTimeMs : 0.0f;
	ImGui::Text("FPS: %.1f", fps);
	ImGui::SliderFloat("時間倍率", &timeScale, 0.0f, 2.0f);

	ImGui::Separator();
	ImGui::Text("--- CPU パフォーマンス ---");
	float cpuTotalWorkMs = sceneUpdateTimeMs + cpuCmdTimeMs;
	ImGui::Text("CPU 稼働合計: %.3f ms", cpuTotalWorkMs);
	ImGui::ProgressBar(cpuTotalWorkMs / 16.66f, ImVec2(0.0f, 0.0f));

	if (ImGui::TreeNode("詳細内訳 (CPU)")) {
		ImGui::Text("  シーン更新  : %.3f ms", sceneUpdateTimeMs);
		ImGui::Text("  描画命令発行: %.3f ms", cpuCmdTimeMs);
		ImGui::TreePop();
	}

	ImGui::PlotLines(
		"##UpdateGraph",
		updateTimeHistory,
		120,
		timeHistoryIndex,
		"CPU負荷推移 (ms)",
		0.0f,
		16.66f,
		ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));

	ImGui::Separator();
	ImGui::Text("--- GPU パフォーマンス ---");
	float gpuTotalMs = DirectXCommon::GetInstance()->GetGpuDrawTimeMs();
	ImGui::Text("GPU 稼働合計: %.3f ms", gpuTotalMs);
	ImGui::ProgressBar(gpuTotalMs / 16.66f, ImVec2(0.0f, 0.0f));

	float gpuWaitMs = drawTimeMs - cpuCmdTimeMs;
	ImGui::Text("CPU 待機時間: %.3f ms (VSync待ち含む)", std::max(gpuWaitMs, 0.0f));

	ImGui::PlotLines(
		"##DrawGraph",
		drawTimeHistory,
		120,
		timeHistoryIndex,
		"フレーム全体 (ms)",
		0.0f,
		16.66f,
		ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));

	ImGui::End();
}

void GameEditorController::CapturePendingThumbnails() {
	if (debugEditor_ && debugEditor_->GetProjectWindow()) {
		debugEditor_->GetProjectWindow()->CapturePendingThumbnails();
	}
}

void GameEditorController::DrawScenePreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
	if (debugEditor_) {
		debugEditor_->DrawPreview(pointLightResource, spotLightResource);
	}
}

void GameEditorController::DrawSceneDebug(ID3D12GraphicsCommandList* commandList) {
	if (debugEditor_) {
		debugEditor_->DrawDebug(commandList);
	}
	if (meshEffectEditor_ && EditorManager::GetInstance()->GetSelectedObject() == meshEffectEditor_.get()) {
		meshEffectEditor_->Draw();
	}
}

void GameEditorController::DrawBackBufferUi() {
	if (spriteDebugEditor_) {
		spriteDebugEditor_->Draw();
	}
	ImGuiManager::GetInstance()->Draw();
}

void GameEditorController::EndFrame() {
	ImGuiManager::GetInstance()->EndFrame();
}

void GameEditorController::ApplyCameraInputState(const EditorFrameState& frameState, bool isPlaying) {
	if (Camera* mainCamera = CameraManager::GetInstance()->GetActiveCamera()) {
		mainCamera->SetInputEnabled(isPlaying || !(frameState.spriteEditorBusy || frameState.gizmoBusy));
	}
}

void GameEditorController::ApplyCameraOverrides() {
	if (debugEditor_ && debugEditor_->GetEffectPreviewStage()) {
		debugEditor_->GetEffectPreviewStage()->ApplyCameraOverride();
	}
	if (debugEditor_ && debugEditor_->GetAnimationWorkbench()) {
		debugEditor_->GetAnimationWorkbench()->ApplyCameraOverride();
	}
}

void GameEditorController::SaveAllEditors() {
	DebugConsole::GetInstance()->AddLog("--- Auto Saving All Editor Data... ---");
	DebugConsole::GetInstance()->AddLog("--- Save Complete! ---");
}

#endif
