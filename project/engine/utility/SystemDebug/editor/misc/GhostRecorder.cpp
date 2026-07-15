#define NOMINMAX
#include "GhostRecorder.h"
#include "AnimationInterpolation.h"
#include "imgui.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "CameraManager.h"
#include "Camera.h"
#include <fstream>
#include "json.hpp"
#include <cmath>
#include <algorithm>
#include "CameraEditor.h"
#include "DebugConsole.h"
#include "Easing.h"
#include "ImGuizmo.h"
#include "IconsFontAwesome5.h"
using json = nlohmann::json;

namespace {

bool IsObjectInScene(SceneManager* sceneManager, Object3d* object) {
	if (!object) {
		return false;
	}
	if (!sceneManager || !sceneManager->GetCurrentScene()) {
		return true;
	}

	for (const auto& sceneObject : sceneManager->GetCurrentScene()->GetObjects()) {
		if (sceneObject.get() == object) {
			return true;
		}
	}
	return false;
}

}

// 親回転へローカル回転を合成する。
Vector3 AddEuler(const Vector3& eulerBase, const Vector3& eulerAdd) {
	Quaternion qBase = Math::EulerToQuaternion(eulerBase);
	Quaternion qAdd = Math::EulerToQuaternion(eulerAdd);
	Quaternion qResult = qAdd * qBase;
	return Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(qResult));
}

// ワールド回転から基準回転を除き、ローカル回転を求める。
Vector3 SubEuler(const Vector3& eulerCurrent, const Vector3& eulerBase) {
	Quaternion qBase = Math::EulerToQuaternion(eulerBase);
	Quaternion qCurrent = Math::EulerToQuaternion(eulerCurrent);
	Quaternion qBaseInv = { -qBase.x, -qBase.y, -qBase.z, qBase.w };
	Quaternion qOffset = qCurrent * qBaseInv;
	return Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(qOffset));
}

float ApplyEasing(int type, float t) {
	switch (type) {
	case 0: return Easing::Linear(t);
	case 1: return Easing::InSine(t);
	case 2: return Easing::OutSine(t);
	case 3: return Easing::InOutSine(t);
	case 4: return Easing::InQuad(t);
	case 5: return Easing::OutQuad(t);
	case 6: return Easing::InOutQuad(t);
	case 7: return Easing::InCubic(t);
	case 8: return Easing::OutCubic(t);
	case 9: return Easing::InOutCubic(t);
	case 10: return Easing::InQuart(t);
	case 11: return Easing::OutQuart(t);
	case 12: return Easing::InOutQuart(t);
	case 13: return Easing::InQuint(t);
	case 14: return Easing::OutQuint(t);
	case 15: return Easing::InOutQuint(t);
	case 16: return Easing::InExpo(t);
	case 17: return Easing::OutExpo(t);
	case 18: return Easing::InOutExpo(t);
	case 19: return Easing::InCirc(t);
	case 20: return Easing::OutCirc(t);
	case 21: return Easing::InOutCirc(t);
	default: return Easing::Linear(t);
	}
}
// ==========================================================================
// 初期化・基本機能
// ==========================================================================

void GhostRecorder::Initialize(SceneManager* sceneManager) {
	sceneManager_ = sceneManager;
	target_ = nullptr;
	anchor_ = nullptr;
	anchorName_.clear();
	frames_.clear();
	undoStack_.clear();
	redoStack_.clear();
	state_ = State::Idle;

	isLoop_ = false;
	isRelative_ = true;
	currentFrameIndex_ = 0;

	genParams_ = GenerationParams();
	isShowPreview_ = true; // デフォルトで表示
	isOverrideCamera_ = false;
	isScrubbing_ = false;
	isDraggingGizmo_ = false;
	DeselectPin();
}

void GhostRecorder::SetTarget(Object3d* target) {
	target_ = target;
	anchor_ = nullptr;
	DeselectPin();
	isScrubbing_ = false;
}

void GhostRecorder::ClearTarget() {
	Stop(false);
	target_ = nullptr;
	anchor_ = nullptr;
	DeselectPin();
	isScrubbing_ = false;
}

int GhostRecorder::GetCurrentEventID() const {
	if (state_ == State::Playing && currentFrameIndex_ < frames_.size()) {
		return frames_[currentFrameIndex_].eventID;
	}
	return 0;
}

void GhostRecorder::DeselectPin() {
	selectedPinType_ = SelectedPinType::None;
	selectedWaypointIndex_ = -1;
}

void GhostRecorder::PlayFromMemory(bool loop, bool isCinematic) {
	if (frames_.empty()) {
		return;
	}
	isLoop_ = loop;
	isOverrideCamera_ = isCinematic;
	if (target_) {
		target_->SetIsVisible(!isCinematic);
	}
	StartPlayingInternal();
}

bool GhostRecorder::IsTargetInCurrentScene() const {
	return IsObjectInScene(sceneManager_, target_);
}

void GhostRecorder::ClearTargetIfMissingFromScene() {
	if (target_ && !IsTargetInCurrentScene()) {
		ClearTarget();
		return;
	}

	if (anchor_ && !IsObjectInScene(sceneManager_, anchor_)) {
		anchor_ = nullptr;
	}
}

void GhostRecorder::Update() {
	ClearTargetIfMissingFromScene();
	if (!target_) return;

	// 録画中
	if (state_ == State::Recording) {
		GhostFrame frame;
		frame.position = target_->GetTranslate();
		frame.rotation = target_->GetRotation();
		frame.scale = target_->GetScale();
		frames_.push_back(frame);
	}
	// 再生中
	else if (state_ == State::Playing) {

		if (frames_.empty()) {
			Stop(false);
			return;
		}

		if (currentFrameIndex_ < frames_.size()) {
			const auto& frame = frames_[currentFrameIndex_];
			ApplyFrameTransform(frame);
			if (frame.eventID != 0) {
				target_->OnRecordEvent(frame.eventID);
			}
			if (isOverrideCamera_) {
				// Object更新後に時間を進めず行列だけ再計算し、完全追従時の1フレーム遅れをなくします。
				CameraManager::GetInstance()->Update(0.0f);
			}
			currentFrameIndex_++;
		}
		else {

			if (isLoop_) {
				currentFrameIndex_ = 0;
			}
			else {
				Stop();
			}
		}
	}
}

void GhostRecorder::Play(const std::string& fileName, bool loop, bool isRelative, bool isCinematic) {
	Load(fileName);
	if (frames_.empty()) return;

	isLoop_ = loop;
	isRelative_ = isRelative; // 渡された引数（Objectのチェックボックス）を優先
	isOverrideCamera_ = isCinematic;

	if (target_) { target_->SetIsVisible(!isCinematic); }

	// 相対再生フラグが有効な場合、開始時のトランスフォームを基準として記憶
	if (isRelative_) {
		CaptureBasePose();
	}

	StartPlayingInternal();
}
void GhostRecorder::Stop(bool autoReset) {

	if (autoReset && state_ == State::Playing && isRelative_ && target_) {
		RestoreBasePose();
	}

	state_ = State::Idle;
	currentFrameIndex_ = 0;

	StopCinematicPlayback();
}

void GhostRecorder::StartRecording() {
	if (!target_) return;
	frames_.clear();
	state_ = State::Recording;
}

void GhostRecorder::StopRecording() {
	state_ = State::Idle;
}

void GhostRecorder::StartPlayingInternal() {
	if (!target_ || frames_.empty()) return;
	state_ = State::Playing;
	currentFrameIndex_ = 0;

	StartCinematicPlayback();
}

void GhostRecorder::ApplyFrameTransform(const GhostFrame& frame) {
	if (!target_) {
		return;
	}

	if (isRelative_) {
		target_->SetTranslate(basePosition_ + frame.position);
		target_->SetRotation(AddEuler(baseRotation_, frame.rotation));
	}
	else {
		target_->SetTranslate(frame.position);
		target_->SetRotation(frame.rotation);
	}
	target_->SetScale(frame.scale);
	target_->UpdateLocalMatrix();
	target_->UpdateWorldMatrix();
}

void GhostRecorder::StartCinematicPlayback() {
	if (!isOverrideCamera_) {
		return;
	}
	if (CameraEditor* cameraEditor = CameraEditor::GetInstance()) {
		cameraEditor->PlaySceneObjectCamera(CameraManager::GetInstance()->GetMainCamera(), target_);
	}
}

