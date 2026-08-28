#define NOMINMAX
#include "GameEditorController.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "AssetReferenceExplorer.h"
#include "PlayModeChangeTracker.h"
#include "Player.h"
#include "Camera.h"
#include "CameraEditor.h"
#include "CaptureToolWindow.h"
#include "CameraManager.h"
#include "DebrisEffectEditor.h"
#include "DebrisEffectManager.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "GameplayEventTrace.h"
#include "DirectXCommon.h"
#include "EditorCommandRegistry.h"
#include "EditorQuickSearch.h"
#include "EditorAssetDragPayload.h"
#include "EditorManager.h"
#include "EditorTransactionManager.h"
#include "EngineManualWindow.h"
#include "GhostDirector.h"
#include "GhostRecorder.h"
#include "GPUParticleEditor.h"
#include "GPUParticleManager.h"
#include "ImguiManager.h"
#include "InputManager.h"
#include "LightEditor.h"
#include "LightManager.h"
#include "MeshEffectEditor.h"
#include "MeshEffectManager.h"
#include "Object3d.h"
#include "ParticleEditor.h"
#include "PostEffect.h"
#include "PostEffectEditor.h"
#include "ProfilerManager.h"
#include "RenderStats.h"
#include "ReplayDebugger.h"
#include "ProjectWindow.h"
#include "SceneManager.h"
#include "SceneWorkspace.h"
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
#include <cmath>
#include <filesystem>

namespace {
constexpr float kEditorViewportAspect = 16.0f / 9.0f;

// 利用可能領域の中央へ固定アスペクトの描画領域を収めます。
GameViewArea CalculateFixedAspectArea(const ImVec2& canvasScreenPos, const ImVec2& canvasSize) {
	GameViewArea area{};
	if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f) {
		return area;
	}

	float width = canvasSize.x;
	float height = width / kEditorViewportAspect;
	if (height > canvasSize.y) {
		height = canvasSize.y;
		width = height * kEditorViewportAspect;
	}

	area.screenX = canvasScreenPos.x + (canvasSize.x - width) * 0.5f;
	area.screenY = canvasScreenPos.y + (canvasSize.y - height) * 0.5f;
	area.width = width;
	area.height = height;
	return area;
}

// 固定アスペクト外の余白をレターボックスとして塗ります。
void DrawViewportLetterbox(const ImVec2& canvasScreenPos, const ImVec2& canvasSize) {
	ImGui::GetWindowDrawList()->AddRectFilled(
		canvasScreenPos,
		ImVec2(canvasScreenPos.x + canvasSize.x, canvasScreenPos.y + canvasSize.y),
		IM_COL32(14, 17, 22, 255));
}

// 選択中オブジェクトが現在シーンにまだ存在しているか確認する。

bool IsObjectInCurrentScene(SceneManager* sceneManager, Object3d* object) {
	if (!object) {
		return false;
	}
	BaseScene* currentScene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
	if (!currentScene) {
		return false;
	}

	auto& objects = currentScene->GetObjects();
	return std::any_of(objects.begin(), objects.end(), [object](const std::unique_ptr<Object3d>& sceneObject) {
		return sceneObject.get() == object;
	});
}
// 2Dベクトルの長さを計算する。

float Length2D(const ImVec2& value) {
	return std::sqrt(value.x * value.x + value.y * value.y);
}
// 2Dベクトルを正規化し、長さがほぼ0ならゼロベクトルを返す。

ImVec2 Normalize2D(const ImVec2& value) {
	const float length = Length2D(value);
	if (length <= 0.0001f) {
		return ImVec2(0.0f, 0.0f);
	}
	return ImVec2(value.x / length, value.y / length);
}
// 原点から方向ベクトルへ指定距離だけ進めた3D座標を返す。

Vector3 AddScaled(const Vector3& origin, const Vector3& direction, float scale) {
	return {
		origin.x + direction.x * scale,
		origin.y + direction.y * scale,
		origin.z + direction.z * scale,
	};
}
// 3軸スケールの中で最も大きい絶対値を返し、ギズモ表示サイズの基準にする。

