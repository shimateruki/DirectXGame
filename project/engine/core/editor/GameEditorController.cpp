#include "engine/core/editor/GameEditorController.Internal.h"

#include "game/ui/SettingsMenuOverlay.h"
#ifdef USE_IMGUI
using namespace GameEditorControllerImplementation;

#ifdef NDEBUG
namespace {
// USE_IMGUIかつNDEBUGのDevelopmentだけに存在する、保存しないデバッグスイッチ。
bool debugHugeHealth = false;
Player* DebugHealthPlayer(SceneManager* manager) {
    auto* scene = manager ? manager->GetCurrentScene() : nullptr;
    return scene ? dynamic_cast<Player*>(scene->GetPlayer()) : nullptr;
}
void DisableDebugHugeHealth(SceneManager* manager) {
    if (debugHugeHealth) {
        if (auto* player = DebugHealthPlayer(manager); player && player->param_.has_value()) {
            player->param_->hp = (std::min)(player->param_->hp, player->GetMaxHp());
        }
    }
    debugHugeHealth = false;
}
}
#endif

GameEditorController::GameEditorController() = default;
// GameEditorControllerの既定デストラクタ。明示的な解放はFinalizeで行う。
GameEditorController::~GameEditorController() = default;

bool GameEditorController::CanEditScene() const {
	if (!debugEditor_ || !sceneManager_ || sceneManager_->IsTransitioning()) {
		return false;
	}
	const bool isPlaying = activePlayState_ ? *activePlayState_ : sceneManager_->IsPlaying();
	return !isPlaying;
}

bool GameEditorController::CanEditSelectedObject() const {
	return CanEditScene() && debugEditor_->GetSelectedObject() && !debugEditor_->GetIsPathEditMode();
}

bool GameEditorController::ExecuteEditorCommand(const char* commandId) {
#ifdef NDEBUG
    // Scene保存にデバッグHPを持ち込まない。
    if (commandId && std::string(commandId) == EditorCommandId::SceneSave)
        DisableDebugHugeHealth(sceneManager_);
#endif
	return commandId && EditorCommandRegistry::GetInstance()->Execute(commandId);
}

bool GameEditorController::DrawEditorCommandMenuItem(
	const char* commandId,
	const char* labelOverride,
	bool selected) {
	const EditorCommand* command = EditorCommandRegistry::GetInstance()->Find(commandId ? commandId : "");
	if (!command) {
		return false;
	}
	const char* label = labelOverride ? labelOverride : command->displayName.c_str();
	const char* shortcut = command->shortcut.empty() ? nullptr : command->shortcut.c_str();
	if (!ImGui::MenuItem(label, shortcut, selected, EditorCommandRegistry::GetInstance()->CanExecute(command->id))) {
		return false;
	}
	return ExecuteEditorCommand(command->id.c_str());
}

void GameEditorController::StopPlay() {
	if (!activePlayState_ || !*activePlayState_) {
		return;
	}

#ifdef NDEBUG
    DisableDebugHugeHealth(sceneManager_);
#endif
	if (playModeChangeTracker_) {
		playModeChangeTracker_->CaptureRuntimeChanges();
	}
	const bool restored = replayDebugger_ && replayDebugger_->RestorePlayInEditorSnapshot();
	*activePlayState_ = false;
	if (sceneManager_) {
		sceneManager_->SetIsPlaying(false);
	}

	// 停止時に進行途中のシーケンスを残すと、次フレームに0秒更新されて
	// 消去後のメッシュエフェクトを再生成し、編集画面へ張り付いたように残ります。
	VFXSequencer::ClearOneShots();
	MeshEffectManager::GetInstance()->Clear();
	GPUParticleManager::GetInstance()->ResetSimulation();
	DebrisEffectManager::GetInstance()->Clear();
	ClearSceneBoundEditorState();
	if (playModeChangeTracker_) {
		playModeChangeTracker_->OnSceneRestored(restored);
	}
	CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);

	if (restored) {
		DebugConsole::GetInstance()->AddLog("Play In Editor: 編集開始時の状態へ復元しました。");
		playOriginSceneName_.clear();
		playOriginSceneLoadContext_ = {};
		playOriginSceneGeneration_ = 0;
		pendingPlayOriginReload_ = false;
		return;
	}

	// Play中にSceneが切り替わった場合、元インスタンスは破棄済みなので再ロードで戻します。
	pendingPlayOriginReload_ = true;
	ReloadPlayOriginScene();
}

void GameEditorController::ReloadPlayOriginScene() {
	if (!pendingPlayOriginReload_ || !sceneManager_ || sceneManager_->IsTransitioning()) {
		return;
	}

	bool reloadRequested = false;
	if (playOriginSceneLoadContext_.IsSceneAsset()) {
		reloadRequested = sceneManager_->OpenSceneAsset(playOriginSceneLoadContext_);
	}
	else if (!playOriginSceneName_.empty() && sceneManager_->IsSceneRegistered(playOriginSceneName_)) {
		sceneManager_->ChangeScene(playOriginSceneName_);
		reloadRequested = true;
	}

	if (reloadRequested) {
		DebugConsole::GetInstance()->AddLog(
			"Play In Editor: Play中にSceneが切り替わったため、開始Sceneを再ロードします。");
	}
	else {
		DebugConsole::GetInstance()->AddLog(
			"Play In Editor Warning: 開始Sceneを復元できませんでした。");
	}

	pendingPlayOriginReload_ = false;
	playOriginSceneName_.clear();
	playOriginSceneLoadContext_ = {};
	playOriginSceneGeneration_ = 0;
}

