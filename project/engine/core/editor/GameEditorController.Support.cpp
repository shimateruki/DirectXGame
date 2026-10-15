#include "engine/core/editor/GameEditorController.Internal.h"

#ifdef USE_IMGUI
using namespace GameEditorControllerImplementation;

// 補助処理の定義を一か所に保持し、共有状態と呼び出し順を維持します。
namespace GameEditorControllerImplementation {
// キャンバス内へ固定縦横比のGame Viewを収め、余白を均等に配分します。
GameViewArea CalculateFixedAspectArea(const ImVec2& canvasScreenPos, const ImVec2& canvasSize) {
	// キャンバス内に固定アスペクト比のゲーム画面を収め、余白を左右または上下へ均等に配分する。
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

// Game Viewの余白を暗色で塗り、描画領域の境界を明確にします。
void DrawViewportLetterbox(const ImVec2& canvasScreenPos, const ImVec2& canvasSize) {
	ImGui::GetWindowDrawList()->AddRectFilled(
		canvasScreenPos,
		ImVec2(canvasScreenPos.x + canvasSize.x, canvasScreenPos.y + canvasSize.y),
		IM_COL32(14, 17, 22, 255));
}

// 借用中のObjectが現在のシーンにまだ所有されているか確認します。
bool IsObjectInCurrentScene(SceneManager* sceneManager, engine::Object3d* object) {
	// 前のシーンのポインターが残っていても編集対象にしないよう、現シーンの所有物だけを受け付ける。
	if (!object) {
		return false;
	}
	BaseScene* currentScene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
	if (!currentScene) {
		return false;
	}

	auto& objects = currentScene->GetObjects();
	return std::any_of(objects.begin(), objects.end(), [object](const std::unique_ptr<engine::Object3d>& sceneObject) {
		return sceneObject.get() == object;
	});
}

// Game Viewの二次元方向ベクトルの長さを求めます。
float Length2D(const ImVec2& value) {
	return std::sqrt(value.x * value.x + value.y * value.y);
}

// ほぼゼロの方向はゼロのまま返し、それ以外は単位方向に直します。
ImVec2 Normalize2D(const ImVec2& value) {
	const float length = Length2D(value);
	if (length <= 0.0001f) {
		return ImVec2(0.0f, 0.0f);
	}
	return ImVec2(value.x / length, value.y / length);
}

engine::Vector3 AddScaled(const engine::Vector3& origin, const engine::Vector3& direction, float scale) {
	return {
		origin.x + direction.x * scale,
		origin.y + direction.y * scale,
		origin.z + direction.z * scale,
	};
}

float GetLargestAbsScale(const engine::Vector3& scale) {
	return std::max(std::max(std::abs(scale.x), std::abs(scale.y)), std::abs(scale.z));
}

// ワールド位置をGame Viewへ投影します。画面外ならfalseを返します。
bool ProjectWorldToGameView(const engine::Vector3& worldPos, const engine::Matrix4x4& viewProjection, const GameViewArea& area, ImVec2& outScreen) {
	// ワールド座標をゲーム画面の固定比率領域へ投影し、画面外なら選択表示を省く。
	engine::Math math;
	const engine::Vector3 ndc = math.Transform(worldPos, viewProjection);
	if (ndc.z < 0.0f || ndc.z > 1.0f) {
		return false;
	}

	outScreen.x = area.screenX + (ndc.x + 1.0f) * 0.5f * area.width;
	outScreen.y = area.screenY + (1.0f - ndc.y) * 0.5f * area.height;
	return outScreen.x >= area.screenX - 32.0f && outScreen.x <= area.screenX + area.width + 32.0f &&
		   outScreen.y >= area.screenY - 32.0f && outScreen.y <= area.screenY + area.height + 32.0f;
}

// 指定線分の終点に三角形の矢印を描きます。
void DrawArrow2D(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, ImU32 color, float thickness) {
	// 線分の終点へ三角形を描き、長さがほぼゼロなら点として表示する。
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

// 軸ラベルを暗い縁取り付きで描き、背景上でも読めるようにします。
void DrawTextWithOutline(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text) {
	const ImU32 outline = IM_COL32(20, 24, 32, 220);
	drawList->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), outline, text);
	drawList->AddText(ImVec2(pos.x - 1.0f, pos.y + 1.0f), outline, text);
	drawList->AddText(pos, color, text);
}

// カメラ方向からワールド軸を求め、Game Viewの方位ギズモへ描画します。
void DrawSceneDirectionGizmo(const GameViewArea& area, Camera* camera) {
	if (!camera || area.width <= 1.0f || area.height <= 1.0f) {
		return;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	engine::Math math;

	const ImVec2 center(area.screenX + area.width - 78.0f, area.screenY + 78.0f);
	const float radius = 48.0f;
	drawList->AddCircleFilled(center, radius, IM_COL32(24, 30, 38, 132), 48);
	drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 90), 48, 1.5f);
	DrawTextWithOutline(drawList, ImVec2(center.x - 39.0f, center.y - radius - 20.0f), IM_COL32(230, 240, 255, 230), "Scene Gizmo");