float GetLargestAbsScale(const Vector3& scale) {
	return std::max(std::max(std::abs(scale.x), std::abs(scale.y)), std::abs(scale.z));
}
// 3Dワールド座標をゲームビュー上の2D座標へ投影する。

bool ProjectWorldToGameView(const Vector3& worldPos, const Matrix4x4& viewProjection, const GameViewArea& area, ImVec2& outScreen) {
	Math math;
	const Vector3 ndc = math.Transform(worldPos, viewProjection);
	if (ndc.z < 0.0f || ndc.z > 1.0f) {
		return false;
	}

	outScreen.x = area.screenX + (ndc.x + 1.0f) * 0.5f * area.width;
	outScreen.y = area.screenY + (1.0f - ndc.y) * 0.5f * area.height;
	return outScreen.x >= area.screenX - 32.0f && outScreen.x <= area.screenX + area.width + 32.0f &&
		   outScreen.y >= area.screenY - 32.0f && outScreen.y <= area.screenY + area.height + 32.0f;
}
// 2D線分の先端に矢印を付けて描画する。

void DrawArrow2D(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, ImU32 color, float thickness) {
	drawList->AddLine(start, end, color, thickness);

	const ImVec2 direction = Normalize2D(ImVec2(end.x - start.x, end.y - start.y));
	if (Length2D(direction) <= 0.0001f) {
		drawList->AddCircleFilled(end, thickness * 1.5f, color, 12);
		return;
	}

	const ImVec2 normal(-direction.y, direction.x);
	const float arrowLength = 8.0f;
	const float arrowWidth = 4.5f;
	const ImVec2 p1(end.x - direction.x * arrowLength + normal.x * arrowWidth, end.y - direction.y * arrowLength + normal.y * arrowWidth);
	const ImVec2 p2(end.x - direction.x * arrowLength - normal.x * arrowWidth, end.y - direction.y * arrowLength - normal.y * arrowWidth);
	drawList->AddTriangleFilled(end, p1, p2, color);
}
// 文字の視認性を上げるため、薄い縁取り付きでテキストを描画する。

void DrawTextWithOutline(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text) {
	const ImU32 outline = IM_COL32(20, 24, 32, 220);
	drawList->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), outline, text);
	drawList->AddText(ImVec2(pos.x - 1.0f, pos.y + 1.0f), outline, text);
	drawList->AddText(pos, color, text);
}
// エディタ画面右上に、現在カメラ基準のワールド軸ギズモを描画する。

void DrawSceneDirectionGizmo(const GameViewArea& area, Camera* camera) {
	if (!camera || area.width <= 1.0f || area.height <= 1.0f) {
		return;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	Math math;

	const ImVec2 center(area.screenX + area.width - 78.0f, area.screenY + 78.0f);
	const float radius = 48.0f;
	drawList->AddCircleFilled(center, radius, IM_COL32(24, 30, 38, 132), 48);
	drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 90), 48, 1.5f);
	DrawTextWithOutline(drawList, ImVec2(center.x - 39.0f, center.y - radius - 20.0f), IM_COL32(230, 240, 255, 230), "Scene Gizmo");

	struct AxisInfo {
		Vector3 worldAxis;
		const char* label;
		ImU32 color;
		float length;
	};

	const AxisInfo axes[] = {
		{ { 1.0f, 0.0f, 0.0f }, "+X 右", IM_COL32(255, 96, 96, 245), 35.0f },
		{ { 0.0f, 1.0f, 0.0f }, "+Y 上", IM_COL32(120, 235, 130, 245), 35.0f },
		{ { 0.0f, 0.0f, 1.0f }, "+Z 前", IM_COL32(95, 170, 255, 245), 40.0f },
	};

	const Matrix4x4& view = camera->GetViewMatrix();
	for (const AxisInfo& axis : axes) {
		const Vector3 viewDir = math.TransformNormal(axis.worldAxis, view);
		const ImVec2 screenDir = Normalize2D(ImVec2(viewDir.x, -viewDir.y));
		const bool nearlyFacingCamera = Length2D(screenDir) <= 0.0001f;
		const ImVec2 end(center.x + screenDir.x * axis.length, center.y + screenDir.y * axis.length);

		if (nearlyFacingCamera) {
			drawList->AddCircleFilled(center, 5.0f, axis.color, 16);
			DrawTextWithOutline(drawList, ImVec2(center.x + 8.0f, center.y - 7.0f), axis.color, axis.label);
			continue;
		}

		DrawArrow2D(drawList, center, end, axis.color, 2.4f);
		DrawTextWithOutline(drawList, ImVec2(end.x + 5.0f, end.y - 7.0f), axis.color, axis.label);
	}
}
// 選択オブジェクトのローカル軸をゲームビュー上に重ねて描画する。