void GameEditorController::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
	sceneManager_ = sceneManager;
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
	ghostDirector_->Initialize(sceneManager, debugEditor_.get());

	engineManualWindow_ = std::make_unique<EngineManualWindow>();
	replayDebugger_ = std::make_unique<ReplayDebugger>();
	replayDebugger_->Initialize(sceneManager, debugEditor_.get());

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


    sceneWorkspace_ = std::make_unique<SceneWorkspace>();
    sceneWorkspace_->Initialize(sceneManager_, debugEditor_.get());
    debugEditor_->SetSceneWorkspace(sceneWorkspace_.get());

    editorQuickSearch_ = std::make_unique<EditorQuickSearch>();
    editorQuickSearch_->Initialize(
        sceneManager_,
        debugEditor_.get(),
        [this]() { showDebugWindows_ = true; });

    assetReferenceExplorer_ = std::make_unique<AssetReferenceExplorer>();
    assetReferenceExplorer_->Initialize(debugEditor_.get());
    playModeChangeTracker_ = std::make_unique<PlayModeChangeTracker>();
    playModeChangeTracker_->Initialize(sceneManager_, debugEditor_.get());
    debugEditor_->SetPlayFromPositionCallback(
        [this](const Vector3& position, const std::string& label) {
            RequestPlayFromPosition(position, label);
        });

	RegisterEditorCommands();
    RegisterEditorTools();
}
// 各種エディタツールを逆順に解放し、DebugConsoleを終了する。