void GhostRecorder::StopCinematicPlayback() {
	if (!isOverrideCamera_) {
		return;
	}
	if (CameraEditor* cameraEditor = CameraEditor::GetInstance()) {
		cameraEditor->StopSceneObjectCamera(CameraManager::GetInstance()->GetMainCamera());
		DebugConsole::GetInstance()->AddLog("Cinematic camera returned to the previous view.");
	}
}

// ==========================================================================
// 計算用ヘルパー
// ==========================================================================

Vector3 GhostRecorder::CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
	Vector3 result;
	float t2 = t * t;
	float t3 = t * t * t;
	result.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
	result.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
	result.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
	return result;
}

Vector3 GhostRecorder::GetSplinePoint(const std::vector<Vector3>& points, float t) {
	if (points.empty()) return { 0,0,0 };
	if (points.size() == 1) return points[0];

	int numPoints = (int)points.size();
	float p = t * (numPoints - 1);
	int index = (int)p;
	float localT = p - index;

	if (index >= numPoints - 1) {
		index = numPoints - 2;
		localT = 1.0f;
	}

	int i0 = index - 1;
	int i1 = index;
	int i2 = index + 1;
	int i3 = index + 2;

	if (i0 < 0) i0 = 0;
	if (i2 >= numPoints) i2 = numPoints - 1;
	if (i3 >= numPoints) i3 = numPoints - 1;

	return CatmullRom(points[i0], points[i1], points[i2], points[i3], localT);
}

// 座標変換 (World -> Clip -> Screen)
bool GhostRecorder::HasPreviewData() const {
	if (!target_) {
		return false;
	}
	if (!IsTargetInCurrentScene()) {
		return false;
	}
	if (frames_.size() >= 2) {
		return true;
	}
	if (!genParams_.waypoints.empty()) {
		return true;
	}

	auto hasDelta = [](const Vector3& a, const Vector3& b) {
		const float epsilon = 0.0001f;
		return std::abs(a.x - b.x) > epsilon ||
			std::abs(a.y - b.y) > epsilon ||
			std::abs(a.z - b.z) > epsilon;
		};

	return hasDelta(genParams_.startPos, genParams_.endPos) ||
		hasDelta(genParams_.startRot, genParams_.endRot) ||
		hasDelta(genParams_.startScale, genParams_.endScale);
}