void DrawSelectedObjectOrientation(const GameViewArea& area, Camera* camera, Object3d* selectedObject) {
	if (!camera || !selectedObject || area.width <= 1.0f || area.height <= 1.0f) {
		return;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	Math math;

	const Matrix4x4 viewProjection = camera->GetViewProjectionMatrix();
	const Matrix4x4& world = selectedObject->GetWorldMatrix();
	const Vector3 origin = selectedObject->GetWorldPosition();
	const float axisLength = std::clamp(GetLargestAbsScale(selectedObject->GetScale()) * 1.35f, 0.85f, 4.0f);

	ImVec2 originScreen;
	if (!ProjectWorldToGameView(origin, viewProjection, area, originScreen)) {
		return;
	}

	struct LocalAxisInfo {
		Vector3 localAxis;
		const char* label;
		ImU32 color;
		float lengthScale;
	};

	const LocalAxisInfo axes[] = {
		{ { 0.0f, 0.0f, 1.0f }, "正面 +Z", IM_COL32(255, 190, 70, 250), 1.15f },
		{ { 1.0f, 0.0f, 0.0f }, "右 +X", IM_COL32(255, 90, 90, 250), 1.0f },
		{ { 0.0f, 1.0f, 0.0f }, "上 +Y", IM_COL32(90, 235, 120, 250), 1.0f },
	};

	drawList->AddCircleFilled(originScreen, 4.0f, IM_COL32(255, 255, 255, 230), 16);
	drawList->AddCircle(originScreen, 6.0f, IM_COL32(32, 38, 48, 220), 16, 1.5f);

	for (const LocalAxisInfo& axis : axes) {
		Vector3 worldDirection = math.TransformNormal(axis.localAxis, world);
		worldDirection = math.Normalize(worldDirection);
		const Vector3 endpoint = AddScaled(origin, worldDirection, axisLength * axis.lengthScale);

		ImVec2 endpointScreen;
		if (!ProjectWorldToGameView(endpoint, viewProjection, area, endpointScreen)) {
			continue;
		}

		DrawArrow2D(drawList, originScreen, endpointScreen, axis.color, 3.0f);
		DrawTextWithOutline(drawList, ImVec2(endpointScreen.x + 6.0f, endpointScreen.y - 8.0f), axis.color, axis.label);
	}
}

}
// GameEditorControllerの既定コンストラクタ。メンバ生成はInitializeで行う。

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

	if (playModeChangeTracker_) {
		playModeChangeTracker_->CaptureRuntimeChanges();
	}
	const bool restored = replayDebugger_ && replayDebugger_->RestorePlayInEditorSnapshot();
	*activePlayState_ = false;
	if (sceneManager_) {
		sceneManager_->SetIsPlaying(false);
	}

	MeshEffectManager::GetInstance()->Clear();
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
}
// 各種エディタツールを逆順に解放し、DebugConsoleを終了する。

