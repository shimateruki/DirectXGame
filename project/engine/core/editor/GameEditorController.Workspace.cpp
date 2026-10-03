#include "engine/core/editor/GameEditorController.Internal.h"

#ifdef USE_IMGUI
using namespace GameEditorControllerImplementation;

// ワークスペース配置の保存と復元。
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
	const bool expandedBottomWorkspace = showReplayDebugger_ || showEnemyAttackTimeline_ || showEffectPreviewTimeline_ ||
        showAnimationTimeline_ || showCinematicTimeline_;
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
        ImGui::DockBuilderDockWindow("攻撃・能力プレビュー - Timeline", dockBottomId);
		if (ImGuiDockNode* timelineNode = ImGui::DockBuilderGetNode(dockBottomId)) {
			timelineNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit;
		}
	} else if (showEffectPreviewTimeline_) {
		ImGui::DockBuilderDockWindow("エフェクトプレビュー - Timeline", dockBottomId);
		if (ImGuiDockNode* timelineNode = ImGui::DockBuilderGetNode(dockBottomId)) {
			timelineNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit;
		}
	} else if (showAnimationTimeline_ || showCinematicTimeline_) {
        ImGui::DockBuilderDockWindow(showAnimationTimeline_ ?
            "アニメーション制作 - Timeline" : "演出制作 - Timeline", dockBottomId);
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockBottomId)) {
            node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit;
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


#endif
