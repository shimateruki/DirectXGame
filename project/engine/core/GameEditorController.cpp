#define NOMINMAX
#include "GameEditorController.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "Camera.h"
#include "CameraEditor.h"
#include "CaptureToolWindow.h"
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
#include "Object3d.h"
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
#include <cmath>

namespace {
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
// ゲームエディタで使う各種ツールとウィンドウを生成し、SceneManagerへ接続する。

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
	ghostDirector_->Initialize(sceneManager, debugEditor_.get());

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
// 各種エディタツールを逆順に解放し、DebugConsoleを終了する。

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
// ImGuiとImGuizmoのフレームを開始し、既定ドックスペースを準備する。

void GameEditorController::BeginFrame() {
	ImGuiManager::GetInstance()->BeginFrame();
	ImGuizmo::BeginFrame();
	SetupDefaultDockspace();
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
// 初回起動時にHierarchy、Inspector、Project、GameViewなどの既定ドック配置を作る。
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
	ImGui::DockBuilderDockWindow("Camera Object Preview", dockBottomRightId);
	ImGui::DockBuilderDockWindow("Game View", dockMainId);
	ImGui::DockBuilderFinish(dockspaceId);
}
// エディタ中央のゲームビューを描画し、マウス入力やギズモ状態を収集する。

void DrawCameraObjectPreviewWindow();

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
			const ImVec2 imageMin = ImGui::GetItemRectMin();
			const ImVec2 imageMax = ImGui::GetItemRectMax();

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
				debugEditor_->SetGameViewScreenRect(imageMin.x, imageMin.y, imageMax.x, imageMax.y);
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
			if (!isPlaying) {
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
	if (!isPlaying) {
		DrawCameraObjectPreviewWindow();
	}

	return frameState;
}
// ゲームビューへのドラッグ&ドロップを受け取り、SpriteやModelなどを配置する。

// 選択中のCamera Objectが実際に描画する画面を、通常のEditorウィンドウとして表示する。
void DrawCameraObjectPreviewWindow() {
	CameraEditor* cameraEditor = CameraEditor::GetInstance();
	Object3d* cameraObject = cameraEditor ? cameraEditor->GetSelectedCameraObject() : nullptr;
	if (!cameraObject || !cameraEditor->ShouldRenderSceneCameraPreview()) {
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
			RequestPlay(sceneManager, isPlaying, currentSceneName);
		}
	}
	if (previousPlayingState_ != isPlaying && !isPlaying) {
		MeshEffectManager::GetInstance()->Clear();
		DebrisEffectManager::GetInstance()->Clear();
		ClearSceneBoundEditorState();
		if (sceneManager) {
			sceneManager->ChangeScene(currentSceneName);
		}
		CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
	}
	previousPlayingState_ = isPlaying;

	ImGui::Text(isPlaying ? " | 実行中" : " | 編集モード");

	if (ImGui::BeginMenu("表示")) {
				if (ImGui::MenuItem("ポートフォリオ撮影モード", "F10", portfolioCaptureMode_)) {
			SetPortfolioCaptureMode(!portfolioCaptureMode_);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("ONにするとImGuiを描画せず、ゲーム画面だけを直接表示します。戻るときはF10です。");
		}
		ImGui::Separator();
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
				ClearSceneBoundEditorState();
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
	DrawUnsavedPlayConfirmPopup(sceneManager, isPlaying, currentSceneName);
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
// 再生開始要求時に未保存変更があれば確認し、問題なければ再生を開始する。

void GameEditorController::RequestPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName) {
	if (HasUnsavedEditorChanges()) {
		openUnsavedPlayConfirm_ = true;
		return;
	}

	StartPlay(sceneManager, isPlaying, currentSceneName);
}
// シーンを再読み込みしてエディタ状態を片付け、ゲーム再生状態へ切り替える。

void GameEditorController::StartPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName) {
	ClearSceneBoundEditorState();
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
// シーンに紐づく選択状態やゴースト対象をクリアし、再読み込み後の不整合を防ぐ。

void GameEditorController::ClearSceneBoundEditorState() {
	if (ghostRecorder_) {
		ghostRecorder_->ClearTarget();
	}
	if (debugEditor_) {
		debugEditor_->SetSelectedObject(nullptr);
	}
	EditorManager::GetInstance()->ClearSelection();
}
// 未保存変更がある状態で再生しようとした時の確認ポップアップを描画する。

void GameEditorController::DrawUnsavedPlayConfirmPopup(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName) {
	const char* popupName = "未保存の変更があります###UnsavedPlayConfirm";
	if (openUnsavedPlayConfirm_) {
		ImGui::OpenPopup(popupName);
		openUnsavedPlayConfirm_ = false;
	}

	ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.32f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " この変更はまだ保存されていません。");
	ImGui::Spacing();
	ImGui::TextWrapped("保存されていない変更があります。このまま開始すると、プレイ用にシーンを再読み込みするため、未保存の編集内容が失われる可能性があります。");
	if (debugEditor_) {
		ImGui::Spacing();
		ImGui::TextDisabled("未保存: %s", debugEditor_->GetDirtySummaryText().c_str());
	}
	ImGui::Separator();

	if (ImGui::Button(ICON_FA_PLAY " 保存せず開始", ImVec2(170.0f, 0.0f))) {
		DebugConsole::GetInstance()->AddLog("Play Warning: 未保存の変更を破棄して開始しました。");
		StartPlay(sceneManager, isPlaying, currentSceneName);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_TIMES " キャンセル", ImVec2(130.0f, 0.0f))) {
		DebugConsole::GetInstance()->AddLog("Play Warning: 未保存のため開始をキャンセルしました。");
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
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

	if (!sceneTransitioning) {
		if (ghostDirector_) {
			ghostDirector_->Update(isPlaying ? deltaTime * timeScale : deltaTime);
		}
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
	}
	if (debugEditor_ && debugEditor_->GetCaptureToolWindow()) {
		debugEditor_->GetCaptureToolWindow()->SetForceGameViewClientCapture(portfolioCaptureMode_);
		debugEditor_->GetCaptureToolWindow()->UpdateHotkeys();
	}

	if (isPlaying && !sceneTransitioning) {
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

	ImGui::End();
}
// ProjectWindowから要求されたサムネイル撮影を処理する。

void GameEditorController::CapturePendingThumbnails() {
	if (debugEditor_ && debugEditor_->GetProjectWindow()) {
		debugEditor_->GetProjectWindow()->CapturePendingThumbnails();
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
}
// ギズモやSprite編集中はカメラ入力を止め、操作の競合を防ぐ。

void GameEditorController::ApplyCameraInputState(const EditorFrameState& frameState, bool isPlaying) {
	if (Camera* mainCamera = CameraManager::GetInstance()->GetActiveCamera()) {
		mainCamera->SetInputEnabled(isPlaying || !(frameState.spriteEditorBusy || frameState.gizmoBusy));
	}
}
// エフェクトプレビューやアニメーション作業台のカメラ上書きを現在フレームへ反映する。

void GameEditorController::ApplyCameraOverrides() {
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
