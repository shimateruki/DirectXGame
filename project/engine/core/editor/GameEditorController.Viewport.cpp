#include "engine/core/editor/GameEditorController.Internal.h"
#include <numbers>

#ifdef USE_IMGUI
#include "SelectedActorSession.h"
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
            const bool actorToolsActive = debugEditor_ &&
                ((debugEditor_->GetActorSession() && debugEditor_->GetActorSession()->IsActive()) ||
                 debugEditor_->GetAnimationClipEditor()->IsEnabled());

            if (canEditViewport && debugEditor_ && debugEditor_->GetActorSession())
                debugEditor_->GetActorSession()->DrawOverlay(area.screenX, area.screenY, area.width, area.height);

            // ドロップ処理より先に、このフレームの矩形とマウスを確定する。
            // Prefabも他の素材と同じ座標を使い、リサイズ時の前フレーム値を残さない。
            if (debugEditor_) {
                debugEditor_->SetGameViewRegion({area.screenX, area.screenY}, {area.width, area.height});
                debugEditor_->SetGameViewMousePos({mousePos.x - area.screenX, mousePos.y - area.screenY});
            }
			if (canEditViewport && !actorToolsActive) {
				HandleGameViewDropTargets(sceneManager, area);
			}

			if (debugEditor_) {
				debugEditor_->SetGameViewScreenRect(imageMin.x, imageMin.y, imageMax.x, imageMax.y);

				if (!sceneTransitioning) {
					debugEditor_->SetGameViewHovered(canEditViewport && imageHovered);

                    // 参照の整理は継続し、動作・クリップ確認中の配置入力を止めます。
                    // 再生時計はScene更新後のUpdateActorToolsで進めます。
                    debugEditor_->Update();
					const bool openCreateMenu = ImGui::IsKeyPressed(ImGuiKey_Tab, false) ||
						ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
					if (canEditViewport && !actorToolsActive && imageHovered && !ImGuizmo::IsOver() && openCreateMenu) {
						debugEditor_->OpenGameViewCreateContextMenu();
					}

					if (canEditViewport && !actorToolsActive) {
						debugEditor_->DrawGameViewCreateContextMenu();
					}

					frameState.gizmoBusy = canEditViewport && ImGuizmo::IsUsing();
				}
				else {
					debugEditor_->SetGameViewHovered(false);
				}
			}

            if (spriteDebugEditor_ && canEditViewport && !actorToolsActive && EditorToolSettings::Get().IsVisible("spriteGuides")) {
				const engine::Vector2 spriteLocalPos = {
					(mousePos.x - area.screenX) * (static_cast<float>(WinApp::GetClientWidth()) / area.width),
					(mousePos.y - area.screenY) * (static_cast<float>(WinApp::GetClientHeight()) / area.height),
				};

				spriteDebugEditor_->Update(spriteLocalPos, imageHovered);
				frameState.spriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
			}

			if (canEditViewport) {
				DrawGhostPreview(false, area);
				Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
				engine::Object3d* selectedObject = debugEditor_ && !actorToolsActive ? debugEditor_->GetSelectedObject() : nullptr;

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

                if (EditorToolSettings::Get().IsVisible("objectOrientation"))
                    DrawSelectedObjectOrientation(area, activeCamera, selectedObject);
                    if (EditorToolSettings::Get().IsVisible("direction"))
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

	if (!isPlaying && (!sceneManager || !sceneManager->IsTransitioning())) {
		DrawCameraObjectPreviewWindow();
	}

	return frameState;
}

// 編集中のViewportへのドラッグ&ドロップを受け取り、SpriteやModelなどを配置する。

