#include "engine/core/editor/GameEditorController.Internal.h"

#ifdef USE_IMGUI
using namespace GameEditorControllerImplementation;

// エディターのコマンドとツール登録。
void GameEditorController::RegisterEditorCommands() {
	EditorCommandRegistry* registry = EditorCommandRegistry::GetInstance();
	registry->UnregisterOwner(this);

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

    registry->Register({
        EditorCommandId::WorkspaceTerrain, "地形編集ワークスペース", "Workspace", "",
        "地形生成Editorを開き、保存済みの地形編集レイアウトを適用します。",
        { "terrain", "layout", "workspace", "地形" },
        [this]() { return CanEditScene(); },
        [this]() { ApplyWorkspacePreset(WorkspacePreset::Terrain); }, this });
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
		EditorCommandId::ViewBossDebug, "ボスロジックデバッグ", "View", "",
		"ボスロジックデバッグの表示を切り替えます。", { "boss", "debug", "ボス" }, {},
		[this]() { showBossDebug_ = !showBossDebug_; }, this });
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
    registry->Register({
        EditorCommandId::HelpGameplayEventTrace, "Gameplay Event Trace", "Help", "",
        "Animation EventからVFX・Audio・Camera・HitStopまでの発火経路を表示します。",
        { "event", "trace", "feedback", "vfx", "animation", "演出" }, {},
        []() { GameplayEventTrace::GetInstance()->Open(); }, this });
}
// ゲームエディタで使う各種ツールとウィンドウを生成し、SceneManagerへ接続する。

void GameEditorController::RegisterEditorTools() {
    auto& settings = EditorToolSettings::Get();
    settings.Clear();
    auto available = [this]() { return debugEditor_ && sceneManager_ && !sceneManager_->IsTransitioning(); };
    auto add = [&](const char* id, const char* name, const char* category, IEditable* target, bool preserve = false) {
        if (!target) return;
        settings.AddTool(std::string("tool.") + id, name, category, [this, target, preserve]() {
            showDebugWindows_ = true;
            SetReplayDebuggerVisible(false);
            if (!preserve) debugEditor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(target);
        }, available);
    };
    add("camera", "カメラ設定", "ステージ", CameraEditor::GetInstance());
    add("light", "ライティング設定", "ステージ", LightEditor::GetInstance());
    add("post", "ポストエフェクト", "演出", postEffectEditor_.get());
    add("key", "ゲーム操作キーコンフィグ", "検証・設定", KeyConfig::GetInstance());
    add("gpu", "GPUパーティクル", "演出", gpuParticleEditor_.get());
    add("particle", "通常パーティクル", "演出", particleEditor_.get());
    add("vfx", "VFXシーケンサー", "演出", vfxSequencerEditor_.get());
    add("director", "演出タイムライン（Cinematic Timeline）", "演出", ghostDirector_.get(), true);
    add("mesh", "メッシュエフェクト", "演出", meshEffectEditor_.get());
    add("debris", "3D破片エフェクト", "演出", debrisEffectEditor_.get());
    add("trail", "トレイルエミッター", "演出", trailEmitterEditor_.get());
    add("preset", "プリセットエディター", "素材", PresetEditor::GetInstance());
    add("auditScene", "シーン視覚監査", "検証・設定", debugEditor_->GetSceneValidator());
    add("levelPlan", "配置プラン検証（レベル設計ラボ）", "ステージ", debugEditor_->GetLevelDesignLabWindow());
    add("inventory", "配置物集計", "ステージ", debugEditor_->GetSceneInventoryWindow());
    add("material", "マテリアル確認", "素材", debugEditor_->GetMaterialPreviewBoard());
    add("effectPreview", "エフェクト確認ステージ", "演出", debugEditor_->GetEffectPreviewStage());
    add("animation", "アニメーション制作", "演出", debugEditor_->GetAnimationWorkbench());
    add("animator", "Animator Controller", "演出", debugEditor_->GetAnimatorControllerEditor());
    add("links", "イベントリンク図", "ステージ", debugEditor_->GetEventLinkGraph());
    add("nodes", "演出ノード", "演出", debugEditor_->GetNodeGraphEditorWindow());
    add("text", "テキストPNG生成", "素材", debugEditor_->GetTextSpriteGenerator());
    add("text3d", "3Dテキスト生成", "素材", debugEditor_->GetText3DGenerator());
    add("models", "モデル最適化", "素材", debugEditor_->GetModelOptimizerWindow());
    add("terrain", "地形生成", "ステージ", debugEditor_->GetTerrainEditorWindow());
    add("assets", "アセット監査", "検証・設定", debugEditor_->GetAssetAuditWindow());
    add("status", "ステータス管理", "戦闘", debugEditor_->GetStatusTuningWindow());
    add("backup", "JSONバックアップ", "検証・設定", debugEditor_->GetJsonBackupWindow());
    add("audio", "音声設定", "演出", debugEditor_->GetAudioSettingsWindow());
    add("package", "実行ファイルセット", "検証・設定", debugEditor_->GetExecutablePackageWindow());
    add("capture", "キャプチャツール", "検証・設定", debugEditor_->GetCaptureToolWindow());
    add("gameData", "内部データ編集", "検証・設定", debugEditor_->GetGameDataDebugEditor());
    auto openPreview = [this](bool player) {
        showDebugWindows_ = true; SetReplayDebuggerVisible(false);
        auto* preview = debugEditor_->GetEnemyAttackPreviewWindow();
        preview->SetPlayerCopyMode(player);
        debugEditor_->SetSelectedObject(nullptr);
        EditorManager::GetInstance()->SetSelectedObject(preview);
    };
    settings.AddTool("tool.enemy", "敵攻撃プレビュー", "戦闘", [openPreview]() { openPreview(false); }, available);
    settings.AddTool("tool.player", "プレイヤー能力プレビュー", "戦闘", [openPreview]() { openPreview(true); }, available);
    settings.AddTool("tool.recorder", "移動軌跡の記録（Ghost Recorder）", "演出", [this]() {
        showDebugWindows_ = true; SetReplayDebuggerVisible(false);
        if (auto* object = debugEditor_->GetSelectedObject()) ghostRecorder_->SetTarget(object);
        debugEditor_->SetSelectedObject(nullptr);
        EditorManager::GetInstance()->SetSelectedObject(ghostRecorder_.get());
    }, available);
    settings.AddTool("tool.matrix", "プロパティマトリクス", "ステージ", [this]() {
        showDebugWindows_ = true; SetReplayDebuggerVisible(false);
        debugEditor_->GetPropertyMatrixWindow()->Open();
        EditorManager::GetInstance()->SetSelectedObject(debugEditor_->GetPropertyMatrixWindow());
    }, available);
    settings.AddTool("tool.game", "ゲーム設定", "戦闘", [this]() {
        showDebugWindows_ = true; debugEditor_->SetSelectedObject(nullptr);
        EditorManager::GetInstance()->SetSelectedObject(sceneManager_->GetCurrentScene());
    }, [this, available]() { return available() && sceneManager_->GetCurrentScene(); });
    settings.AddTool("tool.replay", "リプレイ", "検証・設定", [this]() { SetReplayDebuggerVisible(true); }, available);

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
    settings.Load();
}


#endif