std::vector<GhostFrame> GhostRecorder::BuildPreviewSamples(int sampleCount) {
	std::vector<GhostFrame> samples;
	ClearTargetIfMissingFromScene();
	if (!target_) {
		return samples;
	}

	int count = std::clamp(sampleCount, 2, 32);

	if (frames_.size() >= 2) {
		samples.reserve(count);

		Vector3 basePos = target_->GetTranslate();
		Vector3 baseRot = target_->GetRotation();
		if (isRelative_ || genParams_.generateRelative) {
			FindAnchor();
			if (anchor_) {
				basePos = {
					anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
					anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
					anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
				};
				baseRot = AddEuler(anchor_->GetRotation(), genParams_.anchorOffsetRot);
			}
		}

		for (int i = 0; i < count; ++i) {
			float t = (count <= 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
			float p = t * static_cast<float>(frames_.size() - 1);
			int index = static_cast<int>(p);
			float localT = p - static_cast<float>(index);
			if (index >= static_cast<int>(frames_.size()) - 1) {
				index = static_cast<int>(frames_.size()) - 2;
				localT = 1.0f;
			}

			const GhostFrame& a = frames_[index];
			const GhostFrame& b = frames_[index + 1];
			GhostFrame sample;
			sample.position = Math::Lerp(a.position, b.position, localT);
			sample.rotation = AnimationInterpolation::SlerpEuler(a.rotation, b.rotation, localT);
			sample.scale = Math::Lerp(a.scale, b.scale, localT);

			if (isRelative_ || genParams_.generateRelative) {
				sample.position = {
					basePos.x + sample.position.x,
					basePos.y + sample.position.y,
					basePos.z + sample.position.z
				};
				sample.rotation = AddEuler(baseRot, sample.rotation);
			}
			samples.push_back(sample);
		}
		return samples;
	}

	std::vector<Vector3> positions;
	std::vector<Vector3> rotations;
	std::vector<Vector3> scales;
	positions.reserve(genParams_.waypoints.size() + 2);
	rotations.reserve(genParams_.waypoints.size() + 2);
	scales.reserve(genParams_.waypoints.size() + 2);

	Vector3 drawOffset = { 0.0f, 0.0f, 0.0f };
	Vector3 drawRotOffset = { 0.0f, 0.0f, 0.0f };
	if (genParams_.generateRelative) {
		FindAnchor();
		Vector3 currentBase = target_->GetTranslate();
		Vector3 currentRot = target_->GetRotation();
		if (anchor_) {
			currentBase = {
				anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
				anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
				anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
			};
			currentRot = AddEuler(anchor_->GetRotation(), genParams_.anchorOffsetRot);
		}
		drawOffset = {
			currentBase.x - genParams_.startPos.x,
			currentBase.y - genParams_.startPos.y,
			currentBase.z - genParams_.startPos.z
		};
		drawRotOffset = SubEuler(currentRot, genParams_.startRot);
	}

	auto addPos = [&](const Vector3& p) {
		positions.push_back({ p.x + drawOffset.x, p.y + drawOffset.y, p.z + drawOffset.z });
		};
	auto addRot = [&](const Vector3& r) {
		rotations.push_back(AddEuler(r, drawRotOffset));
		};

	addPos(genParams_.startPos);
	addRot(genParams_.startRot);
	scales.push_back(genParams_.startScale);
	for (const auto& wp : genParams_.waypoints) {
		addPos(wp.pos);
		addRot(wp.rot);
		scales.push_back(wp.scale);
	}
	addPos(genParams_.endPos);
	addRot(genParams_.endRot);
	scales.push_back(genParams_.endScale);

	if (positions.size() < 2) {
		return samples;
	}

	samples.reserve(count);
	for (int i = 0; i < count; ++i) {
		float t = (count <= 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
		float p = t * static_cast<float>(positions.size() - 1);
		int index = static_cast<int>(p);
		float localT = p - static_cast<float>(index);
		if (index >= static_cast<int>(positions.size()) - 1) {
			index = static_cast<int>(positions.size()) - 2;
			localT = 1.0f;
		}

		GhostFrame sample;
		sample.position = genParams_.useSpline ? GetSplinePoint(positions, t) : Math::Lerp(positions[index], positions[index + 1], localT);
		sample.rotation = AnimationInterpolation::SlerpEuler(rotations[index], rotations[index + 1], localT);
		sample.scale = Math::Lerp(scales[index], scales[index + 1], localT);
		samples.push_back(sample);
	}

	return samples;
}

void GhostRecorder::DrawObjectGhostPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
#ifdef USE_IMGUI
	ClearTargetIfMissingFromScene();
	if (!isShowPreview_ || !isShowObjectPreview_ || !target_ || state_ == State::Recording) {
		return;
	}
	if (!target_->GetModel() || !HasPreviewData()) {
		return;
	}

	std::vector<GhostFrame> samples = BuildPreviewSamples(objectPreviewSampleCount_);
	if (samples.empty()) {
		return;
	}

	Vector3 oldPos = target_->GetTranslate();
	Vector3 oldRot = target_->GetRotation();
	Vector3 oldScale = target_->GetScale();
	Vector4 oldColor = target_->GetColor();
	BlendMode oldBlendMode = target_->GetBlendMode();
	bool oldVisible = target_->GetIsVisible();

	Vector4 ghostColor = oldColor;
	ghostColor.w = std::clamp(objectPreviewAlpha_, 0.02f, 0.75f);

	target_->SetBlendMode(BlendMode::kNormal);
	target_->SetColor(ghostColor);
	target_->SetIsVisible(true);

	for (const GhostFrame& sample : samples) {
		target_->SetTranslate(sample.position);
		target_->SetRotation(sample.rotation);
		target_->SetScale(sample.scale);
		target_->UpdateWorldMatrix();
		target_->Draw(pointLightResource, spotLightResource);
	}

	target_->SetTranslate(oldPos);
	target_->SetRotation(oldRot);
	target_->SetScale(oldScale);
	target_->SetColor(oldColor);
	target_->SetBlendMode(oldBlendMode);
	target_->SetIsVisible(oldVisible);
	target_->UpdateWorldMatrix();
#else
	(void)pointLightResource;
	(void)spotLightResource;
#endif
}

Vector3 GhostRecorder::TransformCoord(const Vector3& vec, const Matrix4x4& mat) {
	Vector3 result;
	float w = vec.x * mat.m[0][3] + vec.y * mat.m[1][3] + vec.z * mat.m[2][3] + mat.m[3][3];
	result.x = (vec.x * mat.m[0][0] + vec.y * mat.m[1][0] + vec.z * mat.m[2][0] + mat.m[3][0]);
	result.y = (vec.x * mat.m[0][1] + vec.y * mat.m[1][1] + vec.z * mat.m[2][1] + mat.m[3][1]);
	result.z = (vec.x * mat.m[0][2] + vec.y * mat.m[1][2] + vec.z * mat.m[2][2] + mat.m[3][2]);

	if (w != 0.0f) {
		result.x /= w;
		result.y /= w;
		result.z /= w;
	}
	return result;
}

// ==========================================================================
// DrawPreview (パスとイベントの可視化機能)
// ==========================================================================
void GhostRecorder::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size, bool isReadOnly) {
#ifdef USE_IMGUI
	ClearTargetIfMissingFromScene();
	if (!isShowPreview_ || !target_) return;

	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float time = (float)ImGui::GetTime();

	// --- 1. 座標変換用のヘルパー関数 ---
	auto WorldToScreen = [&](const Vector3& worldPos) -> ImVec2 {
		Vector3 clip = TransformCoord(worldPos, viewProjection);
		if (clip.z < 0.0f || clip.z > 1.0f) return ImVec2(-10000.0f, -10000.0f);
		float screenX = offset.x + (clip.x + 1.0f) * 0.5f * size.x;
		float screenY = offset.y + (1.0f - clip.y) * 0.5f * size.y;
		return ImVec2(screenX, screenY);
		};

	auto LocalToWorld = [&](const Vector3& localPos) -> Vector3 {
		if (!target_ || !target_->GetParent()) return localPos;
		const Matrix4x4& pMat = target_->GetParent()->GetWorldMatrix();
		Vector3 res;
		res.x = localPos.x * pMat.m[0][0] + localPos.y * pMat.m[1][0] + localPos.z * pMat.m[2][0] + pMat.m[3][0];
		res.y = localPos.x * pMat.m[0][1] + localPos.y * pMat.m[1][1] + localPos.z * pMat.m[2][1] + pMat.m[3][1];
		res.z = localPos.x * pMat.m[0][2] + localPos.y * pMat.m[1][2] + localPos.z * pMat.m[2][2] + pMat.m[3][2];
		return res;
		};

	// --- 2. 描画位置のオフセット計算 ---
	Vector3 drawOffset = { 0, 0, 0 };
	Vector3 drawRotOffset = { 0, 0, 0 };

	if (genParams_.generateRelative && target_) {
		FindAnchor();
		if (state_ == State::Playing || isScrubbing_ || isReadOnly) {
			drawOffset = { basePosition_.x - genParams_.startPos.x, basePosition_.y - genParams_.startPos.y, basePosition_.z - genParams_.startPos.z };
			// プレビュー時の回転オフセットをクォータニオンで算出
			drawRotOffset = SubEuler(baseRotation_, genParams_.startRot);
		}
		else {
			Vector3 currentBase = target_->GetTranslate();
			Vector3 currentRot = target_->GetRotation();

			if (anchor_) {
				currentBase = {
					anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
					anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
					anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
				};
				// アンカー時の回転オフセットをクォータニオン合成
				currentRot = AddEuler(anchor_->GetRotation(), genParams_.anchorOffsetRot);
			}

			static Vector3 s_cachedOffset = { 0, 0, 0 };
			static Vector3 s_cachedRotOffset = { 0, 0, 0 };

			if (isDraggingGizmo_ && selectedPinType_ == SelectedPinType::Start) {
				drawOffset = s_cachedOffset;
				drawRotOffset = s_cachedRotOffset;
			}
			else {
				drawOffset = { currentBase.x - genParams_.startPos.x, currentBase.y - genParams_.startPos.y, currentBase.z - genParams_.startPos.z };
				// クォータニオンによる差分計算
				drawRotOffset = SubEuler(currentRot, genParams_.startRot);
				s_cachedOffset = drawOffset;
				s_cachedRotOffset = drawRotOffset;
			}
		}
	}

	auto applyOffset = [&](const Vector3& p) { return Vector3{ p.x + drawOffset.x, p.y + drawOffset.y, p.z + drawOffset.z }; };
	// 回転の適用もクォータニオン合成で実行
	auto applyRotOffset = [&](const Vector3& r) { return AddEuler(r, drawRotOffset); };

	// Gizmo処理 (Rotate / Scale 修正版)
	bool isGizmoHovered = false;

	if (!isReadOnly && selectedPinType_ != SelectedPinType::None) {
		Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
		if (cam) {
			ImGui::PushID("GhostRecorderGizmo");
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(offset.x, offset.y, size.x, size.y);

			Matrix4x4 view = cam->GetViewMatrix();
			Matrix4x4 proj = cam->GetProjectionMatrix();

			Vector3* targetPos = nullptr;
			Vector3* targetRot = nullptr;
			Vector3* targetScale = nullptr;

			if (selectedPinType_ == SelectedPinType::Start) {
				targetPos = &genParams_.startPos;
				targetRot = &genParams_.startRot;
				targetScale = &genParams_.startScale;
			}
			else if (selectedPinType_ == SelectedPinType::End) {
				targetPos = &genParams_.endPos;
				targetRot = &genParams_.endRot;
				targetScale = &genParams_.endScale;
			}
			else if (selectedPinType_ == SelectedPinType::Waypoint && selectedWaypointIndex_ >= 0 && selectedWaypointIndex_ < genParams_.waypoints.size()) {
				targetPos = &genParams_.waypoints[selectedWaypointIndex_].pos;
				targetRot = &genParams_.waypoints[selectedWaypointIndex_].rot;
				targetScale = &genParams_.waypoints[selectedWaypointIndex_].scale;
			}

			if (targetPos && targetRot && targetScale) {
				Vector3 worldPos = { targetPos->x + drawOffset.x, targetPos->y + drawOffset.y, targetPos->z + drawOffset.z };
				// ワールド回転を算出
				Vector3 worldRot = AddEuler(*targetRot, drawRotOffset);
				Vector3 worldScale = *targetScale;

				Matrix4x4 matScale = Math::MakeScaleMatrix(worldScale);
				Matrix4x4 matTrans = Math::MakeTranslateMatrix(worldPos);
				Matrix4x4 matRot = Math::MakeRotateMatrix(worldRot);
				Matrix4x4 worldMat = Math::Multiply(matScale, Math::Multiply(matRot, matTrans));

				static ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
				if (!ImGui::GetIO().WantTextInput) {
					if (ImGui::IsKeyPressed(ImGuiKey_T)) op = ImGuizmo::TRANSLATE;
					if (ImGui::IsKeyPressed(ImGuiKey_R)) op = ImGuizmo::ROTATE;
					if (ImGui::IsKeyPressed(ImGuiKey_S)) op = ImGuizmo::SCALE;
				}

				ImGuizmo::MODE mode = (op == ImGuizmo::TRANSLATE) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
				ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], op, mode, &worldMat.m[0][0], nullptr, nullptr);

				if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) {
					isGizmoHovered = true;
				}

				if (ImGuizmo::IsUsing()) {
					if (!isDraggingGizmo_) { SaveHistory(); isDraggingGizmo_ = true; } // Undo対応
					Vector3 newTrans, newRotDeg, newScale;
					ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], &newTrans.x, &newRotDeg.x, &newScale.x);

					targetPos->x = newTrans.x - drawOffset.x;
					targetPos->y = newTrans.y - drawOffset.y;
					targetPos->z = newTrans.z - drawOffset.z;

					float radX = newRotDeg.x * (3.14159265f / 180.0f);
					float radY = newRotDeg.y * (3.14159265f / 180.0f);
					float radZ = newRotDeg.z * (3.14159265f / 180.0f);
					Vector3 newRotRad = { radX, radY, radZ };

					// クォータニオンによる減算 (Gizmo操作の逆適用)
					*targetRot = SubEuler(newRotRad, drawRotOffset);

					*targetScale = newScale;
				}
				else {
					isDraggingGizmo_ = false;
				}
			}
			ImGui::PopID();
		}
	}

	// --- 3. パスを構成する全頂点のリストを作成 ---
	std::vector<Vector3> allPos;
	std::vector<Vector3> allRot;
	allPos.push_back(applyOffset(genParams_.startPos)); allRot.push_back(applyRotOffset(genParams_.startRot));
	for (const auto& wp : genParams_.waypoints) { allPos.push_back(applyOffset(wp.pos)); allRot.push_back(applyRotOffset(wp.rot)); }
	allPos.push_back(applyOffset(genParams_.endPos)); allRot.push_back(applyRotOffset(genParams_.endRot));
	if (allPos.size() < 2) return;

	ImVec2 mousePos = ImGui::GetMousePos();
	bool isHoveredGameView = (mousePos.x >= offset.x && mousePos.x <= offset.x + size.x && mousePos.y >= offset.y && mousePos.y <= offset.y + size.y);
	bool isMouseClicked = ImGui::IsMouseClicked(0) && isHoveredGameView;

	auto GetDistanceToSegment = [](ImVec2 p, ImVec2 a, ImVec2 b) {
		float l2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
		if (l2 == 0.0f) return sqrtf((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
		float t = ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / l2;
		t = std::max(0.0f, std::min(1.0f, t));
		ImVec2 proj = { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y) };
		return sqrtf((p.x - proj.x) * (p.x - proj.x) + (p.y - proj.y) * (p.y - proj.y));
		};

	float minLineDist = 15.0f;
	int hitSegmentIndex = -1;

	// --- 4. パスの軌跡(ライン)を描画 ＆ ラインクリック判定 ---
	const int samples = 60;
	ImVec2 prevScreenPos = WorldToScreen(LocalToWorld(allPos[0]));

	float bestHitT = 0.0f; //  線上で一番近かった割合(t)を保存する

	for (int i = 1; i <= samples; ++i) {
		float t = (float)i / (float)samples;
		Vector3 currentPos = genParams_.useSpline ? GetSplinePoint(allPos, t) : [&]() { float p = t * (allPos.size() - 1); int idx = (int)p; float lt = p - idx; if (idx >= (int)allPos.size() - 1) { idx = (int)allPos.size() - 2; lt = 1.0f; } return Math::Lerp(allPos[idx], allPos[idx + 1], lt); }();
		ImVec2 screenPos = WorldToScreen(LocalToWorld(currentPos));
		if (screenPos.x > -5000.0f && prevScreenPos.x > -5000.0f) {
			drawList->AddLine(prevScreenPos, screenPos, IM_COL32(255, 255, 255, 100), 1.5f);

			if (isMouseClicked && !isGizmoHovered && ImGui::GetIO().KeyCtrl) {
				float dist = GetDistanceToSegment(mousePos, prevScreenPos, screenPos);
				if (dist < minLineDist) {
					minLineDist = dist;
					float midT = (t + (float)(i - 1) / samples) * 0.5f;
					hitSegmentIndex = (int)(midT * (allPos.size() - 1));
					bestHitT = midT;
				}
			}
		}
		prevScreenPos = screenPos;
	}

	// --- 5. 進行方向を示す矢印の描画 ---
	const int arrowSteps = 10;
	for (int i = 0; i <= arrowSteps; ++i) {
		float t = (float)i / (float)arrowSteps;
		float p = t * (allPos.size() - 1); int idx = (int)p; float lt = p - idx; if (idx >= (int)allPos.size() - 1) { idx = (int)allPos.size() - 2; lt = 1.0f; }
		Vector3 pos = genParams_.useSpline ? GetSplinePoint(allPos, t) : Math::Lerp(allPos[idx], allPos[idx + 1], lt);
		// プレビュー矢印の向きをSlerpで補間
		Vector3 rot = AnimationInterpolation::SlerpEuler(allRot[idx], allRot[idx + 1], lt);

		ImVec2 baseScr = WorldToScreen(LocalToWorld(pos));
		if (baseScr.x < -5000.0f) continue;
		float cosX = cosf(rot.x);
		Vector3 forward = { cosX * sinf(rot.y), -sinf(rot.x), cosX * cosf(rot.y) };
		Vector3 lookTarget = { pos.x + forward.x * 3.0f, pos.y + forward.y * 3.0f, pos.z + forward.z * 3.0f };
		ImVec2 lookScr = WorldToScreen(LocalToWorld(lookTarget));
		if (lookScr.x > -5000.0f) { drawList->AddLine(baseScr, lookScr, IM_COL32(0, 255, 255, 200), 2.0f); drawList->AddCircleFilled(lookScr, 2.0f, IM_COL32(0, 255, 255, 255)); }
	}

	// --- 6. アニメーションして流れる光点の描画 ---
	for (int i = 0; i < 5; ++i) {
		float t = fmodf(time * 0.4f + (float)i / 5.0f, 1.0f);
		Vector3 pPos = genParams_.useSpline ? GetSplinePoint(allPos, t) : [&]() { float p = t * (allPos.size() - 1); int idx = (int)p; float lt = p - idx; if (idx >= (int)allPos.size() - 1) { idx = (int)allPos.size() - 2; lt = 1.0f; } return Math::Lerp(allPos[idx], allPos[idx + 1], lt); }();
		ImVec2 pScr = WorldToScreen(LocalToWorld(pPos));
		if (pScr.x > -5000.0f) drawList->AddCircleFilled(pScr, 3.0f, IM_COL32(255, 255, 0, 200));
	}

	// --- 7. ピンのクリック判定と描画 ---
	float minHitDist = 15.0f;
	SelectedPinType hitType = SelectedPinType::None;
	int hitIndex = -1;

	auto CheckClick = [&](ImVec2 pos, SelectedPinType type, int index) {
		if (isReadOnly || !isMouseClicked) return;
		float dx = mousePos.x - pos.x; float dy = mousePos.y - pos.y;
		float dist = sqrtf(dx * dx + dy * dy);
		if (dist < minHitDist) { minHitDist = dist; hitType = type; hitIndex = index; }
		};

	auto DrawWireBox = [&](const Vector3& pos, const Vector3& rot, const Vector3& scale, ImU32 color) {
		Vector3 localVerts[8] = {
			{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f},
			{-0.5f, -0.5f,  0.5f}, {0.5f, -0.5f,  0.5f}, {-0.5f, 0.5f,  0.5f}, {0.5f, 0.5f,  0.5f}
		};
		Matrix4x4 matTrans = Math::MakeTranslateMatrix(pos);
		Matrix4x4 matRot = Math::MakeRotateMatrix(rot);
		Matrix4x4 matScale = Math::MakeScaleMatrix(scale);
		Matrix4x4 matPoint = Math::Multiply(matScale, Math::Multiply(matRot, matTrans));

		ImVec2 screenVerts[8];
		for (int i = 0; i < 8; ++i) {
			Vector3 p = Math::Transform(localVerts[i], matPoint);
			screenVerts[i] = WorldToScreen(LocalToWorld(p));
		}

		int edges[12][2] = {
			{0,1}, {1,3}, {3,2}, {2,0},
			{4,5}, {5,7}, {7,6}, {6,4},
			{0,4}, {1,5}, {2,6}, {3,7}
		};
		for (int i = 0; i < 12; ++i) {
			ImVec2 p1 = screenVerts[edges[i][0]];
			ImVec2 p2 = screenVerts[edges[i][1]];
			if (p1.x > -5000.0f && p2.x > -5000.0f) {
				drawList->AddLine(p1, p2, color, 1.5f);
			}
		}
		};

	ImVec2 startScr = WorldToScreen(LocalToWorld(allPos[0]));
	if (startScr.x > -5000.0f) {
		DrawWireBox(allPos[0], allRot[0], genParams_.startScale, IM_COL32(0, 255, 0, 100));
		if (selectedPinType_ == SelectedPinType::Start) drawList->AddCircle(startScr, 10.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
		drawList->AddCircleFilled(startScr, 6.0f, IM_COL32(0, 255, 0, 255));
		CheckClick(startScr, SelectedPinType::Start, -1);
	}

	ImVec2 endScr = WorldToScreen(LocalToWorld(allPos.back()));
	if (endScr.x > -5000.0f) {
		DrawWireBox(allPos.back(), allRot.back(), genParams_.endScale, IM_COL32(100, 100, 255, 100));
		if (selectedPinType_ == SelectedPinType::End) drawList->AddCircle(endScr, 10.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
		drawList->AddCircleFilled(endScr, 6.0f, IM_COL32(100, 100, 255, 255));
		CheckClick(endScr, SelectedPinType::End, -1);
	}

	for (int i = 0; i < (int)genParams_.waypoints.size(); ++i) {
		ImVec2 wpScr = WorldToScreen(LocalToWorld(allPos[i + 1]));
		if (wpScr.x > -5000.0f) {
			DrawWireBox(allPos[i + 1], allRot[i + 1], genParams_.waypoints[i].scale, IM_COL32(255, 165, 0, 100));
			if (selectedPinType_ == SelectedPinType::Waypoint && selectedWaypointIndex_ == i) drawList->AddCircle(wpScr, 10.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
			drawList->AddCircleFilled(wpScr, 6.0f, IM_COL32(255, 165, 0, 255));
			CheckClick(wpScr, SelectedPinType::Waypoint, i);
		}
	}

	if (!isReadOnly && isMouseClicked && !isGizmoHovered) {
		if (hitType != SelectedPinType::None) {
			selectedPinType_ = hitType;
			selectedWaypointIndex_ = hitIndex;
		}
		else if (ImGui::GetIO().KeyCtrl) {
			auto ScreenToWorld = [&](const ImVec2& sPos, float zClip) -> Vector3 {
				Matrix4x4 invVP = Math::Inverse(viewProjection);
				float ndcX = ((sPos.x - offset.x) / size.x) * 2.0f - 1.0f;
				float ndcY = 1.0f - ((sPos.y - offset.y) / size.y) * 2.0f;
				Vector3 ndcPos = { ndcX, ndcY, zClip };
				return Math::Transform(ndcPos, invVP);
				};

			Vector3 rayOrigin = ScreenToWorld(mousePos, 0.0f);
			Vector3 rayEnd = ScreenToWorld(mousePos, 1.0f);
			Vector3 rayDir = Math::Normalize({ rayEnd.x - rayOrigin.x, rayEnd.y - rayOrigin.y, rayEnd.z - rayOrigin.z });

			Vector3 currentBase = target_->GetTranslate();
			if (anchor_) {
				currentBase = { anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x, anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y, anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z };
			}

			// 基準点（最後に打ったWaypoint、またはStart位置）とのカメラ距離を維持して配置
			Vector3 refPos = currentBase;
			if (!genParams_.waypoints.empty()) {
				refPos = { genParams_.waypoints.back().pos.x + drawOffset.x, genParams_.waypoints.back().pos.y + drawOffset.y, genParams_.waypoints.back().pos.z + drawOffset.z };
			}
			else {
				refPos = { genParams_.startPos.x + drawOffset.x, genParams_.startPos.y + drawOffset.y, genParams_.startPos.z + drawOffset.z };
			}

			// 2. カメラ(rayOrigin)から基準点までの「距離」を計算する
			float diffX = refPos.x - rayOrigin.x;
			float diffY = refPos.y - rayOrigin.y;
			float diffZ = refPos.z - rayOrigin.z;
			float distToRef = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

			// マウスレイ上でパスに最も近い位置へ新しい点を追加する。
			Vector3 hitPos = { rayOrigin.x + rayDir.x * distToRef, rayOrigin.y + rayDir.y * distToRef, rayOrigin.z + rayDir.z * distToRef };
			Vector3 localPos = { hitPos.x - drawOffset.x, hitPos.y - drawOffset.y, hitPos.z - drawOffset.z };

			SaveHistory();

			GenerationParams::Waypoint wp;
			wp.scale = target_->GetScale();
			wp.eventID = 0;
			wp.waitTime = 0.0f;
			wp.durationToNext = 1.0f;
			wp.easingToNext = 0;

			if (hitSegmentIndex != -1) {
				// 線の上をクリックした場合は、軌道上の3D座標を直接使う
				Vector3 onPathPos = genParams_.useSpline ? GetSplinePoint(allPos, bestHitT) :
					[&]() { float p = bestHitT * (allPos.size() - 1); int idx = (int)p; float lt = p - idx; if (idx >= (int)allPos.size() - 1) { idx = (int)allPos.size() - 2; lt = 1.0f; } return Math::Lerp(allPos[idx], allPos[idx + 1], lt); }();

				wp.pos = { onPathPos.x - drawOffset.x, onPathPos.y - drawOffset.y, onPathPos.z - drawOffset.z };
				wp.rot = (hitSegmentIndex == 0) ? genParams_.startRot : genParams_.waypoints[hitSegmentIndex - 1].rot;
				int insertIdx = std::min(hitSegmentIndex, (int)genParams_.waypoints.size());
				genParams_.waypoints.insert(genParams_.waypoints.begin() + insertIdx, wp);
				selectedPinType_ = SelectedPinType::Waypoint;
				selectedWaypointIndex_ = insertIdx;
				DebugConsole::GetInstance()->AddLog("Waypoint Inserted on Path!");
			}
			else {
				// 何もない空間をクリックした場合は、同じカメラ距離(深度)の空間に追加
				wp.pos = localPos;
				wp.rot = genParams_.waypoints.empty() ? genParams_.startRot : genParams_.waypoints.back().rot;
				genParams_.waypoints.push_back(wp);
				selectedPinType_ = SelectedPinType::Waypoint;
				selectedWaypointIndex_ = static_cast<int>(genParams_.waypoints.size()) - 1;
				DebugConsole::GetInstance()->AddLog("Waypoint Added at Camera Depth!");
			}
		}
	
			
		
		else {
			selectedPinType_ = SelectedPinType::None;
			selectedWaypointIndex_ = -1;
		}
	}
#endif
}

// ==========================================================================
// DrawImGui (UI部分)
// ==========================================================================

void GhostRecorder::DrawImGui() {
#ifdef USE_IMGUI
	Update();
	if (!ImGui::GetIO().WantTextInput) {
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) PerformUndo();
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) PerformRedo();

		if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
			if (selectedPinType_ == SelectedPinType::Waypoint && selectedWaypointIndex_ >= 0 && selectedWaypointIndex_ < genParams_.waypoints.size()) {
				SaveHistory();
				genParams_.waypoints.erase(genParams_.waypoints.begin() + selectedWaypointIndex_);
				selectedPinType_ = SelectedPinType::None;
				selectedWaypointIndex_ = -1;
				DebugConsole::GetInstance()->AddLog("Waypoint Deleted!");
			}
		}
	}
	static char fName[64] = "anim_path";

	// -------------------------------------------------------------
	// 1. ファイル管理
	// -------------------------------------------------------------
	if (ImGui::CollapsingHeader(ICON_FA_FOLDER_OPEN " ファイル管理 (File IO)", ImGuiTreeNodeFlags_DefaultOpen)) {
		std::string dirPath = "Resources/json/animation/";
		if (std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath)) {
			if (ImGui::BeginCombo(ICON_FA_HISTORY " 既存データからロード", fName)) {
				for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
					if (entry.path().extension() == ".json") {
						std::string fileName = entry.path().stem().string();
						bool isSelected = (std::string(fName) == fileName);
						if (ImGui::Selectable(fileName.c_str(), isSelected)) {
							strncpy_s(fName, sizeof(fName), fileName.c_str(), _TRUNCATE);
							Load(fName);
						}
						if (isSelected) ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		ImGui::InputText(ICON_FA_FILE_SIGNATURE " ファイル名", fName, sizeof(fName));

		if (ImGui::Button(ICON_FA_DOWNLOAD " Save")) Save(fName);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_UPLOAD " Load")) Load(fName);
	}
	ImGui::Separator();

	// -------------------------------------------------------------
	// 2. ターゲット & アンカー選択
	// -------------------------------------------------------------
	if (ImGui::CollapsingHeader(ICON_FA_BULLSEYE " ターゲット設定", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (sceneManager_ && sceneManager_->GetCurrentScene()) {
			std::string currentTargetName = target_ ? target_->GetName() : "(未選択)";
			if (ImGui::BeginCombo(ICON_FA_CROSSHAIRS " ターゲット", currentTargetName.c_str())) {
				for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
					bool isSelected = (target_ == obj.get());
					if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) SetTarget(obj.get());
				}
				ImGui::EndCombo();
			}

			std::string currentAnchorName = anchorName_.empty() ? "(なし: 自身を基準)" : anchorName_;
			if (ImGui::BeginCombo(ICON_FA_LINK " アンカー(相対基準)", currentAnchorName.c_str())) {
				if (ImGui::Selectable("(なし: 自身を基準)", anchorName_.empty())) {
					anchorName_ = "";
					anchor_ = nullptr;
				}
				for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
					bool isSelected = (anchorName_ == obj->GetName());
					if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) {
						anchorName_ = obj->GetName();
						anchor_ = obj.get();
					}
				}
				ImGui::EndCombo();
			}
		}

		if (anchor_ && genParams_.generateRelative) {
			ImGui::Indent();
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ICON_FA_CHEVRON_DOWN " アンカーからの相対距離");
			ImGui::DragFloat3("Offset Pos", &genParams_.anchorOffsetPos.x, 0.1f);
			ImGui::DragFloat3("Offset Rot", &genParams_.anchorOffsetRot.x, 1.0f);
			ImGui::Unindent();
		}
	}

	const char* stateStr = (state_ == State::Recording) ? "録画中" : (state_ == State::Playing) ? "再生中" : "待機中";
	ImGui::Text(ICON_FA_INFO_CIRCLE " 状態: %s | フレーム: %d", stateStr, (int)frames_.size());
	ImGui::Separator();

	// -------------------------------------------------------------
	// 3. 手動録画
	// -------------------------------------------------------------
	if (ImGui::CollapsingHeader(ICON_FA_VIDEO " 手動録画", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (state_ == State::Idle) {
			if (!target_) ImGui::BeginDisabled();
			if (ImGui::Button(ICON_FA_CIRCLE " ● 録画開始", ImVec2(-1, 0))) StartRecording();
			if (!target_) ImGui::EndDisabled();
		}
		else if (state_ == State::Recording) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
			if (ImGui::Button(ICON_FA_STOP " ■ 録画停止", ImVec2(-1, 0))) StopRecording();
			ImGui::PopStyleColor();
		}
	}

	// -------------------------------------------------------------
	// 4. 自動生成 (Path Editor)
	// -------------------------------------------------------------
	if (ImGui::CollapsingHeader(ICON_FA_MAP_SIGNS " パス生成 (Path Editor)", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (target_) {
			ImGui::Checkbox(ICON_FA_EYE " プレビュー線を表示", &isShowPreview_);

			ImGui::SameLine();
			ImGui::Checkbox("Object Ghost", &isShowObjectPreview_);
			if (isShowObjectPreview_) {
				ImGui::DragInt("Ghost Samples", &objectPreviewSampleCount_, 1.0f, 2, 16);
				ImGui::SliderFloat("Ghost Alpha", &objectPreviewAlpha_, 0.05f, 0.60f);
			}

			static const char* easingNames[] = {
				"Linear(等速)", "InSine", "OutSine", "InOutSine", "InQuad", "OutQuad", "InOutQuad",
				"InCubic", "OutCubic", "InOutCubic", "InQuart", "OutQuart", "InOutQuart",
				"InQuint", "OutQuint", "InOutQuint", "InExpo", "OutExpo", "InOutExpo",
				"InCirc", "OutCirc", "InOutCirc"
			};

			// --- Start 設定 ---
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ICON_FA_CHEVRON_CIRCLE_RIGHT " [ START ]");
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_PENCIL_ALT " 上書き##Start")) {
				genParams_.startPos = target_->GetTranslate();
				genParams_.startRot = target_->GetRotation();
				genParams_.startScale = target_->GetScale();
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_LOCATION_ARROW " ワープ##Start")) {
				target_->SetTranslate(genParams_.startPos);
				target_->SetRotation(genParams_.startRot);
				target_->UpdateWorldMatrix();
			}
			ImGui::DragFloat3("Pos##Start", &genParams_.startPos.x, 0.1f);
			ImGui::DragFloat3("Rot##Start", &genParams_.startRot.x, 1.0f);
			ImGui::DragFloat("待機(sec)##Start", &genParams_.startWaitTime, 0.1f, 0.0f, 10.0f);
			ImGui::Combo("次へのEasing##Start", &genParams_.startEasingToNext, easingNames, IM_ARRAYSIZE(easingNames));

			// --- Waypoints 設定 ---
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), ICON_FA_MAP_PIN " [ WAYPOINTS ]");

			int waypointToDelete = -1;
			for (int i = 0; i < (int)genParams_.waypoints.size(); ++i) {
				ImGui::PushID(i);
				if (ImGui::TreeNode("P", "Point %d", i + 1)) {
					if (ImGui::Button(ICON_FA_LOCATION_ARROW " ワープ")) {
						target_->SetTranslate(genParams_.waypoints[i].pos);
						target_->SetRotation(genParams_.waypoints[i].rot);
					}
					ImGui::SameLine();
					if (ImGui::Button(ICON_FA_PENCIL_ALT " 上書き")) {
						genParams_.waypoints[i].pos = target_->GetTranslate();
						genParams_.waypoints[i].rot = target_->GetRotation();
					}
					ImGui::SameLine();
					if (ImGui::Button(ICON_FA_TRASH_ALT " 削除")) {
						SaveHistory();
						waypointToDelete = i;
					}
					ImGui::DragFloat3("Pos", &genParams_.waypoints[i].pos.x, 0.1f);
					ImGui::DragFloat("待機(sec)", &genParams_.waypoints[i].waitTime, 0.1f, 0.0f, 10.0f);
					ImGui::Combo("Easing", &genParams_.waypoints[i].easingToNext, easingNames, IM_ARRAYSIZE(easingNames));
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (waypointToDelete >= 0) genParams_.waypoints.erase(genParams_.waypoints.begin() + waypointToDelete);

			if (ImGui::Button(ICON_FA_PLUS_CIRCLE " ＋ 現在地を追加", ImVec2(-1, 0))) {
				SaveHistory();
				GenerationParams::Waypoint wp;
				wp.pos = target_->GetTranslate(); wp.rot = target_->GetRotation(); wp.scale = target_->GetScale();
				wp.durationToNext = 1.0f; wp.easingToNext = 0;
				genParams_.waypoints.push_back(wp);
			}

			// --- End 設定 ---
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), ICON_FA_FLAG_CHECKERED " [ END ]");
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_PENCIL_ALT " 上書き##End")) {
				genParams_.endPos = target_->GetTranslate();
				genParams_.endRot = target_->GetRotation();
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_SYNC " Loop同期##End")) {
				genParams_.endPos = genParams_.startPos; genParams_.endRot = genParams_.startRot;
			}
			ImGui::DragFloat3("Pos##End", &genParams_.endPos.x, 0.1f);
			ImGui::DragFloat("待機(sec)##End", &genParams_.endWaitTime, 0.1f, 0.0f, 10.0f);

			ImGui::Separator();
			ImGui::Checkbox(ICON_FA_BEZIER_CURVE " スプライン曲線", &genParams_.useSpline);
			ImGui::SameLine();
			ImGui::Checkbox(ICON_FA_PROJECT_DIAGRAM " 相対データ化", &genParams_.generateRelative);

			// パス生成実行ロジック (クォータニオン対応)
			if (ImGui::Button(ICON_FA_MAGIC " 生成実行 (Generate & AutoSave)", ImVec2(-1, 40))) {
				frames_.clear();
				FindAnchor();
				if (anchor_ && genParams_.generateRelative) {
					genParams_.anchorOffsetPos = {
						genParams_.startPos.x - anchor_->GetTranslate().x,
						genParams_.startPos.y - anchor_->GetTranslate().y,
						genParams_.startPos.z - anchor_->GetTranslate().z
					};
					// クォータニオンでの差分計算（アンカー回転からのオフセット）
					genParams_.anchorOffsetRot = SubEuler(genParams_.startRot, anchor_->GetRotation());
				}
				else {
					genParams_.anchorOffsetPos = { 0, 0, 0 };
					genParams_.anchorOffsetRot = { 0, 0, 0 };
				}

				struct PointData {
					Vector3 pos, rot, scale; int eventID; float waitTime, durationToNext; int easingToNext;
				};
				std::vector<PointData> pts;
				pts.push_back({ genParams_.startPos, genParams_.startRot, genParams_.startScale, genParams_.startEventID, genParams_.startWaitTime, genParams_.startDurationToNext, genParams_.startEasingToNext });
				for (const auto& wp : genParams_.waypoints) {
					pts.push_back({ wp.pos, wp.rot, wp.scale, wp.eventID, wp.waitTime, wp.durationToNext, wp.easingToNext });
				}
				pts.push_back({ genParams_.endPos, genParams_.endRot, genParams_.endScale, genParams_.endEventID, genParams_.endWaitTime, 1.0f, 0 });

				Vector3 offset = genParams_.generateRelative ? genParams_.startPos : Vector3{ 0,0,0 };
				Vector3 rotOffset = genParams_.generateRelative ? genParams_.startRot : Vector3{ 0,0,0 };

				for (int i = 0; i < (int)pts.size(); ++i) {
					int waitFrames = static_cast<int>(pts[i].waitTime * 60.0f);
					for (int w = 0; w < waitFrames; ++w) {
						GhostFrame f;
						f.position = { pts[i].pos.x - offset.x, pts[i].pos.y - offset.y, pts[i].pos.z - offset.z };
						f.rotation = SubEuler(pts[i].rot, rotOffset); // クォータニオンによる差分計算
						f.scale = pts[i].scale;
						f.eventID = (w == 0) ? pts[i].eventID : 0;
						frames_.push_back(f);
					}

					if (i == (int)pts.size() - 1) {
						if (waitFrames == 0) {
							GhostFrame f;
							f.position = { pts[i].pos.x - offset.x, pts[i].pos.y - offset.y, pts[i].pos.z - offset.z };
							f.rotation = SubEuler(pts[i].rot, rotOffset); // クォータニオンによる差分計算
							f.scale = pts[i].scale;
							f.eventID = pts[i].eventID;
							frames_.push_back(f);
						}
						break;
					}

					int travelFrames = static_cast<int>(pts[i].durationToNext * 60.0f);
					if (travelFrames < 1) travelFrames = 1;

					for (int f = 0; f < travelFrames; ++f) {
						if (f == 0 && waitFrames > 0) continue;

						float rawT = (float)f / (float)travelFrames;
						float t = ApplyEasing(pts[i].easingToNext, rawT);

						Vector3 currentPos;
						if (genParams_.useSpline) {
							Vector3 p0 = pts[std::max(0, i - 1)].pos;
							Vector3 p1 = pts[i].pos;
							Vector3 p2 = pts[i + 1].pos;
							Vector3 p3 = pts[std::min((int)pts.size() - 1, i + 2)].pos;
							currentPos = CatmullRom(p0, p1, p2, p3, t);
						}
						else {
							currentPos = Math::Lerp(pts[i].pos, pts[i + 1].pos, t);
						}

						// クォータニオンによる最短経路補間（Slerp）
						Vector3 currentRot = AnimationInterpolation::SlerpEuler(pts[i].rot, pts[i + 1].rot, t);
						Vector3 currentScale = Math::Lerp(pts[i].scale, pts[i + 1].scale, t);

						GhostFrame frame;
						frame.position = { currentPos.x - offset.x, currentPos.y - offset.y, currentPos.z - offset.z };
						frame.rotation = SubEuler(currentRot, rotOffset); // クォータニオンによる差分計算
						frame.scale = currentScale;
						frame.eventID = (f == 0 && waitFrames == 0) ? pts[i].eventID : 0;
						frames_.push_back(frame);
					}
				}

				isRelative_ = genParams_.generateRelative;
				Stop();
				Save(fName);
				ImGui::OpenPopup("Done");
			}
			if (ImGui::BeginPopupModal("Done", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("生成＆セーブ完了!");
				if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
				ImGui::EndPopup();
			}
		}
	}

	ImGui::Separator();
	ImGui::Checkbox(ICON_FA_CHESS_BOARD " Cinema Mode (カメラ乗っ取り)", &isOverrideCamera_);
	ImGui::Separator();

	// -------------------------------------------------------------
	// 5. 再生制御 (タイムライン)
	// -------------------------------------------------------------
	if (!frames_.empty()) {
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 1.0f, 1.0f), ICON_FA_STREAM " [ タイムライン ]");

		int maxFrame = static_cast<int>(frames_.size()) - 1;
		int displayFrame = static_cast<int>(currentFrameIndex_);
		ImGui::SetNextItemWidth(-1);
		ImGui::SliderInt("##SeekBar", &displayFrame, 0, maxFrame, "Frame: %d");

		if (ImGui::IsItemActivated()) { isScrubbing_ = true; state_ = State::Idle; FindAnchor(); }
		if (ImGui::IsItemActive()) { currentFrameIndex_ = displayFrame; EvaluateAtFrame(displayFrame); }
		if (ImGui::IsItemDeactivated()) isScrubbing_ = false;

		ImGui::Checkbox(ICON_FA_REDO " Loop再生", &isLoop_);
		ImGui::SameLine();

		if (ImGui::Button(ICON_FA_PLAY " メモリから再生")) {
			if (isRelative_) CaptureBasePose();
			StartPlayingInternal();
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_FILM " ファイルからPlay")) {
			bool isCinematic = target_->IsCameraObject();
			Play(fName, isLoop_, isRelative_, isCinematic);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_STOP " 停止")) Stop();
