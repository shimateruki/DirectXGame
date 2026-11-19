#include "engine/editor/core/GameEditorController.Internal.h"
#include <numbers>
#include <format>

#ifdef USE_IMGUI
#include "SelectedActorSession.h"
#include "game/ui/SettingsMenuOverlay.h"
using namespace GameEditorControllerImplementation;

// プレビュー描画から呼び出す、同じ実装ファイル内の補助処理です。
void DrawCameraObjectPreviewWindow();

namespace {

// Game Viewの画像の内側にラベルを収め、隣接するEditorパネルに重ねない。
void DrawCameraLabel(const GameViewArea& area, ImVec2 anchor, const char* text, ImU32 accent) {
    constexpr float padding = 6.0f;
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float width = (std::min)(area.width, textSize.x + padding * 2.0f);
    const float height = textSize.y + padding * 2.0f;
    if (width <= padding * 2.0f || area.height < height) return;
    // ラベルの左上は、幅と高さを引いた分までしか動かせない。画像の右端や下端からはみ出さない位置へ丸める。
    const ImVec2 min(
        std::clamp(anchor.x, area.screenX, area.screenX + (area.width - width)),
        std::clamp(anchor.y, area.screenY, area.screenY + (area.height - height)));
    const ImVec2 max(min.x + width, min.y + height);
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, IM_COL32(18, 23, 30, 224), 4.0f);
    drawList->AddRectFilled(min, ImVec2(min.x + 3.0f, max.y), accent, 2.0f);
    drawList->PushClipRect(min, max, true);
    drawList->AddText(ImVec2(min.x + padding, min.y + padding), IM_COL32(244, 247, 250, 255), text);
    drawList->PopClipRect();
}

