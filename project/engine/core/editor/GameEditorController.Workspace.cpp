#include "engine/core/editor/GameEditorController.Internal.h"

#ifdef USE_IMGUI
using namespace GameEditorControllerImplementation;

// ワークスペース配置の保存と復元。
void GameEditorController::ApplyWorkspacePreset(WorkspacePreset preset) {
    if (!debugEditor_) {
        return;
    }

    showDebugWindows_ = true;
    // 前の対象選択を残すと、新しいワークスペースのInspectorに別用途の編集対象が混ざる。
    debugEditor_->SetSelectedObject(nullptr);
    EditorManager::GetInstance()->ClearSelection();

    // ワークスペースごとの主役となる編集対象と、専用のタイムライン表示を切り替える。
    IEditable* target = nullptr;
    switch (preset) {
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
    const char* key = "vfx";
    const char* displayName = "VFX編集";
    switch (preset) {
    case WorkspacePreset::Vfx: key = "vfx"; displayName = "VFX編集"; break;
    case WorkspacePreset::Animation: key = "animation"; displayName = "アニメーション編集"; break;
    case WorkspacePreset::Replay: key = "replay"; displayName = "リプレイ解析"; break;
    default: break;
    }

    // 保存済みiniがあるときだけ後続のフレームで復元し、なければ既定ドックを組み立てる。
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
    // 各用途のDockレイアウトを別名で保存し、切り替え時の配置を再現できるようにする。
    const char* key = "vfx";
    const char* displayName = "VFX編集";
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
    // メニュー操作の直後に読み込まず、DockSpaceの更新時点で保留していた配置を反映する。
    if (pendingWorkspaceLayout_ < 0) {
        return;
    }

    const WorkspacePreset preset = static_cast<WorkspacePreset>(pendingWorkspaceLayout_);
    pendingWorkspaceLayout_ = -1;
    const char* key = "vfx";
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
    // 保存先が決まるのは既存ワークスペースを選択している場合だけ。
    if (!ImGui::BeginMenu(ICON_FA_COLUMNS " ワークスペース")) {
        return;
    }

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

    // Asset一覧の拡大要求は、既存の上下分割を再利用できる場合だけ局所調整する。
    const bool expandAssets = PresetEditor::GetInstance()->ConsumeLibraryDockExpandRequest();
    if (expandAssets) {
        SetReplayDebuggerVisible(false);
    }
    if (dockspaceInitialized_ && expandAssets) {
        ImGuiWindow* project = ImGui::FindWindowByName("Project (Assets)");
        ImGuiWindow* console = ImGui::FindWindowByName("Debug Console");
        ImGuiDockNode* bottom = project ? project->DockNode : nullptr;
        // 旧配置の左右分割だけをまとめ、HierarchyとInspectorの幅は維持します。
        if (bottom && console && console->DockNode && bottom != console->DockNode &&
            bottom->ParentNode && bottom->ParentNode == console->DockNode->ParentNode) {
            bottom = bottom->ParentNode;
        }
        ImGuiWindow* gameView = ImGui::FindWindowByName("Game View");
        if (bottom && gameView && gameView->DockNode && bottom->ParentNode &&
            bottom->ParentNode == gameView->DockNode->ParentNode) {
            const ImGuiID bottomId = bottom->ID;
            ImGuiDockNode* parent = bottom->ParentNode;
            const float height = parent->Size.y * 0.50f;
            ImGui::DockBuilderRemoveNodeChildNodes(bottomId);
            bottom = ImGui::DockBuilderGetNode(bottomId);
            bottom->LocalFlags &= ~(ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit);
            ImGui::DockBuilderSetNodeSize(bottomId, ImVec2(parent->Size.x, height));
            ImGui::DockBuilderSetNodeSize(gameView->DockNode->ID,
                ImVec2(parent->Size.x, (std::max)(1.0f, parent->Size.y - height - ImGui::GetStyle().ItemSpacing.y)));
            ImGui::DockBuilderDockWindow("Project (Assets)", bottomId);
            ImGui::DockBuilderDockWindow(ICON_FA_FOLDER_OPEN " Sprite Assets", bottomId);
            ImGui::DockBuilderDockWindow("Debug Console", bottomId);
            ImGui::DockBuilderDockWindow("ステータス", bottomId);
            ImGui::DockBuilderDockWindow("Camera Object Preview", bottomId);
            ImGui::DockBuilderFinish(dockspaceId);
            ImGui::SetWindowFocus("Project (Assets)");
        } else {
            // Projectが浮動ウィンドウなどに移された場合は、既定の下部配置へ戻します。
            dockspaceInitialized_ = false;
        }
    }
    if (dockspaceInitialized_) {
        return;
    }

    // 再構築時はルートから作り直し、左右に編集パネル、下にタイムラインまたはAsset群を配置する。
	dockspaceInitialized_ = true;
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

	ImGuiID dockMainId = dockspaceId;
	ImGuiID dockLeftId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.22f, nullptr, &dockMainId);
	ImGuiID dockRightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
	ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(
		dockMainId,
		ImGuiDir_Down,
		expandAssets ? 0.50f : 0.42f,
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
        // Asset一覧は下部の横幅を使い、ログ・計測は同じ領域のタブで切り替えます。
        ImGui::DockBuilderDockWindow("Project (Assets)", dockBottomId);
        ImGui::DockBuilderDockWindow(ICON_FA_FOLDER_OPEN " Sprite Assets", dockBottomId);
        ImGui::DockBuilderDockWindow("Debug Console", dockBottomId);
        ImGui::DockBuilderDockWindow("ステータス", dockBottomId);
        ImGui::DockBuilderDockWindow("Camera Object Preview", dockBottomId);
	}
	ImGui::DockBuilderDockWindow("Game View", dockMainId);
	ImGui::DockBuilderFinish(dockspaceId);
}

// カメラプレビューは別の実装ファイルにあり、ここではDock先のウィンドウ名だけを使用する。
void DrawCameraObjectPreviewWindow();


#endif