#endif
}

void GhostRecorder::Save(const std::string& fileName) {
	if (frames_.empty()) return;
	json root;
	root["frames"] = json::array();
	for (const auto& frame : frames_) {
		json frameJson;
		frameJson["pos"] = { frame.position.x, frame.position.y, frame.position.z };
		frameJson["rot"] = { frame.rotation.x, frame.rotation.y, frame.rotation.z };
		frameJson["scale"] = { frame.scale.x, frame.scale.y, frame.scale.z };
		frameJson["eventID"] = frame.eventID;
		root["frames"].push_back(frameJson);
	}


	json paramsJson;
	paramsJson["startPos"] = { genParams_.startPos.x, genParams_.startPos.y, genParams_.startPos.z };
	paramsJson["startRot"] = { genParams_.startRot.x, genParams_.startRot.y, genParams_.startRot.z };
	paramsJson["startScale"] = { genParams_.startScale.x, genParams_.startScale.y, genParams_.startScale.z };
	paramsJson["startEventID"] = genParams_.startEventID;
	paramsJson["startWaitTime"] = genParams_.startWaitTime;

	paramsJson["endPos"] = { genParams_.endPos.x, genParams_.endPos.y, genParams_.endPos.z };
	paramsJson["endRot"] = { genParams_.endRot.x, genParams_.endRot.y, genParams_.endRot.z };
	paramsJson["endScale"] = { genParams_.endScale.x, genParams_.endScale.y, genParams_.endScale.z };
	paramsJson["endEventID"] = genParams_.endEventID;
	paramsJson["endWaitTime"] = genParams_.endWaitTime;
	paramsJson["anchorOffsetPos"] = { genParams_.anchorOffsetPos.x, genParams_.anchorOffsetPos.y, genParams_.anchorOffsetPos.z };
	paramsJson["anchorOffsetRot"] = { genParams_.anchorOffsetRot.x, genParams_.anchorOffsetRot.y, genParams_.anchorOffsetRot.z };
	paramsJson["startDurationToNext"] = genParams_.startDurationToNext;
	paramsJson["startEasingToNext"] = genParams_.startEasingToNext;
	paramsJson["waypoints"] = json::array();
	for (const auto& wp : genParams_.waypoints) {
		paramsJson["waypoints"].push_back({
			{"pos", {wp.pos.x, wp.pos.y, wp.pos.z}},
			{"rot", {wp.rot.x, wp.rot.y, wp.rot.z}},
			{"scale", {wp.scale.x, wp.scale.y, wp.scale.z}},
			{"eventID", wp.eventID},
			{"waitTime", wp.waitTime},
			{ "durationToNext", wp.durationToNext },
			{"easingToNext", wp.easingToNext}
			});
	}

	paramsJson["useSpline"] = genParams_.useSpline;
	paramsJson["generateRelative"] = genParams_.generateRelative;


	root["genParams"] = paramsJson;
	root["anchorName"] = anchorName_;


	std::string path = "Resources/json/animation/" + fileName + ".json";
	std::ofstream file(path);
	if (file.is_open()) { file << root.dump(4); file.close(); }
}