// 範囲の天面中央を投影し、選択したCamera Objectの名前と優先度を表示する。
void DrawSelectedCameraRegionLabel(const GameViewArea& area, engine::Camera* viewCamera,
    const engine::Object3D& object) {
    const auto& settings = object.GetSceneCameraSettings();
    if (!viewCamera || !object.IsCameraObject() || !object.GetIsVisible() || !settings.region.enabled) return;
    const auto& region = settings.region;
    // 範囲のY中心に半分の高さを足し、ワールド座標で天面中央の目印を作る。
    const auto labelAnchor = region.center + engine::Vector3{0, region.size.y * 0.5f, 0};
    const auto viewProjection = viewCamera->GetViewProjectionMatrix();
    ImVec2 screen;
    const bool anchorVisible = ProjectWorldToGameView(labelAnchor, viewProjection, area, screen) &&
        screen.x >= area.screenX && screen.x <= area.screenX + area.width &&
        screen.y >= area.screenY && screen.y <= area.screenY + area.height;
    // 大きな範囲の天面が画面外でも、選択したカメラと優先度は確認できる。
    const ImVec2 labelPosition = anchorVisible
        ? ImVec2(screen.x + 6.0f, screen.y - ImGui::GetTextLineHeight() - 12.0f)
        : ImVec2(area.screenX + 10.0f, area.screenY + 10.0f);
    const std::string label = std::format("{}  |  優先度 {}{}", object.GetName(), region.priority,
        settings.enabled ? "" : "  |  無効");
    const auto& color = region.color;
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w));
    DrawCameraLabel(area, labelPosition, label.c_str(), accent);
}

}

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
			// パネル内に縦横比固定の画像矩形を作る。以降の入力やラベルの基準は黒帯を除いた画像の領域。
			const GameViewArea area = CalculateFixedAspectArea(canvasScreenPos, displaySize);
			const uint32_t texHandle = PostEffect::GetInstance()->GetSrvHandle(1);
			const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SrvManager::GetInstance()->GetGpuDescriptorHandle(texHandle);
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
				// ImGuiの画面座標から画像の左上を引き、表示倍率を戻してゲーム側のピクセル座標に換算する。
				const engine::Vector2 spriteLocalPos = {
					(mousePos.x - area.screenX) * (static_cast<float>(WinApp::GetClientWidth()) / area.width),
					(mousePos.y - area.screenY) * (static_cast<float>(WinApp::GetClientHeight()) / area.height),
				};

				spriteDebugEditor_->Update(spriteLocalPos, imageHovered);
				frameState.spriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
			}

			if (canEditViewport) {
				DrawGhostPreview(false, area);
				engine::Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
				engine::Object3D* selectedObject = debugEditor_ && !actorToolsActive ? debugEditor_->GetSelectedObject() : nullptr;

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

                if (selectedObject && !SettingsMenuOverlay::IsAnyActive() &&
                    EditorToolSettings::Get().IsVisible("cameraGuide") &&
                    *cameraEditor->GetCameraGuideVisiblePtr()) {
                    DrawSelectedCameraRegionLabel(area, activeCamera, *selectedObject);
                }

                if (EditorToolSettings::Get().IsVisible("objectOrientation"))
                    DrawSelectedObjectOrientation(area, activeCamera, selectedObject);
                    if (EditorToolSettings::Get().IsVisible("direction"))
                        DrawSceneDirectionGizmo(area, activeCamera);
			}
            if (isPlaying && !sceneTransitioning && !SettingsMenuOverlay::IsAnyActive() &&
                EditorToolSettings::Get().IsVisible("cameraGuide")) {
                const auto& status = CameraManager::GetInstance()->GetRegionStatus();
                DrawCameraLabel(area, ImVec2(area.screenX + 10.0f, area.screenY + 10.0f),
                    status.c_str(), IM_COL32(110, 206, 244, 255));
            }
		}
	}

	ImGui::End();
	ImGui::PopStyleVar();

	// パネルの大きさではなく、制作画面の16:9をカメラへ常に適用します。
	if (engine::Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
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
	engine::Object3D* cameraObject = cameraEditor ? cameraEditor->GetSelectedCameraObject() : nullptr;

	if (!cameraObject || !cameraEditor->HasSceneCameraPreviewTarget()) {
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(560.0f, 390.0f), ImGuiCond_FirstUseEver);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	if (ImGui::Begin("Camera Object Preview", nullptr, flags)) {
		const SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
		const char* roleLabel = settings.role == SceneCameraRole::Main ? "Main" : "Cinematic";
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

				// Game View と同じ SRV ではなく、選択カメラで別途描画したプレビュー専用テクスチャを表示する。
				const uint32_t textureHandle = PostEffect::GetInstance()->GetSrvHandle(
					PostEffect::kCinematicCameraPreviewTextureIndex);
				const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
					SrvManager::GetInstance()->GetGpuDescriptorHandle(textureHandle);
				ImGui::Image((ImTextureID)gpuHandle.ptr, ImVec2(previewWidth, previewHeight));
				// 折り畳みやクリップで画像が見えないときは、CameraEditor へ表示不要と通知する。
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
				// 画像左上からの比率にゲーム画面の幅と高さを掛ける。縮小表示でドロップしても実際のSprite位置を同じ比率にする。
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

                // Prefab 内の親子構造を保ったまま複製する入口へ渡す。座標は同じフレームの Game View のドロップ位置を使う。
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

	engine::Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

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
		engine::Object3D* selectedObject = debugEditor_->GetSelectedObject();
		SceneManager* editorSceneManager = debugEditor_->GetSceneManager();

		// シーン切り替えで無効になった選択参照を描画前に外し、旧録画器へアクセスするのを防ぐ。
		if (selectedObject && !IsObjectInCurrentScene(editorSceneManager, selectedObject)) {
			debugEditor_->SetSelectedObject(nullptr);

			if (EditorManager::GetInstance()->GetSelectedObject() == debugEditor_.get()) {
				EditorManager::GetInstance()->ClearSelection();
			}

			selectedObject = nullptr;
		}

		// 経路アセットを割り当て済みのObjectは選択されていなくても、それぞれの録画器のプレビューを重ねる。
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

	// Director を操作しているときは、録画器個別の経路に加えて編集中の演出プレビューを重ねる。
	if (ghostDirector_ && EditorManager::GetInstance()->GetSelectedObject() == ghostDirector_.get()) {
		ghostDirector_->DrawPreview(
			camera->GetViewProjectionMatrix(),
			engine::Vector2{ area.screenX, area.screenY },
			engine::Vector2{ area.width, area.height });
	}
}

// 再生/停止、表示切替、シーン切替、ヘルプなどのメインメニューを描画する。


#endif