void GameEditorController::Finalize() {
    EditorToolSettings::Get().Clear();
	EditorCommandRegistry::GetInstance()->UnregisterOwner(this);
	activePlayState_ = nullptr;
	activeSceneName_.clear();
    if (editorQuickSearch_) {
        editorQuickSearch_->Finalize();
    }
    editorQuickSearch_.reset();
    if (debugEditor_) {
        debugEditor_->SetSceneWorkspace(nullptr);
    }
    if (sceneWorkspace_) {
        sceneWorkspace_->Finalize();
    }
    sceneWorkspace_.reset();
    if (debugEditor_) debugEditor_->SetPlayFromPositionCallback({});
    if (playModeChangeTracker_) playModeChangeTracker_->Finalize();
    playModeChangeTracker_.reset();
    if (assetReferenceExplorer_) assetReferenceExplorer_->Finalize();
    assetReferenceExplorer_.reset();
	sceneManager_ = nullptr;
	if (replayDebugger_) {
		replayDebugger_->Finalize();
	}
	replayDebugger_.reset();
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
// ImGuiとImGuizmoのフレームを開始し、既定ドックスペースを準備する。

void GameEditorController::BeginFrame() {
	ImGuiManager::GetInstance()->BeginFrame();
	ImGuizmo::BeginFrame();
	CameraEditor::GetInstance()->BeginPreviewUiFrame();
    EditorToolSettings::Get().HandleShortcuts(!SettingsMenuOverlay::IsAnyActive() && sceneManager_ && !sceneManager_->IsPlaying() && !sceneManager_->IsTransitioning());
    if (sceneWorkspace_) {
        sceneWorkspace_->Update();
        if (!SettingsMenuOverlay::IsAnyActive()) sceneWorkspace_->HandleHotkeys();
    }
    if (editorQuickSearch_) {
        if (!SettingsMenuOverlay::IsAnyActive()) editorQuickSearch_->HandleShortcut();
    }
	IEditable* selectedObject = EditorManager::GetInstance()->GetSelectedObject();
	const bool wantsEnemyAttackTimeline =
		!showReplayDebugger_ &&
		debugEditor_ &&
		selectedObject == debugEditor_->GetEnemyAttackPreviewWindow();
	const bool wantsEffectPreviewTimeline =
		!showReplayDebugger_ &&
		!wantsEnemyAttackTimeline &&
		(selectedObject == particleEditor_.get() ||
		 selectedObject == gpuParticleEditor_.get() ||
		 selectedObject == vfxSequencerEditor_.get() ||
		 selectedObject == meshEffectEditor_.get() ||
		 selectedObject == debrisEffectEditor_.get() ||
		 selectedObject == trailEmitterEditor_.get());
	EffectPreviewStage* effectPreviewStage = EffectPreviewStage::GetInstance();
    const bool wantsAnimationTimeline = !showReplayDebugger_ && debugEditor_ &&
        selectedObject == debugEditor_->GetAnimationWorkbench();
    const bool wantsCinematicTimeline = !showReplayDebugger_ && ghostDirector_ &&
        selectedObject == ghostDirector_.get();
    if (showCinematicTimeline_ && !wantsCinematicTimeline) ghostDirector_->EndStudio();
    if (showAnimationTimeline_ && !wantsAnimationTimeline && debugEditor_)
        debugEditor_->GetAnimationWorkbench()->StopPreview();
	if (wantsEffectPreviewTimeline && !showEffectPreviewTimeline_) {
		effectPreviewStage->EnableForToolPreview();
	} else if (!wantsEffectPreviewTimeline && showEffectPreviewTimeline_) {
		effectPreviewStage->ReturnToScene();
	}
    if (wantsAnimationTimeline && !showAnimationTimeline_)
        debugEditor_->GetAnimationWorkbench()->StartPreview();
	if (showEnemyAttackTimeline_ != wantsEnemyAttackTimeline ||
		showEffectPreviewTimeline_ != wantsEffectPreviewTimeline ||
        showAnimationTimeline_ != wantsAnimationTimeline ||
        showCinematicTimeline_ != wantsCinematicTimeline) {
        showAnimationTimeline_ = wantsAnimationTimeline;
        showCinematicTimeline_ = wantsCinematicTimeline;
		showEnemyAttackTimeline_ = wantsEnemyAttackTimeline;
		showEffectPreviewTimeline_ = wantsEffectPreviewTimeline;
		dockspaceInitialized_ = false;
	}
	SetupDefaultDockspace();
    ProcessPendingWorkspaceLayout();
}
// ポートフォリオ撮影用に、エディタUIを隠すモードのON/OFFを切り替える。

void GameEditorController::SetPortfolioCaptureMode(bool enabled) {
	if (portfolioCaptureMode_ == enabled) {
		return;
	}

	portfolioCaptureMode_ = enabled;
	DebugConsole::GetInstance()->AddLog(
		portfolioCaptureMode_
			? "ポートフォリオ撮影モード: ON (F10でエディタ表示に戻ります)"
			: "ポートフォリオ撮影モード: OFF (エディタ表示を再開しました)");
}

void GameEditorController::SetReplayDebuggerVisible(bool visible) {
	if (showReplayDebugger_ == visible) {
		return;
	}

	showReplayDebugger_ = visible;
	// Replay表示中は下段を1枚の固定パネルに組み直し、閉じたら通常配置へ戻します。
	dockspaceInitialized_ = false;
}
// 初回起動時にHierarchy、Inspector、Project、GameViewなどの既定ドック配置を作る。

void GameEditorController::DrawMainMenuBar(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName) {
	sceneManager_ = sceneManager;
	activePlayState_ = &isPlaying;
	activeSceneName_ = currentSceneName;
	ReloadPlayOriginScene();

	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	if (isPlaying) {
		ImGui::BeginDisabled(!EditorCommandRegistry::GetInstance()->CanExecute(EditorCommandId::Stop));
		if (ImGui::Button(ICON_FA_STOP " 停止")) {
			ExecuteEditorCommand(EditorCommandId::Stop);
		}
		ImGui::EndDisabled();
	} else {
		ImGui::BeginDisabled(!EditorCommandRegistry::GetInstance()->CanExecute(EditorCommandId::Play));
		if (ImGui::Button(ICON_FA_PLAY " 再生")) {
			ExecuteEditorCommand(EditorCommandId::Play);
		}
		ImGui::EndDisabled();
	}
	ImGui::Text(isPlaying ? " | 実行中" : " | 編集モード");
	if (isPlaying && replayDebugger_) {
		ImGui::SameLine();
		const bool canControlReplay = replayDebugger_->HasFrames();
		const bool replayPaused = replayDebugger_->ShouldFreezeSimulation();
		ImGui::BeginDisabled(!canControlReplay);
		if (ImGui::Button(replayPaused ? ICON_FA_PLAY " リプレイ再開" : ICON_FA_PAUSE " 一時停止")) {
			ExecuteEditorCommand(EditorCommandId::ReplayPauseResume);
		}
		ImGui::EndDisabled();
	}

	const ImGuiIO& menuIo = ImGui::GetIO();
	if (!menuIo.WantTextInput && menuIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
		ExecuteEditorCommand(EditorCommandId::SceneSave);
	}
	if (!menuIo.WantTextInput && menuIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
		ExecuteEditorCommand(isPlaying ? EditorCommandId::Stop : EditorCommandId::Play);
	}

	if (ImGui::BeginMenu(ICON_FA_FILE " ファイル")) {
		DrawEditorCommandMenuItem(EditorCommandId::SceneNew, ICON_FA_PLUS " 新規Scene Asset");
		DrawEditorCommandMenuItem(EditorCommandId::SceneSave, ICON_FA_SAVE " Active Sceneを保存");
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Scene Asset管理を表示")) {
			showDebugWindows_ = true;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("編集")) {
        DrawEditorCommandMenuItem(EditorCommandId::ViewQuickSearch, ICON_FA_SEARCH " 統合コマンドパレット");
        ImGui::Separator();
		const std::string undoLabel = EditorTransactionManager::GetInstance()->CanUndo()
			? "元に戻す: " + EditorTransactionManager::GetInstance()->GetUndoLabel()
			: "元に戻す";
		const std::string redoLabel = EditorTransactionManager::GetInstance()->CanRedo()
			? "やり直す: " + EditorTransactionManager::GetInstance()->GetRedoLabel()
			: "やり直す";
		DrawEditorCommandMenuItem(EditorCommandId::EditUndo, undoLabel.c_str());
		DrawEditorCommandMenuItem(EditorCommandId::EditRedo, redoLabel.c_str());
		ImGui::Separator();
		DrawEditorCommandMenuItem(EditorCommandId::EditDuplicate);
		DrawEditorCommandMenuItem(EditorCommandId::EditDelete);
		DrawEditorCommandMenuItem(EditorCommandId::ObjectDropToFloor);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("表示")) {
        EditorToolSettings::Get().DrawVisibilityMenu();
		DrawEditorCommandMenuItem(EditorCommandId::ViewPortfolio, nullptr, portfolioCaptureMode_);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("ONにするとImGuiを描画せず、ゲーム画面だけを直接表示します。戻るときはF10です。");
		}
		ImGui::Separator();
		DrawEditorCommandMenuItem(EditorCommandId::ViewEditorPanels, nullptr, showDebugWindows_);
        DrawEditorCommandMenuItem(EditorCommandId::ViewAssetReferences);
        DrawEditorCommandMenuItem(
            EditorCommandId::ViewIsolateSelection, nullptr,
            sceneWorkspace_ && sceneWorkspace_->IsIsolationActive());
        DrawEditorCommandMenuItem(EditorCommandId::ViewRestoreTemporaryVisibility);
        ImGui::Separator();
		ImGui::Separator();
		DrawEditorCommandMenuItem(EditorCommandId::ViewConsole, nullptr, showDebugConsole_);
		DrawEditorCommandMenuItem(EditorCommandId::ViewStatus, nullptr, showTimeController_);
		DrawEditorCommandMenuItem(EditorCommandId::ViewReplay, nullptr, showReplayDebugger_);
		DrawEditorCommandMenuItem(EditorCommandId::ViewBossDebug, nullptr, showBossDebug_);
		ImGui::EndMenu();
	}

    DrawWorkspaceMenu();
    EditorToolSettings::Get().DrawToolsMenu();

#ifdef NDEBUG
    if (ImGui::BeginMenu("デバッグ")) {
        auto* player = DebugHealthPlayer(sceneManager);
        const bool available = isPlaying && player && !player->isDead && player->GetHp() > 0.0f;
        if (ImGui::MenuItem("無敵（HP 1,000,000）", nullptr, debugHugeHealth, available)) {
            if (debugHugeHealth) DisableDebugHugeHealth(sceneManager);
            else debugHugeHealth = true;
        }
        ImGui::TextDisabled("HPのみ増加。落下・即死は対象外。");
        ImGui::TextDisabled("OFF・再生停止・メニューからのScene保存で解除。");
        ImGui::EndMenu();
    }
    if (!isPlaying) DisableDebugHugeHealth(sceneManager);
    if (debugHugeHealth && isPlaying) {
        // シーン切替後の新しいPlayerにも適用。死亡済みのPlayerは復活させない。
        if (auto* player = DebugHealthPlayer(sceneManager);
            player && !player->isDead && player->param_.has_value() && player->GetHp() > 0.0f) {
            player->param_->hp = 1000000.0f;
        }
    }
#endif

	if (ImGui::BeginMenu(ICON_FA_HISTORY " リプレイ")) {
		DrawEditorCommandMenuItem(EditorCommandId::ViewReplay, "保存済みリプレイ / 再生・確認を開く", showReplayDebugger_);
		const std::size_t changeCount = playModeChangeTracker_ ? playModeChangeTracker_->GetPendingChangeCount() : 0;
		const std::string changeLabel = "Play中の変更を確認（" + std::to_string(changeCount) + "件）";
		if (ImGui::MenuItem(changeLabel.c_str(), nullptr, false, changeCount > 0 && !isPlaying)) {
			playModeChangeTracker_->ShowPendingChanges();
		}
		ImGui::Separator();
		const bool replayPaused = replayDebugger_ && replayDebugger_->ShouldFreezeSimulation();
		DrawEditorCommandMenuItem(
			EditorCommandId::ReplayPauseResume,
			replayPaused ? "選択時点から実行を再開" : "実行を一時停止");
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("シーン切り替え")) {
		const bool canSwitchScene = sceneManager && !sceneManager->IsTransitioning() && !isPlaying;
		ImGui::BeginDisabled(!canSwitchScene);
		ImGui::TextDisabled("C++ Scene");
		if (sceneManager) {
			const bool hasUnsavedChanges = debugEditor_ && debugEditor_->HasAnyDirty();
			ImGui::BeginDisabled(hasUnsavedChanges);
			for (const std::string& sceneName : sceneManager->GetRegisteredSceneNames()) {
				if (sceneName == "SCENE_EDITOR") {
					continue;
				}
				if (ImGui::MenuItem(sceneName.c_str(), nullptr, sceneName == sceneManager->GetCurrentSceneName() && !sceneManager->HasActiveSceneAsset())) {
					ClearSceneBoundEditorState();
					sceneManager->ChangeScene(sceneName);
				}
			}
			ImGui::EndDisabled();
			if (hasUnsavedChanges) {
				ImGui::TextDisabled("先にActive Sceneを保存してください");
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("Scene Asset");
		if (debugEditor_) {
			const std::vector<SceneSerializer::SceneAssetInfo> sceneAssets = debugEditor_->GetSceneAssets();
			if (sceneAssets.empty()) {
				ImGui::TextDisabled("Scene Assetがありません");
			}
			for (const SceneSerializer::SceneAssetInfo& asset : sceneAssets) {
				const std::string label = asset.displayName + "##SceneAssetMenu_" + asset.filename;
				const std::string runtimeLabel = asset.runtimeScene + " / " + asset.controllerName;
				if (ImGui::MenuItem(
					label.c_str(),
					runtimeLabel.c_str(),
					debugEditor_->IsCurrentSceneAsset(asset.filename))) {
					showDebugWindows_ = true;
					debugEditor_->RequestOpenSceneAssetFromMenu(asset.filename);
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("ヘルプ")) {
		DrawEditorCommandMenuItem(EditorCommandId::HelpManual, ICON_FA_BOOK " エンジン説明書");
		DrawEditorCommandMenuItem(EditorCommandId::HelpProfiler, ICON_FA_CHART_BAR " システムプロファイラ");
		ImGui::EndMenu();
	}
		DrawEditorCommandMenuItem(EditorCommandId::HelpGameplayEventTrace, ICON_FA_PROJECT_DIAGRAM " Gameplay Event Trace");

	ImGui::EndMainMenuBar();
	DrawUnsavedExitConfirmPopup();
}
// エディタ上に未保存変更が残っているかを確認する。

bool GameEditorController::HasUnsavedEditorChanges() const {
	return debugEditor_ && debugEditor_->HasAnyDirty();
}
// 終了要求時に未保存変更があれば確認ポップアップを開く。

void GameEditorController::RequestExit() {
	if (HasUnsavedEditorChanges()) {
		openUnsavedExitConfirm_ = true;
		return;
	}

	WinApp::CloseNow();
}
// 現在の編集状態をメモリへ保持して、そのままゲーム再生を開始する。

void GameEditorController::RequestPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName) {
	StartPlay(sceneManager, isPlaying, currentSceneName);
}
// Scene再読み込みは行わず、Play開始直前の状態を保持してゲームモードへ切り替える。

void GameEditorController::StartPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName) {
	if (playModeChangeTracker_) {
		playModeChangeTracker_->CaptureBaseline();
	}
	ClearSceneBoundEditorState();
	playOriginSceneName_ = sceneManager ? sceneManager->GetCurrentSceneName() : currentSceneName;
	playOriginSceneGeneration_ = sceneManager ? sceneManager->GetSceneGeneration() : 0;
	playOriginSceneLoadContext_ = sceneManager ? sceneManager->GetActiveSceneLoadContext() : SceneLoadContext{};
	pendingPlayOriginReload_ = false;

	const bool snapshotCaptured = replayDebugger_ && replayDebugger_->BeginPlayInEditorSnapshot();
	if (!snapshotCaptured) {
		DebugConsole::GetInstance()->AddLog(
			"Play In Editor Warning: 編集状態を保持できなかったため、停止時はScene再ロードを使用します。");
	}

	ApplyPendingPlayStartPosition();
	// エディターのプレビューや直前のPlayから残った一時演出を、再生開始前に破棄します。
	// シーケンスを先に止めることで、消去済みエフェクトが後から再生成されるのを防ぎます。
	VFXSequencer::ClearOneShots();
	MeshEffectManager::GetInstance()->Clear();
	GPUParticleManager::GetInstance()->ResetSimulation();
	DebrisEffectManager::GetInstance()->Clear();
	isPlaying = true;
	if (sceneManager) {
		sceneManager->SetIsPlaying(true);
	}
	CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
}
// GameView等で指定された地点を保持し、そのままPlayを開始します。


void GameEditorController::RequestPlayFromPosition(const Vector3& position, const std::string& label) {
	if (!activePlayState_ || *activePlayState_ || !sceneManager_ || sceneManager_->IsTransitioning()) {
		return;
	}
	hasPendingPlayStartPosition_ = true;
	pendingPlayStartPosition_ = position;
	pendingPlayStartLabel_ = label;
	RequestPlay(sceneManager_, *activePlayState_, activeSceneName_);
}

void GameEditorController::ApplyPendingPlayStartPosition() {
	if (!hasPendingPlayStartPosition_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;
	Player* player = nullptr;
	for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
		if (!object || object->GetClassName() != "Player") continue;
		player = dynamic_cast<Player*>(object.get());
		if (player) break;
	}
	if (!player) {
		DebugConsole::GetInstance()->AddLog("Play開始位置を適用できません: Playerが見つかりません。");
		hasPendingPlayStartPosition_ = false;
		pendingPlayStartLabel_.clear();
		return;
	}
	player->SetTranslate(pendingPlayStartPosition_);
	player->SetRespawnPosition(pendingPlayStartPosition_);
	player->SetVelocity({ 0.0f, 0.0f, 0.0f });
	player->SetIsControlActive(true);
	player->UpdateLocalMatrix();
	player->UpdateWorldMatrix();
	DebugConsole::GetInstance()->AddLog(
		"Play開始位置: " + pendingPlayStartLabel_ + " (" +
		std::to_string(pendingPlayStartPosition_.x) + ", " +
		std::to_string(pendingPlayStartPosition_.y) + ", " +
		std::to_string(pendingPlayStartPosition_.z) + ")");
	hasPendingPlayStartPosition_ = false;
	pendingPlayStartLabel_.clear();
}

// シーンに紐づく選択状態やゴースト対象をクリアし、Play前後の参照不整合を防ぐ。
void GameEditorController::ClearSceneBoundEditorState() {
    if (ghostDirector_) ghostDirector_->EndStudio();
    if (debugEditor_) debugEditor_->GetAnimationWorkbench()->StopPreview();
    if (sceneWorkspace_) {
        sceneWorkspace_->RestoreAllTemporaryVisibility();
    }
	if (replayDebugger_) {
		replayDebugger_->ResetForSceneChange();
	}
	if (ghostRecorder_) {
		ghostRecorder_->ClearTarget();
	}
	if (debugEditor_) {
		debugEditor_->SetSelectedObject(nullptr);
	}
	if (spriteDebugEditor_) {
		spriteDebugEditor_->ClearSceneSelection();
	}
	EditorManager::GetInstance()->ClearSelection();
}
// 未保存変更がある状態で終了しようとした時の確認ポップアップを描画する。

void GameEditorController::DrawUnsavedExitConfirmPopup() {
	const char* popupName = "未保存の変更があります###UnsavedExitConfirm";
	if (openUnsavedExitConfirm_) {
		ImGui::OpenPopup(popupName);
		openUnsavedExitConfirm_ = false;
	}

	ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.32f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " この変更はまだ保存されていません。");
	ImGui::Spacing();
	ImGui::TextWrapped("保存されていない変更があります。このまま終了すると、未保存の編集内容が失われる可能性があります。");
	if (debugEditor_) {
		ImGui::Spacing();
		ImGui::TextDisabled("未保存: %s", debugEditor_->GetDirtySummaryText().c_str());
	}
	ImGui::Separator();

	if (ImGui::Button(ICON_FA_STOP " 保存せず終了", ImVec2(170.0f, 0.0f))) {
		DebugConsole::GetInstance()->AddLog("Exit Warning: 未保存の変更を破棄して終了しました。");
		WinApp::CloseNow();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_TIMES " キャンセル", ImVec2(130.0f, 0.0f))) {
		DebugConsole::GetInstance()->AddLog("Exit Warning: 未保存のため終了をキャンセルしました。");
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}
// パーティクル、VFX、メッシュ、デブリ、トレイルなどのエディタツールを更新する。

void GameEditorController::UpdateTools(float deltaTime, bool isPlaying, float timeScale) {
	SceneManager* sceneManager = SceneManager::GetInstance();
	const bool sceneTransitioning = sceneManager && sceneManager->IsTransitioning();
	if (sceneTransitioning) {
		if (!sceneTransitionStateCleared_) {
			// Runtime側から開始したScene遷移でも、破棄予定Object/SpriteをInspectorに残しません。
			ClearSceneBoundEditorState();
			sceneTransitionStateCleared_ = true;
		}
	} else {
		sceneTransitionStateCleared_ = false;
	}
	const EnemyAttackPreviewWindow* enemyPreview = debugEditor_ ? debugEditor_->GetEnemyAttackPreviewWindow() : nullptr;
	// 敵プレビュー中は同じGPU/Mesh/Particle管理系を敵タイムラインが所有します。
	// 非選択のVFXツールが時間倍率を戻したり実時間更新したりすると、一時停止中の短寿命粒子が消えるため更新を競合させません。
	const bool enemyPreviewOwnsEffects = enemyPreview && enemyPreview->IsEnabled() && !isPlaying;
	if (replayDebugger_) {
		replayDebugger_->UpdateBeforeSimulation(deltaTime, isPlaying);
	}
	const bool replayFrozen = ShouldFreezeSimulationForReplay();

	if (!sceneTransitioning && !replayFrozen) {
		if (ghostDirector_) {
			ghostDirector_->Update(isPlaying ? deltaTime * timeScale : deltaTime);
		}
		if (!enemyPreviewOwnsEffects) {
			if (particleEditor_) {
				particleEditor_->Update(deltaTime, isPlaying);
			}
			if (gpuParticleEditor_) {
				gpuParticleEditor_->Update(deltaTime, isPlaying);
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
		}
	}
	if (debugEditor_ && debugEditor_->GetCaptureToolWindow()) {
		debugEditor_->GetCaptureToolWindow()->SetForceGameViewClientCapture(portfolioCaptureMode_);
		debugEditor_->GetCaptureToolWindow()->UpdateHotkeys();
	}

	if (isPlaying && !sceneTransitioning && !replayFrozen) {
		MeshEffectManager::GetInstance()->Update(deltaTime * timeScale);
		GPUParticleManager::GetInstance()->Update(deltaTime * timeScale);
	}
}
// Hierarchy、Inspector、Project、Console、Statusなどの補助ウィンドウを描画する。

void GameEditorController::DrawToolWindows(
	float& timeScale,
	float sceneUpdateTimeMs,
	float cpuCmdTimeMs,
	float drawTimeMs,
	float* updateTimeHistory,
	float* drawTimeHistory,
	int timeHistoryIndex) {
	const bool replayWorkspaceActive = replayDebugger_ && showReplayDebugger_;
    EditorToolSettings::Get().DrawWindow();
	const bool enemyAttackWorkspaceActive =
		showEnemyAttackTimeline_ &&
		debugEditor_ &&
		debugEditor_->GetEnemyAttackPreviewWindow();
	const bool effectPreviewWorkspaceActive = showEffectPreviewTimeline_ && debugEditor_;
	const bool expandedBottomWorkspace = replayWorkspaceActive || enemyAttackWorkspaceActive || effectPreviewWorkspaceActive ||
        showAnimationTimeline_ || showCinematicTimeline_;
	if (showDebugWindows_) {
		if (debugEditor_) {
			debugEditor_->SetProjectWindowVisible(!expandedBottomWorkspace);
			debugEditor_->DrawHierarchy();
		}

		EditorManager::GetInstance()->DrawInspector();

		if (spriteDebugEditor_) {
			spriteDebugEditor_->DrawHierarchyWindow();
			spriteDebugEditor_->DrawInspectorWindow();
			if (!expandedBottomWorkspace) {
				spriteDebugEditor_->DrawProjectWindow();
			}
		}
	}

    if (!expandedBottomWorkspace && showDebugConsole_ && EditorToolSettings::Get().IsVisible("diagnosticWindows")) {
		DebugConsole::GetInstance()->DrawImGui();
	}
    if (!expandedBottomWorkspace && showTimeController_ && EditorToolSettings::Get().IsVisible("diagnosticWindows")) {
		DrawStatusWindow(timeScale, sceneUpdateTimeMs, cpuCmdTimeMs, drawTimeMs, updateTimeHistory, drawTimeHistory, timeHistoryIndex);
	}
	if (replayWorkspaceActive) {
		bool replayOpen = true;
		replayDebugger_->Draw(&replayOpen);
		if (!replayOpen) {
			SetReplayDebuggerVisible(false);
		}
	}
	if (enemyAttackWorkspaceActive) {
		debugEditor_->GetEnemyAttackPreviewWindow()->DrawTimelineWindow();
	}
	if (effectPreviewWorkspaceActive) {
		EffectPreviewStage::GetInstance()->DrawTimelineWindow();
	}
    if (showAnimationTimeline_ && debugEditor_) debugEditor_->GetAnimationWorkbench()->DrawTimelinePanel();
    if (showCinematicTimeline_ && ghostDirector_) ghostDirector_->DrawTimelinePanel();
	if (debugEditor_) {
		EnemyAttackPreviewWindow* enemyPreview = debugEditor_->GetEnemyAttackPreviewWindow();
		if (enemyPreview && enemyPreview->IsEnabled()) {
			enemyPreview->ApplyEffectPlaybackState();
		}
	}
	if (engineManualWindow_) {
		engineManualWindow_->Draw();
	}
    if (EditorToolSettings::Get().IsVisible("diagnosticWindows")) ProfilerManager::GetInstance()->DrawImGui();
    if (editorQuickSearch_) {
        editorQuickSearch_->Draw();
    if (EditorToolSettings::Get().IsVisible("diagnosticWindows")) GameplayEventTrace::GetInstance()->DrawImGui();
    }
    if (assetReferenceExplorer_) {
        assetReferenceExplorer_->Draw();
    }
    if (playModeChangeTracker_) {
        playModeChangeTracker_->Draw();
    }
}

bool GameEditorController::ShouldFreezeSimulationForReplay() const {
	return replayDebugger_ && replayDebugger_->ShouldFreezeSimulation();
}

float GameEditorController::ResolveReplaySimulationDeltaTime(float defaultDeltaTime) const {
    return replayDebugger_
        ? replayDebugger_->ResolveSimulationDeltaTime(defaultDeltaTime)
        : defaultDeltaTime;
}

void GameEditorController::CaptureReplayFrame(float simulationDeltaTime, bool isPlaying) {
	if (replayDebugger_) {
		replayDebugger_->CaptureAfterSimulation(simulationDeltaTime, isPlaying);
	}
}
// FPS、CPU/GPU負荷、時間倍率などを確認するステータスウィンドウを描画する。

void GameEditorController::DrawStatusWindow(
	float& timeScale,
	float sceneUpdateTimeMs,
	float cpuCmdTimeMs,
	float drawTimeMs,
	float* updateTimeHistory,
	float* drawTimeHistory,
	int timeHistoryIndex) {
	ImGui::Begin("ステータス", &showTimeController_, ImGuiWindowFlags_HorizontalScrollbar);

	float fps = ImGui::GetIO().Framerate;
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

	ImGui::Separator();
	if (ImGui::CollapsingHeader("描画統計 (Render Stats)", ImGuiTreeNodeFlags_DefaultOpen)) {
		const RenderFrameStats& frame = RenderStats::GetInstance()->GetLastCompletedFrame();
		uint64_t totalDrawCalls = 0;
		uint64_t totalIndirectDrawCalls = 0;
		uint64_t totalObjects = 0;
		uint64_t totalCulled = 0;
		uint64_t totalTriangles = 0;
		uint64_t totalInstances = 0;
		uint64_t totalDispatches = 0;
		uint64_t totalThreadGroups = 0;

		for (const RenderPassStats& pass : frame.passes) {
			totalDrawCalls += pass.drawCalls;
			totalIndirectDrawCalls += pass.indirectDrawCalls;
			totalObjects += pass.submittedObjects;
			totalCulled += pass.culledObjects;
			totalTriangles += pass.submittedTriangles;
			totalInstances += pass.submittedInstances;
			totalDispatches += pass.computeDispatches;
			totalThreadGroups += pass.computeThreadGroups;
		}

		if (frame.frameNumber == 0) {
			ImGui::TextDisabled("描画統計を準備中です。");
		} else {
			ImGui::Text("計測フレーム: %llu", static_cast<unsigned long long>(frame.frameNumber));
			ImGui::Text(
				"Draw: %llu (Indirect %llu)  Object送信: %llu  カリング: %llu",
				static_cast<unsigned long long>(totalDrawCalls),
				static_cast<unsigned long long>(totalIndirectDrawCalls),
				static_cast<unsigned long long>(totalObjects),
				static_cast<unsigned long long>(totalCulled));
			ImGui::Text(
				"送信Triangle: %llu  Instance: %llu",
				static_cast<unsigned long long>(totalTriangles),
				static_cast<unsigned long long>(totalInstances));

			if (ImGui::BeginTable(
				"RenderStatsByPass",
				6,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("描画パス");
				ImGui::TableSetupColumn("Draw");
				ImGui::TableSetupColumn("Object");
				ImGui::TableSetupColumn("Cull");
				ImGui::TableSetupColumn("Triangle");
				ImGui::TableSetupColumn("Instance");
				ImGui::TableHeadersRow();

				for (size_t passIndex = 0; passIndex < kRenderPassCount; ++passIndex) {
					const RenderPass passType = static_cast<RenderPass>(passIndex);
					const RenderPassStats& pass = frame.passes[passIndex];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(RenderStats::GetPassDisplayName(passType));
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.drawCalls));
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.submittedObjects));
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.culledObjects));
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.submittedTriangles));
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.submittedInstances));
				}
				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::Text(
				"有効ライト: Point %u / %zu  Spot %u / %zu",
				frame.activePointLights,
				LightManager::GetInstance()->GetPointLights().size(),
				frame.activeSpotLights,
				LightManager::GetInstance()->GetSpotLights().size());
			ImGui::Text(
				"Shadow Map: %d x %d / 範囲 %.0f",
				DirectXCommon::GetInstance()->GetShadowMapResolution(),
				DirectXCommon::GetInstance()->GetShadowMapResolution(),
				LightManager::GetInstance()->GetShadowAreaSize());
			ImGui::Text(
				"GPU Particle: %u systems / System容量合計 %llu",
				frame.gpuParticleSystems,
				static_cast<unsigned long long>(frame.gpuParticleCapacity));
			ImGui::Text(
				"CPU Particle: %llu / PostEffect Pass: %u",
				static_cast<unsigned long long>(frame.cpuParticleCount),
				frame.postProcessPasses);
			ImGui::Text(
				"Compute Dispatch: %llu / Thread Group: %llu",
				static_cast<unsigned long long>(totalDispatches),
				static_cast<unsigned long long>(totalThreadGroups));
			ImGui::TextDisabled("Draw数はエンジンが発行した命令です。ImGui内部描画は含みません。");
			ImGui::TextDisabled("GPU ParticleのSystem容量合計は生存数ではなく、各プリセットが確保・Compute更新する上限枠の合計です。");
			ImGui::TextDisabled("Indirect Particleの実Instance/Triangle数はGPUが決めるため、上のCPU集計には含みません。");
		}
	}

	ImGui::End();
}
// ProjectWindowから要求されたサムネイル撮影を処理する。

