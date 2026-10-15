#include "engine/core/editor/GameEditorController.Internal.h"

#ifdef USE_IMGUI
using namespace GameEditorControllerImplementation;

// エディターのコマンドとツール登録。
void GameEditorController::RegisterEditorCommands() {
	EditorCommandRegistry* registry = EditorCommandRegistry::GetInstance();
	// 再初期化時に旧ラムダが this を保持し続けないよう、同じ Owner の登録を先に解除します。
	registry->UnregisterOwner(this);

	// Play/Stop と Replay の一時停止は Active Scene の実行状態を確認してから有効にします。
	registry->Register({
		EditorCommandId::Play, "再生", "Game", "Ctrl+P",
		"Active Sceneをゲームモードで再生します。", { "play", "game", "実行" },
		[this]() {
			return activePlayState_ && !*activePlayState_ && sceneManager_ && !sceneManager_->IsTransitioning();
		},
		[this]() { RequestPlay(sceneManager_, *activePlayState_, activeSceneName_); }, this });

	registry->Register({
		EditorCommandId::Stop, "停止", "Game", "Ctrl+P",
		"ゲームモードを停止して編集Sceneへ戻ります。", { "stop", "game", "停止" },
		[this]() { return activePlayState_ && *activePlayState_; },
		[this]() { StopPlay(); }, this });

	registry->Register({
		EditorCommandId::ReplayPauseResume, "実行を一時停止／再開", "Replay", "",
		"Replay Debuggerの現在位置でシミュレーションを停止または再開します。",
		{ "replay", "pause", "resume", "一時停止" },
		[this]() {
			return activePlayState_ && *activePlayState_ && replayDebugger_ && replayDebugger_->HasFrames();
		},
		[this]() {
			const bool wasPaused = replayDebugger_->ShouldFreezeSimulation();
			replayDebugger_->ToggleSimulationPause();
			if (!wasPaused) SetReplayDebuggerVisible(true);
		}, this });

	// Scene の保存と Undo/Redo は編集可能な状態に限定し、実操作は DebugEditor に委譲します。
	registry->Register({
		EditorCommandId::SceneNew, "新規Scene Asset", "Scene", "",
		"新しいScene Assetの作成画面を開きます。", { "scene", "new", "作成" },
		[this]() { return CanEditScene(); },
		[this]() { showDebugWindows_ = true; debugEditor_->OpenCreateSceneAssetDialog(); }, this });

	registry->Register({
		EditorCommandId::SceneSave, "Active Sceneを保存", "Scene", "Ctrl+S",
		"現在のScene Object、Camera、Spriteを保存します。", { "scene", "save", "保存" },
		[this]() { return CanEditScene(); },
		[this]() { debugEditor_->SaveScene(SaveMode::All); }, this });

	registry->Register({
		EditorCommandId::EditUndo, "元に戻す", "Edit", "Ctrl+Z",
		"直前のEditor操作を取り消します。", { "undo", "戻す" },
		[this]() { return CanEditScene() && EditorTransactionManager::GetInstance()->CanUndo(); },
		[this]() { debugEditor_->PerformUndo(); }, this });

	registry->Register({
		EditorCommandId::EditRedo, "やり直す", "Edit", "Ctrl+Y",
		"取り消したEditor操作を再適用します。", { "redo", "やり直す" },
		[this]() { return CanEditScene() && EditorTransactionManager::GetInstance()->CanRedo(); },
		[this]() { debugEditor_->PerformRedo(); }, this });

	// 選択 Object への操作は共通の CanEditSelectedObject で適用条件を揃えます。
	registry->Register({
		EditorCommandId::EditDuplicate, "選択Objectを複製", "Edit", "Ctrl+D / Ctrl+C",
		"選択中のObjectと選択中Hierarchyを複製します。", { "duplicate", "copy", "複製" },
		[this]() { return CanEditSelectedObject(); },
		[this]() { debugEditor_->DuplicateSelected(); }, this });

	registry->Register({
		EditorCommandId::EditDelete, "選択Objectを削除", "Edit", "Delete",
		"選択中のObjectをSceneから削除します。", { "delete", "remove", "削除" },
		[this]() { return CanEditSelectedObject(); },
		[this]() { debugEditor_->DeleteSelected(); }, this });

	registry->Register({
		EditorCommandId::ObjectDropToFloor, "選択Objectを床へ移動", "Object", "End",
		"選択Objectを直下のColliderへ接地させます。", { "drop", "floor", "ground", "床" },
		[this]() { return CanEditSelectedObject(); },
		[this]() { debugEditor_->DropToFloor(); }, this });


    // 検索・参照確認・一時表示は Scene の保存内容を変えない閲覧操作として登録します。
    registry->Register({
        EditorCommandId::ViewQuickSearch, "統合コマンドパレット", "View", "Ctrl+K",
        "操作、Scene Object、Asset、Scene、Preset、Editorウィンドウを横断検索します。",
        { "search", "command", "object", "asset", "検索" }, {},
        [this]() { if (editorQuickSearch_) editorQuickSearch_->Open(); }, this });

    registry->Register({
        EditorCommandId::ViewAssetReferences, "Asset参照エクスプローラー", "View", "",
        "Assetの参照元、動的参照候補、Missing参照を横断表示します。",
        { "asset", "reference", "missing", "参照" },
        [this]() { return assetReferenceExplorer_ != nullptr; },
        [this]() { assetReferenceExplorer_->Open(); }, this });

    registry->Register({
        EditorCommandId::ViewIsolateSelection, "選択Objectのみ表示", "View", "",
        "選択Objectとその子だけを作業用に表示します。Sceneデータは変更しません。",
        { "isolate", "selection", "隔離", "一時表示" },
        [this]() { return sceneWorkspace_ && CanEditSelectedObject(); },
        [this]() { sceneWorkspace_->IsolateSelection(); }, this });

    registry->Register({
        EditorCommandId::ViewRestoreTemporaryVisibility, "一時非表示をすべて復元", "View", "",
        "隔離表示、Layer表示、個別の一時非表示をすべて解除します。",
        { "show", "restore", "visibility", "全表示" },
        [this]() { return sceneWorkspace_ && sceneWorkspace_->GetTemporaryHiddenCount() > 0; },
        [this]() { sceneWorkspace_->RestoreAllTemporaryVisibility(); }, this });

    // 作業レイアウトは同じコマンド窓口から切り替え、各ツールの配置を Workspace 側に任せます。
    registry->Register({
        EditorCommandId::WorkspaceVfx, "VFX編集ワークスペース", "Workspace", "",
        "VFX Sequencerを開き、保存済みのVFX編集レイアウトを適用します。",
        { "vfx", "effect", "layout", "エフェクト" },
        [this]() { return CanEditScene(); },
        [this]() { ApplyWorkspacePreset(WorkspacePreset::Vfx); }, this });
    registry->Register({
        EditorCommandId::WorkspaceAnimation, "アニメーション編集ワークスペース", "Workspace", "",
        "Animation Workbenchを開き、保存済みのアニメーション編集レイアウトを適用します。",
        { "animation", "motion", "layout", "アニメーション" },
        [this]() { return CanEditScene(); },
        [this]() { ApplyWorkspacePreset(WorkspacePreset::Animation); }, this });
    registry->Register({
        EditorCommandId::WorkspaceReplay, "リプレイ解析ワークスペース", "Workspace", "",
        "Replay Debuggerを開き、保存済みのリプレイ解析レイアウトを適用します。",
        { "replay", "timeline", "layout", "リプレイ" },
        [this]() { return replayDebugger_ != nullptr; },
        [this]() { ApplyWorkspacePreset(WorkspacePreset::Replay); }, this });
	// ウィンドウ表示と Help は Scene 編集可否とは独立した操作として公開します。
	registry->Register({
		EditorCommandId::ViewEditorPanels, "Hierarchy / Inspector表示", "View", "",
		"主要Editorパネルの表示を切り替えます。", { "hierarchy", "inspector", "window" }, {},
		[this]() { showDebugWindows_ = !showDebugWindows_; }, this });
	registry->Register({
		EditorCommandId::ViewConsole, "デバッグログ", "View", "",
		"Debug Consoleの表示を切り替えます。", { "console", "log", "ログ" }, {},
		[this]() { showDebugConsole_ = !showDebugConsole_; }, this });
	registry->Register({
		EditorCommandId::ViewStatus, "ステータス", "View", "",
		"CPU/GPUステータスの表示を切り替えます。", { "status", "performance", "fps" }, {},
		[this]() { showTimeController_ = !showTimeController_; }, this });
	registry->Register({
		EditorCommandId::ViewReplay, "リプレイデバッガー", "View", "",
		"Replay Editorの表示を切り替えます。", { "replay", "time machine" }, {},
		[this]() { SetReplayDebuggerVisible(!showReplayDebugger_); }, this });
	registry->Register({
		EditorCommandId::ViewPortfolio, "ポートフォリオ撮影モード", "View", "F10",
		"Editor UIを隠してゲーム画面だけを表示します。", { "capture", "portfolio", "撮影" }, {},
		[this]() { SetPortfolioCaptureMode(!portfolioCaptureMode_); }, this });

	registry->Register({
		EditorCommandId::HelpManual, "エンジン説明書", "Help", "",
		"Editor内のエンジン説明書を開きます。", { "manual", "help", "説明書" },
		[this]() { return engineManualWindow_ != nullptr; },
		[this]() { engineManualWindow_->Open(); }, this });
	registry->Register({
		EditorCommandId::HelpProfiler, "システムプロファイラ", "Help", "",
		"CPU/GPUシステムプロファイラを開きます。", { "profiler", "performance", "性能" }, {},
		[]() { ProfilerManager::GetInstance()->Open(); }, this });
}