	// 画面隅の方位表示用。worldAxis をビュー空間へ投影し、length は画面上の軸長に使う。
	struct AxisInfo {
		engine::Vector3 worldAxis;
		const char* label;
		ImU32 color;
		float length;
	};

	const AxisInfo axes[] = {
		{ { 1.0f, 0.0f, 0.0f }, "+X 右", IM_COL32(255, 96, 96, 245), 35.0f },
		{ { 0.0f, 1.0f, 0.0f }, "+Y 上", IM_COL32(120, 235, 130, 245), 35.0f },
		{ { 0.0f, 0.0f, 1.0f }, "+Z 前", IM_COL32(95, 170, 255, 245), 40.0f },
	};

	const engine::Matrix4x4& view = camera->GetViewMatrix();
	for (const AxisInfo& axis : axes) {
		const engine::Vector3 viewDir = math.TransformNormal(axis.worldAxis, view);
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

// 選択Objectの向きをGame View上に投影して表示します。
void DrawSelectedObjectOrientation(const GameViewArea& area, Camera* camera, engine::Object3d* selectedObject) {
	if (!camera || !selectedObject || area.width <= 1.0f || area.height <= 1.0f) {
		return;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	engine::Math math;

	const engine::Matrix4x4 viewProjection = camera->GetViewProjectionMatrix();
	const engine::Matrix4x4& world = selectedObject->GetWorldMatrix();
	const engine::Vector3 origin = selectedObject->GetWorldPosition();
	const float axisLength = std::clamp(GetLargestAbsScale(selectedObject->GetScale()) * 1.35f, 0.85f, 4.0f);

	ImVec2 originScreen;
	if (!ProjectWorldToGameView(origin, viewProjection, area, originScreen)) {
		return;
	}

	// 選択 Object のローカル軸をワールドへ変換して表示するための色・長さ倍率。
	struct LocalAxisInfo {
		engine::Vector3 localAxis;
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
		engine::Vector3 worldDirection = math.TransformNormal(axis.localAxis, world);
		worldDirection = math.Normalize(worldDirection);
		const engine::Vector3 endpoint = AddScaled(origin, worldDirection, axisLength * axis.lengthScale);

		ImVec2 endpointScreen;
		if (!ProjectWorldToGameView(endpoint, viewProjection, area, endpointScreen)) {
			continue;
		}

		DrawArrow2D(drawList, originScreen, endpointScreen, axis.color, 3.0f);
		DrawTextWithOutline(drawList, ImVec2(endpointScreen.x + 6.0f, endpointScreen.y - 8.0f), axis.color, axis.label);
	}
}
}

#endif