void GameEditorController::CapturePendingThumbnails() {
	if (debugEditor_ && debugEditor_->GetProjectWindow()) {
		debugEditor_->GetProjectWindow()->CapturePendingThumbnails();
	}
}

void GameEditorController::ExportCapturedHudPortraits() {
	if (debugEditor_ && debugEditor_->GetProjectWindow()) {
		debugEditor_->GetProjectWindow()->ExportCapturedHudPortraits();
	}
}
// エディタ選択中のプレビューやゴースト表示を3Dシーン上に描画する。

void GameEditorController::DrawScenePreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
	if (debugEditor_) {
		SceneManager* editorSceneManager = debugEditor_->GetSceneManager();
		const bool isPlaying = editorSceneManager && editorSceneManager->IsPlaying();
		if (!isPlaying) {
			Object3d* selectedObject = debugEditor_->GetSelectedObject();
			if (selectedObject && !IsObjectInCurrentScene(editorSceneManager, selectedObject)) {
				debugEditor_->SetSelectedObject(nullptr);
				if (EditorManager::GetInstance()->GetSelectedObject() == debugEditor_.get()) {
					EditorManager::GetInstance()->ClearSelection();
				}
				selectedObject = nullptr;
			}
			if (selectedObject && selectedObject->recorder_) {
				selectedObject->recorder_->SetSceneManager(editorSceneManager);
			}
            if (EditorToolSettings::Get().IsVisible("ghostModels") && selectedObject && selectedObject->recorder_ && selectedObject->recorder_->HasPreviewData()) {
				selectedObject->recorder_->DrawObjectGhostPreview(pointLightResource, spotLightResource);
			}
            if (EditorToolSettings::Get().IsVisible("placementPreview")) debugEditor_->DrawPreview(pointLightResource, spotLightResource);
			if (BaseScene* currentScene = editorSceneManager ? editorSceneManager->GetCurrentScene() : nullptr) {
				CameraEditor* cameraEditor = CameraEditor::GetInstance();
				cameraEditor->SetObject3dCommon(currentScene->GetObject3dCommon());
				cameraEditor->DrawCameraModelGizmos(pointLightResource, spotLightResource);
			}
		}
	}

	SceneManager* editorSceneManager = debugEditor_ ? debugEditor_->GetSceneManager() : nullptr;
	const bool isPlaying = editorSceneManager && editorSceneManager->IsPlaying();
    if (EditorToolSettings::Get().IsVisible("ghostModels") && !isPlaying && ghostRecorder_ && EditorManager::GetInstance()->GetSelectedObject() == ghostRecorder_.get()) {
		ghostRecorder_->SetSceneManager(editorSceneManager);
		ghostRecorder_->DrawObjectGhostPreview(pointLightResource, spotLightResource);
	}
}
// DebugEditorやMeshEffectEditorのデバッグ描画を実行する。