void GhostRecorder::Load(const std::string& fileName) {
	std::string path = "Resources/json/animation/" + fileName + ".json";
	std::ifstream file(path);
	if (!file.is_open()) return;
	json root; file >> root;
	frames_.clear();
	if (root.contains("anchorName")) {
		anchorName_ = root["anchorName"].get<std::string>();
		anchor_ = nullptr;
	}
	else {
		anchorName_ = "";
		anchor_ = nullptr;
	}
	if (root.contains("frames")) {
		for (const auto& j : root["frames"]) {
			GhostFrame f;
			f.position = { j["pos"][0], j["pos"][1], j["pos"][2] };
			f.rotation = { j["rot"][0], j["rot"][1], j["rot"][2] };
			if (j.contains("scale")) {
				f.scale = { j["scale"][0], j["scale"][1], j["scale"][2] };
			}
			else {
				f.scale = { 1.0f, 1.0f, 1.0f }; // デフォルト値
			}
			f.eventID = j.value("eventID", 0);
			frames_.push_back(f);
		}
	}

	if (root.contains("genParams")) {
		const auto& pj = root["genParams"];
		if (pj.contains("startPos")) genParams_.startPos = { pj["startPos"][0], pj["startPos"][1], pj["startPos"][2] };
		if (pj.contains("startRot")) genParams_.startRot = { pj["startRot"][0], pj["startRot"][1], pj["startRot"][2] };
		if (pj.contains("startScale")) {
			genParams_.startScale = { pj["startScale"][0], pj["startScale"][1], pj["startScale"][2] };
		}
		else {
			genParams_.startScale = { 1.0f, 1.0f, 1.0f };
		}

		genParams_.startEventID = pj.value("startEventID", 0);
		genParams_.startWaitTime = pj.value("startWaitTime", 0.0f);


		if (pj.contains("endPos"))   genParams_.endPos = { pj["endPos"][0], pj["endPos"][1], pj["endPos"][2] };
		if (pj.contains("endRot"))   genParams_.endRot = { pj["endRot"][0], pj["endRot"][1], pj["endRot"][2] };
		if (pj.contains("endScale")) {
			genParams_.endScale = { pj["endScale"][0], pj["endScale"][1], pj["endScale"][2] };
		}
		else {
			genParams_.endScale = { 1.0f, 1.0f, 1.0f };
		}
		if (pj.contains("anchorOffsetPos")) {
			genParams_.anchorOffsetPos = { pj["anchorOffsetPos"][0], pj["anchorOffsetPos"][1], pj["anchorOffsetPos"][2] };
		}
		else {
			genParams_.anchorOffsetPos = { 0.0f, 0.0f, 0.0f };
		}
		if (pj.contains("anchorOffsetRot")) {
			genParams_.anchorOffsetRot = { pj["anchorOffsetRot"][0], pj["anchorOffsetRot"][1], pj["anchorOffsetRot"][2] };
		}
		else {
			genParams_.anchorOffsetRot = { 0.0f, 0.0f, 0.0f };
		}
		genParams_.endEventID = pj.value("endEventID", 0);
		genParams_.endWaitTime = pj.value("endWaitTime", 0.0f);
		genParams_.startDurationToNext = pj.value("startDurationToNext", 1.0f);
		genParams_.startEasingToNext = pj.value("startEasingToNext", 0);
		if (pj.contains("waypoints") && pj["waypoints"].is_array()) {
			genParams_.waypoints.clear();
			for (const auto& wj : pj["waypoints"]) {
				GenerationParams::Waypoint wp;
				wp.pos = { wj["pos"][0], wj["pos"][1], wj["pos"][2] };
				wp.rot = { wj["rot"][0], wj["rot"][1], wj["rot"][2] };
				if (wj.contains("scale")) {
					wp.scale = { wj["scale"][0], wj["scale"][1], wj["scale"][2] };
				}
				else {
					wp.scale = { 1.0f, 1.0f, 1.0f };
				}
				wp.eventID = wj.value("eventID", 0);
				wp.waitTime = wj.value("waitTime", 0.0f);
				wp.durationToNext = wj.value("durationToNext", 1.0f);
				wp.easingToNext = wj.value("easingToNext", 0);
				genParams_.waypoints.push_back(wp);
			}
		}
		if (pj.contains("useSpline")) genParams_.useSpline = pj["useSpline"];


		if (pj.contains("generateRelative")) {
			genParams_.generateRelative = pj["generateRelative"];
			isRelative_ = genParams_.generateRelative;
		}


	}
}