// 選択中のCamera Objectが実際に描画する画面を、通常のEditorウィンドウとして表示する。
void DrawCameraObjectPreviewWindow() {
    // シーンカメラが選択され、プレビュー用の描画先がある間だけ専用ウィンドウを出す。
    if (!EditorToolSettings::Get().IsVisible("sceneCameraPreview"))
        return;
	CameraEditor* cameraEditor = CameraEditor::GetInstance();
	engine::Object3d* cameraObject = cameraEditor ? cameraEditor->GetSelectedCameraObject() : nullptr;

	if (!cameraObject || !cameraEditor->HasSceneCameraPreviewTarget()) {
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(560.0f, 390.0f), ImGuiCond_FirstUseEver);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	if (ImGui::Begin("Camera Object Preview", nullptr, flags)) {
		const SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
		const char* roleLabel = settings.role == SceneCameraRole::kMain ? "Main" : "Cinematic";
		constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;

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
				// 描画結果の16:9比率を保ち、余白はキャンバス中央へ均等に置く。
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
	// ドロップ座標はImGuiの画面座標なので、Sprite配置時だけゲームの設計解像度へ戻す。
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
		const char* droppedFilename = static_cast<const char*>(payload->Data);

		if (sceneManager && droppedFilename) {
			if (BaseScene* currentScene = sceneManager->GetCurrentScene()) {
				engine::SpriteCommon* spriteCommon = currentScene->GetSpriteCommon();

				ImVec2 mousePos = ImGui::GetIO().MousePos;
				engine::Vector2 dropPos = {
					(mousePos.x - area.screenX) * (static_cast<float>(WinApp::GetClientWidth()) / area.width),
					(mousePos.y - area.screenY) * (static_cast<float>(WinApp::GetClientHeight()) / area.height),
				};

				std::string fullPath = "Resources/sprite/" + std::string(droppedFilename);
				auto newSprite = std::make_unique<engine::Sprite>();
				uint32_t handle = TextureManager::GetInstance()->Load(fullPath);

				newSprite->Initialize(spriteCommon, handle);
				newSprite->SetName(droppedFilename);
				newSprite->SetTextureName(droppedFilename);
				newSprite->SetPosition(dropPos);

				engine::Sprite* addedSprite = currentScene->AddSprite(std::move(newSprite));

				if (spriteDebugEditor_) {
					spriteDebugEditor_->SetSelectedSprite(addedSprite);
				}

				DebugConsole::GetInstance()->AddLog("Dropped Sprite: " + std::string(droppedFilename));
			}
		}
	}

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
		// 3D素材はEditor側のレイキャスト配置へ渡し、Spriteとは別経路で位置を決める。
		const std::string droppedModelName = ReadEditorAssetDragPath(payload->Data, payload->DataSize);

		if (debugEditor_ && !droppedModelName.empty()) {
			debugEditor_->InstantiateModelAtCursor(droppedModelName);
		}
	}

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
		// 複数Objectを持つプリセットはルート位置をEditor側で決めてから一括生成する。
		const char* droppedPresetName = static_cast<const char*>(payload->Data);

		if (debugEditor_ && droppedPresetName) {
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
			engine::Vector2{ area.screenX, area.screenY },
			engine::Vector2{ area.width, area.height });
	}

	if (debugEditor_) {
		engine::Object3d* selectedObject = debugEditor_->GetSelectedObject();
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
				if (!object || object->GetRecordPathName().empty() || !object->GetRecorder())
					continue;
				object->GetRecorder()->SetSceneManager(editorSceneManager);
				if (!object->GetRecorder()->HasPreviewData())
					continue;
				object->GetRecorder()->DrawPreview(
					camera->GetViewProjectionMatrix(),
					engine::Vector2{ area.screenX, area.screenY },
					engine::Vector2{ area.width, area.height }, true);
			}
		}

		// Path未割当でも、選択中のObjectで編集中の経路は表示する。
		if (selectedObject && selectedObject->GetRecordPathName().empty() && selectedObject->GetRecorder()) {
			selectedObject->GetRecorder()->SetSceneManager(editorSceneManager);

			if (selectedObject->GetRecorder()->HasPreviewData()) {
				selectedObject->GetRecorder()->DrawPreview(
					camera->GetViewProjectionMatrix(),
					engine::Vector2{ area.screenX, area.screenY },
					engine::Vector2{ area.width, area.height }, true);
			}
		}
	}

	if (ghostDirector_ && EditorManager::GetInstance()->GetSelectedObject() == ghostDirector_.get()) {
		ghostDirector_->DrawPreview(
			camera->GetViewProjectionMatrix(),
			engine::Vector2{ area.screenX, area.screenY },
			engine::Vector2{ area.width, area.height });
	}
}

// 再生/停止、表示切替、シーン切替、ヘルプなどのメインメニューを描画する。


#endif