void GameEditorController::DrawSceneDebug(ID3D12GraphicsCommandList* commandList) {
    if (EditorToolSettings::Get().IsTemporarilyHidden()) return;
	if (debugEditor_) {
		debugEditor_->DrawDebug(commandList);
	}
    if (meshEffectEditor_ && EditorToolSettings::Get().IsVisible("meshEffectGuides") && EditorManager::GetInstance()->GetSelectedObject() == meshEffectEditor_.get()) {
		meshEffectEditor_->Draw();
	}
}
// Sprite編集UIとImGui全体を最終バックバッファへ描画する。

void GameEditorController::DrawBackBufferUi() {
    if (spriteDebugEditor_ && EditorToolSettings::Get().IsVisible("spriteGuides")) {
		spriteDebugEditor_->Draw();
	}
	ImGuiManager::GetInstance()->Draw();
}
// ImGuiのフレームを終了し、描画コマンドを発行できる状態にする。

void GameEditorController::EndFrame() {
	ImGuiManager::GetInstance()->EndFrame();
	if (replayDebugger_ && debugEditor_ && debugEditor_->GetCaptureToolWindow()) {
		replayDebugger_->CapturePendingRegressionScreenshot(
			debugEditor_->GetCaptureToolWindow());
	}
}
// ギズモやSprite編集中はカメラ入力を止め、操作の競合を防ぐ。