void GhostRecorder::EvaluateAtFrame(int frameIndex) {
	ClearTargetIfMissingFromScene();
	if (frames_.empty() || !target_) return;

	const int lastFrameIndex = static_cast<int>(frames_.size()) - 1;
	frameIndex = std::clamp(frameIndex, 0, lastFrameIndex);
	ApplyFrameTransform(frames_[frameIndex]);
}
void GhostRecorder::CaptureBasePose() {
	ClearTargetIfMissingFromScene();
	if (!target_) {
		return;
	}
	FindAnchor();
	if (anchor_ && genParams_.generateRelative) {

		basePosition_ = {
			anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
			anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
			anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
		};
		// アンカーの回転にオフセットをクォータニオン合成
		baseRotation_ = AddEuler(anchor_->GetRotation(), genParams_.anchorOffsetRot);
		baseScale_ = target_->GetScale();
	}
	else if (target_) {
		basePosition_ = target_->GetTranslate();
		baseRotation_ = target_->GetRotation();
		baseScale_ = target_->GetScale();
	}
}

void GhostRecorder::RestoreBasePose() {
	ClearTargetIfMissingFromScene();
	if (target_ && isRelative_) {
		FindAnchor();

		if (anchor_) {
			target_->SetTranslate({
				anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
				anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
				anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
				});
			// アンカーの回転にオフセットをクォータニオン合成
			target_->SetRotation(AddEuler(anchor_->GetRotation(), genParams_.anchorOffsetRot));
		}
		else {
			// アンカーがいなければ今まで通り
			target_->SetTranslate(basePosition_);
			target_->SetRotation(baseRotation_);
		}

		target_->SetScale(baseScale_);
		target_->UpdateLocalMatrix();
		target_->UpdateWorldMatrix();
	}
}