// ゲームエディタで使う各種ツールとウィンドウを生成し、SceneManagerへ接続する。

void GameEditorController::RegisterEditorTools() {
    auto& settings = EditorToolSettings::Get();
    settings.Clear();

    // Scene 切替中にはツールの対象が入れ替わるため、共通の可用性判定で開く操作を止めます。
    auto available = [this]() { return debugEditor_ && sceneManager_ && !sceneManager_->IsTransitioning(); };
    auto add = [&](const char* id, const char* name, const char* category, IEditable* target, bool preserve = false) {
        if (!target) return;
        settings.AddTool(std::string("tool.") + id, name, category, [this, target, preserve]() {
            showDebugWindows_ = true;
            SetReplayDebuggerVisible(false);
			// 通常は Object 選択を解除して Inspector の対象をツールへ切り替えます。
			// Cinematic Timeline だけは現在の選択を保持して編集できるようにします。
            if (!preserve) debugEditor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(target);
        }, available);
    };

    // 共通の起動経路を使う Editor ツールをカテゴリごとに登録します。
    add("camera", "カメラ設定", "ステージ", CameraEditor::GetInstance());
    add("light", "ライティング設定", "ステージ", LightEditor::GetInstance());
    add("post", "ポストエフェクト", "演出", postEffectEditor_.get());
    add("gpu", "GPUパーティクル", "演出", gpuParticleEditor_.get());
    add("particle", "通常パーティクル", "演出", particleEditor_.get());
    add("vfx", "VFXシーケンサー", "演出", vfxSequencerEditor_.get());
    add("director", "演出タイムライン（Cinematic Timeline）", "演出", ghostDirector_.get(), true);
    add("mesh", "メッシュエフェクト", "演出", meshEffectEditor_.get());
    add("debris", "3D破片エフェクト", "演出", debrisEffectEditor_.get());
    add("trail", "トレイルエミッター", "演出", trailEmitterEditor_.get());
    add("inventory", "配置物集計", "ステージ", debugEditor_->GetSceneInventoryWindow());
    add("material", "マテリアル確認", "素材", debugEditor_->GetMaterialPreviewBoard());
    add("effectPreview", "エフェクト確認ステージ", "演出", debugEditor_->GetEffectPreviewStage());
    add("animation", "アニメーション制作", "演出", debugEditor_->GetAnimationWorkbench());
    add("animator", "Animator Controller", "演出", debugEditor_->GetAnimatorControllerEditor());
    add("text", "テキストPNG生成", "素材", debugEditor_->GetTextSpriteGenerator());
    add("models", "モデル最適化", "素材", debugEditor_->GetModelOptimizerWindow());
    add("assets", "アセット監査", "検証・設定", debugEditor_->GetAssetAuditWindow());
    add("status", "ステータス管理", "戦闘", debugEditor_->GetStatusTuningWindow());
    add("audio", "音声設定", "演出", debugEditor_->GetAudioSettingsWindow());
    add("package", "実行ファイルセット", "検証・設定", debugEditor_->GetExecutablePackageWindow());
    add("capture", "キャプチャツール", "検証・設定", debugEditor_->GetCaptureToolWindow());
    add("gameData", "内部データ編集", "検証・設定", debugEditor_->GetGameDataDebugEditor());
    // 敵攻撃とコピー能力は同じプレビューを使い、起動時の対象モードだけ切り替えます。
    auto openPreview = [this](bool player) {
        showDebugWindows_ = true; SetReplayDebuggerVisible(false);
        auto* preview = debugEditor_->GetEnemyAttackPreviewWindow();
        preview->SetPlayerCopyMode(player);
        debugEditor_->SetSelectedObject(nullptr);
        EditorManager::GetInstance()->SetSelectedObject(preview);
    };
    settings.AddTool("tool.enemy", "敵攻撃プレビュー", "戦闘", [openPreview]() { openPreview(false); }, available);
    settings.AddTool("tool.crownMovie", "王冠継承ムービープレビュー", "演出", [this]() {
        showDebugWindows_ = true; SetReplayDebuggerVisible(false);
        auto* preview = debugEditor_->GetEnemyAttackPreviewWindow();
        preview->SetCrownMovieMode(); debugEditor_->SetSelectedObject(nullptr);
        EditorManager::GetInstance()->SetSelectedObject(preview);
    }, available);
    settings.AddTool("tool.player", "プレイヤー能力プレビュー", "戦闘", [openPreview]() { openPreview(true); }, available);
    // Ghost Recorder は現在の選択 Object を編集対象として受け渡します。
    settings.AddTool("tool.recorder", "移動軌跡の記録（Ghost Recorder）", "演出", [this]() {
        showDebugWindows_ = true; SetReplayDebuggerVisible(false);
        if (auto* object = debugEditor_->GetSelectedObject()) ghostRecorder_->SetTarget(object);
        debugEditor_->SetSelectedObject(nullptr);
        EditorManager::GetInstance()->SetSelectedObject(ghostRecorder_.get());
    }, available);
    settings.AddTool("tool.game", "ゲーム設定", "戦闘", [this]() {
        showDebugWindows_ = true; debugEditor_->SetSelectedObject(nullptr);
        EditorManager::GetInstance()->SetSelectedObject(sceneManager_->GetCurrentScene());
    }, [this, available]() { return available() && sceneManager_->GetCurrentScene(); });
    settings.AddTool("tool.replay", "リプレイ", "検証・設定", [this]() { SetReplayDebuggerVisible(true); }, available);

    // 可視化フラグは各ツールが持つ実際の状態へのポインタを渡し、設定画面と表示を同期します。
    settings.AddVisibility("collision", "当たり判定の補助線", debugEditor_->GetDrawCollidersPtr());
    settings.AddVisibility("invisibleBounds", "透明な壁の補助線", debugEditor_->GetDrawInvisibleBoundsPtr());
    settings.AddVisibility("sceneIcons", "シーン内の盾・カメラ等のアイコン", debugEditor_->GetDrawSceneIconsPtr());
    settings.AddVisibility("helperModels", "透明壁・パーティクル配置用の仮モデル");
    settings.AddVisibility("selectionBounds", "選択オブジェクトの枠・ラベル", debugEditor_->GetDrawSelectionBoundsPtr());
    settings.AddVisibility("eventIds", "イベントID", debugEditor_->GetDrawEventIDsPtr());
    settings.AddVisibility("light", "ライトの目印", LightEditor::GetInstance()->GetGizmoVisiblePtr());
    auto* camera = CameraEditor::GetInstance();
    settings.AddVisibility("cameraPreview", "カメラ設定内の映像プレビュー", camera->GetCameraPreviewVisiblePtr());
    settings.AddVisibility("sceneCameraPreview", "選択カメラのプレビューウィンドウ");
    settings.AddVisibility("objectOrientation", "選択オブジェクトの向き・軸ラベル");
    settings.AddVisibility("transformGizmo", "移動・回転・拡縮の操作ギズモ");
    settings.AddVisibility("placementPreview", "配置・ブラシのプレビューと補助線");
    settings.AddVisibility("spriteGuides", "Sprite編集の選択枠・操作ハンドル");
    settings.AddVisibility("meshEffectGuides", "メッシュエフェクトの補助表示");
    settings.AddVisibility("diagnosticWindows", "デバッグログ・性能・イベント計測ウィンドウ");
    settings.AddVisibility("cameraGuide", "カメラの視野ガイド", camera->GetCameraGuideVisiblePtr());
    settings.AddVisibility("cameraBody", "カメラ本体の目印", camera->GetCameraBodyVisiblePtr());
    settings.AddVisibility("orbitGuide", "カメラ旋回ガイド", camera->GetOrbitGuideVisiblePtr());
    settings.AddVisibility("orbitGizmo", "カメラ中心ギズモ", camera->GetOrbitGizmoVisiblePtr());
    settings.AddVisibility("ghostPath", "ゴースト軌跡の表示（全体）", &ghostPathVisible_);
    settings.AddVisibility("ghostModels", "ゴースト残像モデル（全体）", &ghostModelVisible_);
    settings.AddVisibility("directorTrack", "ムービーの選択トラック軌跡", ghostDirector_->GetTrackPreviewPtr());
    settings.AddVisibility("directorPose", "ムービーの姿勢ガイド", ghostDirector_->GetOrientationPreviewPtr());
    settings.AddVisibility("direction", "右上の方向ギズモ", &sceneDirectionVisible_);
    auto* animation = debugEditor_->GetAnimationWorkbench();
    settings.AddVisibility("bones", "アニメーションのボーン", animation->GetBoneOverlayPtr());
    settings.AddVisibility("boneNames", "ボーン名", animation->GetBoneNamesPtr());
    settings.AddVisibility("boneAxes", "ボーンの選択軸", animation->GetBoneAxesPtr());
    auto* stage = debugEditor_->GetEffectPreviewStage();
    settings.AddVisibility("previewGrid", "エフェクト確認ステージのグリッド", stage->GetGridVisiblePtr());
    settings.AddVisibility("previewAxes", "エフェクト確認ステージの座標軸", stage->GetAxesVisiblePtr());
    // 登録後に保存済みの設定を読み、対象フラグへ反映します。
    settings.Load();
}


#endif
