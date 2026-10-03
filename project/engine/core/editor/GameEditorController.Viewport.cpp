#include "engine/core/editor/GameEditorController.Internal.h"

#ifdef USE_IMGUI
using namespace GameEditorControllerImplementation;

// プレビュー描画から呼び出す、同じ実装ファイル内の補助処理です。
void DrawCameraObjectPreviewWindow();

// ゲームビューとプレビューの表示・操作。
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

            if (spriteDebugEditor_ && canEditViewport && EditorToolSettings::Get().IsVisible("spriteGuides")) {
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
                if (EditorToolSettings::Get().IsVisible("objectOrientation")) DrawSelectedObjectOrientation(area, activeCamera, selectedObject);
                    if (EditorToolSettings::Get().IsVisible("direction")) DrawSceneDirectionGizmo(area, activeCamera);
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
    if (!EditorToolSettings::Get().IsVisible("sceneCameraPreview")) return;
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
    if (isPlaying || !EditorToolSettings::Get().IsVisible("ghostPath")) {
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
	if (debugEditor_) {
		Object3d* selectedObject = debugEditor_->GetSelectedObject();
		SceneManager* editorSceneManager = debugEditor_->GetSceneManager();
		if (selectedObject && !IsObjectInCurrentScene(editorSceneManager, selectedObject)) {
			debugEditor_->SetSelectedObject(nullptr);
			if (EditorManager::GetInstance()->GetSelectedObject() == debugEditor_.get()) {
				EditorManager::GetInstance()->ClearSelection();
			}
			selectedObject = nullptr;
		}
		if (editorSceneManager && editorSceneManager->GetCurrentScene()) {
			for (const auto& object : editorSceneManager->GetCurrentScene()->GetObjects()) {
				if (!object || object->GetRecordPathName().empty() || !object->recorder_) continue;
				object->recorder_->SetSceneManager(editorSceneManager);
				if (!object->recorder_->HasPreviewData()) continue;
				object->recorder_->DrawPreview(
					camera->GetViewProjectionMatrix(),
					Vector2{ area.screenX, area.screenY },
					Vector2{ area.width, area.height }, true);
			}
		}
		// Path未割当でも、選択中のObjectで編集中の経路は表示する。
		if (selectedObject && selectedObject->GetRecordPathName().empty() && selectedObject->recorder_) {
			selectedObject->recorder_->SetSceneManager(editorSceneManager);
			if (selectedObject->recorder_->HasPreviewData()) {
				selectedObject->recorder_->DrawPreview(
					camera->GetViewProjectionMatrix(),
					Vector2{ area.screenX, area.screenY },
					Vector2{ area.width, area.height }, true);
			}
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


#endif