void GhostRecorder::FindAnchor() {
	if (anchor_ && !IsObjectInScene(sceneManager_, anchor_)) {
		anchor_ = nullptr;
	}
	if (!anchorName_.empty() && !anchor_ && sceneManager_ && sceneManager_->GetCurrentScene()) {
		for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
			if (obj->GetName() == anchorName_) {
				anchor_ = obj.get();
				break;
			}
		}
	}
}

// ==========================================================================
// Undo / Redo 機能
// ==========================================================================
void GhostRecorder::SaveHistory() {
	undoStack_.push_back(genParams_);
	constexpr size_t kMaxHistoryEntries = 50;
	if (undoStack_.size() > kMaxHistoryEntries) {
		undoStack_.pop_front();
	}
	redoStack_.clear(); // 新しい操作をしたらRedoの履歴は消える
}

void GhostRecorder::PerformUndo() {
	if (undoStack_.empty()) return;

	redoStack_.push_back(genParams_); // 今の状態をRedoに積む
	genParams_ = undoStack_.back();   // 1個前の状態を取り出す
	undoStack_.pop_back();

	DeselectPin();

	DebugConsole::GetInstance()->AddLog("GhostRecorder: Undo!");
}

void GhostRecorder::PerformRedo() {
	if (redoStack_.empty()) return;

	undoStack_.push_back(genParams_); // 今の状態をUndoに積む
	genParams_ = redoStack_.back();   // 未来の状態を取り出す
	redoStack_.pop_back();

	DeselectPin();

	DebugConsole::GetInstance()->AddLog("GhostRecorder: Redo!");
}

void GhostRecorder::DeleteSelectedPin() {
	// StartとEndは消せないので、Waypoint(途中の点)だけ消せるようにする
	if (selectedPinType_ == SelectedPinType::Waypoint &&
		selectedWaypointIndex_ >= 0 &&
		selectedWaypointIndex_ < static_cast<int>(genParams_.waypoints.size())) {
		SaveHistory(); // Ctrl+Zで戻せるように履歴を保存
		genParams_.waypoints.erase(genParams_.waypoints.begin() + selectedWaypointIndex_);

		DeselectPin();
		DebugConsole::GetInstance()->AddLog("Waypoint Deleted!");
	}
}