void GameEditorController::ApplyCameraInputState(const EditorFrameState& frameState, bool isPlaying) {
	if (Camera* mainCamera = CameraManager::GetInstance()->GetActiveCamera()) {
		mainCamera->SetInputEnabled(isPlaying || !(frameState.spriteEditorBusy || frameState.gizmoBusy));
	}
}
// エフェクトプレビューやアニメーション作業台のカメラ上書きを現在フレームへ反映する。

void GameEditorController::ApplyCameraOverrides() {
    if (ShouldFreezeSimulationForReplay()) {
        return;
    }
    // 通常アニメーションに上書きされた演出の身体姿勢を描画直前に反映する。
    if (ghostDirector_) {
        ghostDirector_->ApplyBodyPose();
    }
	if (debugEditor_ && debugEditor_->GetEffectPreviewStage()) {
		debugEditor_->GetEffectPreviewStage()->ApplyCameraOverride();
	}
	if (debugEditor_ && debugEditor_->GetAnimationWorkbench()) {
		debugEditor_->GetAnimationWorkbench()->ApplyCameraOverride();
	}
}
// すべてのエディタデータ保存処理の入口。現在はログ出力のみを行う。

void GameEditorController::SaveAllEditors() {
	DebugConsole::GetInstance()->AddLog("--- Auto Saving All Editor Data... ---");
	DebugConsole::GetInstance()->AddLog("--- Save Complete! ---");
}

#endif