void GameEditorController::Finalize() {
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
    if (sceneWorkspace_) {
        sceneWorkspace_->Update();
        sceneWorkspace_->HandleHotkeys();
    }
    if (editorQuickSearch_) {
        editorQuickSearch_->HandleShortcut();
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
	if (wantsEffectPreviewTimeline && !showEffectPreviewTimeline_) {
		effectPreviewStage->EnableForToolPreview();
	} else if (!wantsEffectPreviewTimeline && showEffectPreviewTimeline_) {
		effectPreviewStage->ReturnToScene();
	}
	if (showEnemyAttackTimeline_ != wantsEnemyAttackTimeline ||
		showEffectPreviewTimeline_ != wantsEffectPreviewTimeline) {
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

void GameEditorController::ApplyWorkspacePreset(WorkspacePreset preset) {
    if (!debugEditor_) {
        return;
    }

    showDebugWindows_ = true;
    debugEditor_->SetSelectedObject(nullptr);
    EditorManager::GetInstance()->ClearSelection();

    IEditable* target = nullptr;
    switch (preset) {
    case WorkspacePreset::Terrain:
        SetReplayDebuggerVisible(false);
        target = debugEditor_->GetTerrainEditorWindow();
        break;
    case WorkspacePreset::Vfx:
        SetReplayDebuggerVisible(false);
        target = vfxSequencerEditor_.get();
        break;
    case WorkspacePreset::Animation:
        SetReplayDebuggerVisible(false);
        target = debugEditor_->GetAnimationWorkbench();
        break;
    case WorkspacePreset::Replay:
        SetReplayDebuggerVisible(true);
        break;
    }

    if (target) {
        EditorManager::GetInstance()->SetSelectedObject(target);
    }

    activeWorkspacePreset_ = static_cast<int>(preset);
    const char* key = "terrain";
    const char* displayName = "地形編集";
    switch (preset) {
    case WorkspacePreset::Vfx: key = "vfx"; displayName = "VFX編集"; break;
    case WorkspacePreset::Animation: key = "animation"; displayName = "アニメーション編集"; break;
    case WorkspacePreset::Replay: key = "replay"; displayName = "リプレイ解析"; break;
    default: break;
    }

    const std::filesystem::path layoutPath =
        std::filesystem::path("output/editor_state/workspaces") / (std::string(key) + ".ini");
    std::error_code error;
    pendingWorkspaceLayout_ = std::filesystem::exists(layoutPath, error)
        ? static_cast<int>(preset)
        : -1;
    dockspaceInitialized_ = false;
    workspaceStatus_ = std::string(displayName) +
        (pendingWorkspaceLayout_ >= 0 ? "レイアウトを復元します。" : "を既定配置で開きました。");
    DebugConsole::GetInstance()->AddLog("Workspace: " + workspaceStatus_);
}

void GameEditorController::SaveWorkspacePreset(WorkspacePreset preset) {
    const char* key = "terrain";
    const char* displayName = "地形編集";
    switch (preset) {
    case WorkspacePreset::Vfx: key = "vfx"; displayName = "VFX編集"; break;
    case WorkspacePreset::Animation: key = "animation"; displayName = "アニメーション編集"; break;
    case WorkspacePreset::Replay: key = "replay"; displayName = "リプレイ解析"; break;
    default: break;
    }

    const std::filesystem::path directory("output/editor_state/workspaces");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        workspaceStatus_ = "レイアウト保存先を作成できませんでした。";
        DebugConsole::GetInstance()->AddLog("Workspace Error: " + workspaceStatus_);
        return;
    }

    const std::filesystem::path layoutPath = directory / (std::string(key) + ".ini");
    ImGui::SaveIniSettingsToDisk(layoutPath.string().c_str());
    workspaceStatus_ = std::string(displayName) + "の現在レイアウトを保存しました。";
    DebugConsole::GetInstance()->AddLog("Workspace: " + workspaceStatus_);
}

void GameEditorController::ProcessPendingWorkspaceLayout() {
    if (pendingWorkspaceLayout_ < 0) {
        return;
    }

    const WorkspacePreset preset = static_cast<WorkspacePreset>(pendingWorkspaceLayout_);
    pendingWorkspaceLayout_ = -1;
    const char* key = "terrain";
    switch (preset) {
    case WorkspacePreset::Vfx: key = "vfx"; break;
    case WorkspacePreset::Animation: key = "animation"; break;
    case WorkspacePreset::Replay: key = "replay"; break;
    default: break;
    }

    const std::filesystem::path layoutPath =
        std::filesystem::path("output/editor_state/workspaces") / (std::string(key) + ".ini");
    std::error_code error;
    if (std::filesystem::exists(layoutPath, error) && !error) {
        ImGui::LoadIniSettingsFromDisk(layoutPath.string().c_str());
    }
}

void GameEditorController::DrawWorkspaceMenu() {
    if (!ImGui::BeginMenu(ICON_FA_COLUMNS " ワークスペース")) {
        return;
    }

    DrawEditorCommandMenuItem(
        EditorCommandId::WorkspaceTerrain, nullptr,
        activeWorkspacePreset_ == static_cast<int>(WorkspacePreset::Terrain));
    DrawEditorCommandMenuItem(
        EditorCommandId::WorkspaceVfx, nullptr,
        activeWorkspacePreset_ == static_cast<int>(WorkspacePreset::Vfx));
    DrawEditorCommandMenuItem(
        EditorCommandId::WorkspaceAnimation, nullptr,
        activeWorkspacePreset_ == static_cast<int>(WorkspacePreset::Animation));
    DrawEditorCommandMenuItem(
        EditorCommandId::WorkspaceReplay, nullptr,
        activeWorkspacePreset_ == static_cast<int>(WorkspacePreset::Replay));

    ImGui::Separator();
    ImGui::BeginDisabled(activeWorkspacePreset_ < 0);
    if (ImGui::MenuItem(ICON_FA_SAVE " 現在の配置をこのワークスペースへ保存")) {
        SaveWorkspacePreset(static_cast<WorkspacePreset>(activeWorkspacePreset_));
    }
    ImGui::EndDisabled();
    if (!workspaceStatus_.empty()) {
        ImGui::TextDisabled("%s", workspaceStatus_.c_str());
    }
    ImGui::EndMenu();
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
	const bool expandedBottomWorkspace = showReplayDebugger_ || showEnemyAttackTimeline_ || showEffectPreviewTimeline_;
	ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(
		dockMainId,
		ImGuiDir_Down,
		expandedBottomWorkspace ? 0.42f : 0.30f,
		nullptr,
		&dockMainId);

	ImGui::DockBuilderDockWindow("Hierarchy", dockLeftId);
	ImGui::DockBuilderDockWindow(ICON_FA_LIST_UL " Sprite Hierarchy", dockLeftId);
	ImGui::DockBuilderDockWindow("Inspector", dockRightId);
	ImGui::DockBuilderDockWindow(ICON_FA_INFO_CIRCLE " Sprite Inspector", dockRightId);
	if (showReplayDebugger_) {
		ImGui::DockBuilderDockWindow("リプレイデバッガー - Time Machine", dockBottomId);
		if (ImGuiDockNode* replayNode = ImGui::DockBuilderGetNode(dockBottomId)) {
			replayNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit;
		}
	} else if (showEnemyAttackTimeline_) {
		ImGui::DockBuilderDockWindow("敵攻撃プレビュー - Timeline", dockBottomId);
		if (ImGuiDockNode* timelineNode = ImGui::DockBuilderGetNode(dockBottomId)) {
			timelineNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit;
		}
	} else if (showEffectPreviewTimeline_) {
		ImGui::DockBuilderDockWindow("エフェクトプレビュー - Timeline", dockBottomId);
		if (ImGuiDockNode* timelineNode = ImGui::DockBuilderGetNode(dockBottomId)) {
			timelineNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit;
		}
	} else {
		ImGuiID dockBottomRightId = dockBottomId;
		ImGuiID dockBottomLeftId = ImGui::DockBuilderSplitNode(
			dockBottomId,
			ImGuiDir_Left,
			0.60f,
			nullptr,
			&dockBottomRightId);
		ImGui::DockBuilderDockWindow("Project (Assets)", dockBottomLeftId);
		ImGui::DockBuilderDockWindow(ICON_FA_FOLDER_OPEN " Sprite Assets", dockBottomLeftId);
		ImGui::DockBuilderDockWindow("Debug Console", dockBottomRightId);
		ImGui::DockBuilderDockWindow("ステータス", dockBottomRightId);
		ImGui::DockBuilderDockWindow("Camera Object Preview", dockBottomRightId);
	}
	ImGui::DockBuilderDockWindow("Game View", dockMainId);
	ImGui::DockBuilderFinish(dockspaceId);
}
// 編集中はScene View、再生中はGame Viewとして使う単一Viewportを描画する。

void DrawCameraObjectPreviewWindow();

EditorFrameState GameEditorController::DrawGameView(SceneManager* sceneManager, bool isPlaying) {
	EditorFrameState frameState;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Game View", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		const ImVec2 displaySize = ImGui::GetContentRegionAvail();
		const ImVec2 canvasScreenPos = ImGui::GetCursorScreenPos();

		if (displaySize.x > 0.0f && displaySize.y > 0.0f) {
			const bool sceneTransitioning = sceneManager && sceneManager->IsTransitioning();
			DrawViewportLetterbox(canvasScreenPos, displaySize);
			const GameViewArea area = CalculateFixedAspectArea(canvasScreenPos, displaySize);
			const uint32_t texHandle = PostEffect::GetInstance()->GetSRVHandle(1);
			const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(texHandle);
			ImGui::SetCursorScreenPos(ImVec2(area.screenX, area.screenY));
			ImGui::Image((ImTextureID)gpuHandle.ptr, ImVec2(area.width, area.height));
			const ImVec2 imageMin = ImGui::GetItemRectMin();
			const ImVec2 imageMax = ImGui::GetItemRectMax();
			const bool imageHovered = ImGui::IsItemHovered();
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			const bool canEditViewport = !isPlaying && !sceneTransitioning;

			if (canEditViewport) {
				HandleGameViewDropTargets(sceneManager, area);
			}

			if (debugEditor_) {
				debugEditor_->SetGameViewScreenRect(imageMin.x, imageMin.y, imageMax.x, imageMax.y);
				if (!sceneTransitioning) {
					debugEditor_->SetGameViewRegion({ area.screenX, area.screenY }, { area.width, area.height });
					debugEditor_->SetGameViewMousePos({ mousePos.x - area.screenX, mousePos.y - area.screenY });
					debugEditor_->SetGameViewHovered(canEditViewport && imageHovered);
					debugEditor_->Update();
					const bool openCreateMenu = ImGui::IsKeyPressed(ImGuiKey_Tab, false) ||
						ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
					if (canEditViewport && imageHovered && !ImGuizmo::IsOver() && openCreateMenu) {
						debugEditor_->OpenGameViewCreateContextMenu();
					}
					if (canEditViewport) {
						debugEditor_->DrawGameViewCreateContextMenu();
					}
					frameState.gizmoBusy = canEditViewport && ImGuizmo::IsUsing();
				}
				else {
					debugEditor_->SetGameViewHovered(false);
				}
			}

			if (spriteDebugEditor_ && canEditViewport) {
				const Vector2 spriteLocalPos = {
					(mousePos.x - area.screenX) * (static_cast<float>(WinApp::kClientWidth) / area.width),
					(mousePos.y - area.screenY) * (static_cast<float>(WinApp::kClientHeight) / area.height),
				};
				spriteDebugEditor_->Update(spriteLocalPos, imageHovered);
				frameState.spriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
			}

			if (canEditViewport) {
				DrawGhostPreview(false, area);
				Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
				Object3d* selectedObject = debugEditor_ ? debugEditor_->GetSelectedObject() : nullptr;
				if (!IsObjectInCurrentScene(sceneManager, selectedObject)) {
					selectedObject = nullptr;
				}
				CameraEditor* cameraEditor = CameraEditor::GetInstance();
				if (selectedObject && selectedObject->IsCameraObject()) {
					cameraEditor->SetSelectedCameraObject(selectedObject);
				}
				else if (EditorManager::GetInstance()->GetSelectedObject() != cameraEditor) {
					cameraEditor->SetSelectedCameraObject(nullptr);
				}
				DrawSelectedObjectOrientation(area, activeCamera, selectedObject);
				DrawSceneDirectionGizmo(area, activeCamera);
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();

	// パネルの大きさではなく、制作画面の16:9をカメラへ常に適用します。
	if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
		camera->SetAspectRatio(kEditorViewportAspect);
		camera->UpdateProjectionMatrix();
	}

	if (!isPlaying && !showReplayDebugger_ && (!sceneManager || !sceneManager->IsTransitioning())) {
		DrawCameraObjectPreviewWindow();
	}

	return frameState;
}

// 編集中のViewportへのドラッグ&ドロップを受け取り、SpriteやModelなどを配置する。

// 選択中のCamera Objectが実際に描画する画面を、通常のEditorウィンドウとして表示する。
void DrawCameraObjectPreviewWindow() {
	CameraEditor* cameraEditor = CameraEditor::GetInstance();
	Object3d* cameraObject = cameraEditor ? cameraEditor->GetSelectedCameraObject() : nullptr;
	if (!cameraObject || !cameraEditor->HasSceneCameraPreviewTarget()) {
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(560.0f, 390.0f), ImGuiCond_FirstUseEver);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	if (ImGui::Begin("Camera Object Preview", nullptr, flags)) {
		const SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
		const char* roleLabel = settings.role == SceneCameraRole::kMain ? "Main" : "Cinematic";
		constexpr float kRadToDeg = 180.0f / 3.14159265f;

		ImGui::TextColored(
			ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
			ICON_FA_VIDEO " %s",
			cameraObject->GetName().c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("[%s]  FOV %.1f°", roleLabel, settings.fovY * kRadToDeg);
		ImGui::Separator();

		if (ImGui::BeginChild(
			"CameraObjectPreviewCanvas",
			ImVec2(0.0f, 0.0f),
			true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
			if (canvasSize.x > 1.0f && canvasSize.y > 1.0f) {
				constexpr float kAspect = 16.0f / 9.0f;
				float previewWidth = canvasSize.x;
				float previewHeight = previewWidth / kAspect;
				if (previewHeight > canvasSize.y) {
					previewHeight = canvasSize.y;
					previewWidth = previewHeight * kAspect;
				}

				const ImVec2 canvasCursor = ImGui::GetCursorPos();
				const ImVec2 canvasScreenPos = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(
					canvasScreenPos,
					ImVec2(canvasScreenPos.x + canvasSize.x, canvasScreenPos.y + canvasSize.y),
					IM_COL32(18, 20, 25, 255));
				ImGui::SetCursorPos(ImVec2(
					canvasCursor.x + (canvasSize.x - previewWidth) * 0.5f,
					canvasCursor.y + (canvasSize.y - previewHeight) * 0.5f));

				const uint32_t textureHandle = PostEffect::GetInstance()->GetSRVHandle(
					PostEffect::kCinematicCameraPreviewTextureIndex);
				const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
					SRVManager::GetInstance()->GetGPUDescriptorHandle(textureHandle);
				ImGui::Image((ImTextureID)gpuHandle.ptr, ImVec2(previewWidth, previewHeight));
				cameraEditor->SetSceneCameraPreviewWindowVisible(ImGui::IsItemVisible());
			}
		}
		ImGui::EndChild();
	}
	ImGui::End();
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
		const std::string droppedModelName = ReadEditorAssetDragPath(payload->Data, payload->DataSize);
		if (debugEditor_ && !droppedModelName.empty()) {
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

	            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_ASSET")) {
                const char* prefabName = static_cast<const char*>(payload->Data);
                if (debugEditor_ && prefabName) {
                    debugEditor_->InstantiatePrefabAtCursor(prefabName);
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
// ゴーストレコーダーや選択オブジェクトの録画プレビューをゲームビュー上に描画する。

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
	else if (debugEditor_) {
		Object3d* selectedObject = debugEditor_->GetSelectedObject();
		SceneManager* editorSceneManager = debugEditor_->GetSceneManager();
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
		if (selectedObject && selectedObject->recorder_ && selectedObject->recorder_->HasPreviewData()) {
			selectedObject->recorder_->DrawPreview(
				camera->GetViewProjectionMatrix(),
				Vector2{ area.screenX, area.screenY },
				Vector2{ area.width, area.height },
				true);
		}
	}

	if (ghostDirector_ && EditorManager::GetInstance()->GetSelectedObject() == ghostDirector_.get()) {
		ghostDirector_->DrawPreview(
			camera->GetViewProjectionMatrix(),
			Vector2{ area.screenX, area.screenY },
			Vector2{ area.width, area.height });
	}
}
// 再生/停止、表示切替、シーン切替、ヘルプなどのメインメニューを描画する。

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

	if (ImGui::BeginMenu(ICON_FA_HISTORY " リプレイ")) {
		DrawEditorCommandMenuItem(EditorCommandId::ViewReplay, "下段Replay Editorを表示", showReplayDebugger_);
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
	MeshEffectManager::GetInstance()->Clear();
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
	const bool enemyAttackWorkspaceActive =
		showEnemyAttackTimeline_ &&
		debugEditor_ &&
		debugEditor_->GetEnemyAttackPreviewWindow();
	const bool effectPreviewWorkspaceActive = showEffectPreviewTimeline_ && debugEditor_;
	const bool expandedBottomWorkspace = replayWorkspaceActive || enemyAttackWorkspaceActive || effectPreviewWorkspaceActive;
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

	if (!expandedBottomWorkspace && showDebugConsole_) {
		DebugConsole::GetInstance()->DrawImGui();
	}
	if (!expandedBottomWorkspace && showTimeController_) {
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
	if (debugEditor_) {
		EnemyAttackPreviewWindow* enemyPreview = debugEditor_->GetEnemyAttackPreviewWindow();
		if (enemyPreview && enemyPreview->IsEnabled()) {
			enemyPreview->ApplyEffectPlaybackState();
		}
	}
	if (engineManualWindow_) {
		engineManualWindow_->Draw();
	}
	ProfilerManager::GetInstance()->DrawImGui();
    if (editorQuickSearch_) {
        editorQuickSearch_->Draw();
	GameplayEventTrace::GetInstance()->DrawImGui();
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
			if (selectedObject && selectedObject->recorder_ && selectedObject->recorder_->HasPreviewData()) {
				selectedObject->recorder_->DrawObjectGhostPreview(pointLightResource, spotLightResource);
			}
			debugEditor_->DrawPreview(pointLightResource, spotLightResource);
			if (BaseScene* currentScene = editorSceneManager ? editorSceneManager->GetCurrentScene() : nullptr) {
				CameraEditor* cameraEditor = CameraEditor::GetInstance();
				cameraEditor->SetObject3dCommon(currentScene->GetObject3dCommon());
				cameraEditor->DrawCameraModelGizmos(pointLightResource, spotLightResource);
			}
		}
	}

	SceneManager* editorSceneManager = debugEditor_ ? debugEditor_->GetSceneManager() : nullptr;
	const bool isPlaying = editorSceneManager && editorSceneManager->IsPlaying();
	if (!isPlaying && ghostRecorder_ && EditorManager::GetInstance()->GetSelectedObject() == ghostRecorder_.get()) {
		ghostRecorder_->SetSceneManager(editorSceneManager);
		ghostRecorder_->DrawObjectGhostPreview(pointLightResource, spotLightResource);
	}
}
// DebugEditorやMeshEffectEditorのデバッグ描画を実行する。

void GameEditorController::DrawSceneDebug(ID3D12GraphicsCommandList* commandList) {
	if (debugEditor_) {
		debugEditor_->DrawDebug(commandList);
	}
	if (meshEffectEditor_ && EditorManager::GetInstance()->GetSelectedObject() == meshEffectEditor_.get()) {
		meshEffectEditor_->Draw();
	}
}
// Sprite編集UIとImGui全体を最終バックバッファへ描画する。

void GameEditorController::DrawBackBufferUi() {
	if (spriteDebugEditor_) {
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
