#define NOMINMAX
#include "GhostRecorder.h"
#include "imgui.h" 
#include "SceneManager.h" 
#include "BaseScene.h"
#include "CameraManager.h"
#include "Camera.h"
#include <fstream>
#include "json.hpp"
#include <cmath> 
#include <algorithm> 
#include <CameraEditor.h>
#include <DebugConsole.h>
#include"Easing.h"

using json = nlohmann::json;
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
    frames_.clear();
    state_ = State::Idle;

    isLoop_ = false;
    isRelative_ = true;
    currentFrameIndex_ = 0;

    genParams_ = GenerationParams();
    isShowPreview_ = true; // デフォルトで表示
    cameraManager_ = nullptr; // 初期化忘れずに
    isOverrideCamera_ = false;
}

void GhostRecorder::Update() {
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
            state_ = State::Idle;
            return;
        }

        if (currentFrameIndex_ < frames_.size()) {
            const auto& frame = frames_[currentFrameIndex_];

            if (isRelative_) {
                target_->SetTranslate({
                    basePosition_.x + frame.position.x,
                    basePosition_.y + frame.position.y,
                    basePosition_.z + frame.position.z
                    });
                target_->SetRotation({
                    baseRotation_.x + frame.rotation.x,
                    baseRotation_.y + frame.rotation.y,
                    baseRotation_.z + frame.rotation.z
                    });
            } else {
                target_->SetTranslate(frame.position);
                target_->SetRotation(frame.rotation);
            }
            target_->SetScale(frame.scale);
            // 座標を動かしたら即座に行列を更新して画面に反映させる！
			target_->UpdateLocalMatrix();
            target_->UpdateWorldMatrix();
            if (frame.eventID != 0) {
                target_->OnRecordEvent(frame.eventID);
            }
            if (isOverrideCamera_) {
                CameraEditor* camEditor = CameraEditor::GetInstance();
                if (camEditor) {
                    Vector3 camPos = target_->GetTranslate();
                    Vector3 camRot = target_->GetRotation();
                    camEditor->SetEditorCameraTransform(camPos, camRot);
                }
            }
            currentFrameIndex_++;
        } else {
      
            if (isLoop_) {
                currentFrameIndex_ = 0;
            } else {
                Stop();
            }
        }
    }
}

void GhostRecorder::Play(const std::string& fileName, bool loop, bool isRelative, bool isCinematic) {
    DebugConsole::GetInstance()->AddLog("GhostRecorder: Play called! File: " + fileName);
    Load(fileName);
    if (frames_.empty()) return;

    isLoop_ = loop;

    isRelative_ = genParams_.generateRelative;

    isOverrideCamera_ = isCinematic; // 演出の時だけカメラ乗っ取り

    // ターゲットの表示設定
    if (target_) {
        if (isCinematic) {
            target_->SetIsVisible(false);
        } else {
            target_->SetIsVisible(true);
        }
    }

    StartPlayingInternal();
}

void GhostRecorder::Stop(bool autoReset) {

    if (autoReset && state_ == State::Playing && isRelative_ && target_) {
        RestoreBasePose();
    }

    state_ = State::Idle;
    currentFrameIndex_ = 0;

    if (isOverrideCamera_) {
        CameraEditor* camEditor = CameraEditor::GetInstance();
        if (camEditor) {
            camEditor->SetMode(CameraEditor::Mode::Game);
            DebugConsole::GetInstance()->AddLog("Camera returned to Game Mode.");
        }
    }
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
    DebugConsole::GetInstance()->AddLog("GhostRecorder: StartPlayingInternal() called!");
    if (!target_ || frames_.empty()) {
        DebugConsole::GetInstance()->AddLog(" -> Aborted: 再生をキャンセルしました。");
        return;
    }
    DebugConsole::GetInstance()->AddLog(" -> Success: 再生を開始します!");
    state_ = State::Playing;
    currentFrameIndex_ = 0;

    // =======================================================
    //  再生開始時の基準点をアンカーから取得！
    // =======================================================
    FindAnchor();
    if (anchor_ && genParams_.generateRelative) {
        basePosition_ = {
            anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
            anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
            anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
        };
        baseRotation_ = {
            anchor_->GetRotation().x + genParams_.anchorOffsetRot.x,
            anchor_->GetRotation().y + genParams_.anchorOffsetRot.y,
            anchor_->GetRotation().z + genParams_.anchorOffsetRot.z
        };
    } else if (target_) {
        basePosition_ = target_->GetTranslate();
        baseRotation_ = target_->GetRotation();
    }

    if (isOverrideCamera_) {
        CameraEditor* camEditor = CameraEditor::GetInstance();
        if (camEditor) camEditor->SetMode(CameraEditor::Mode::Editor);
    }
}
// ==========================================================================
// 計算用ヘルパー
// ==========================================================================

Vector3 GhostRecorder::Lerp(const Vector3& start, const Vector3& end, float t) {
    return {
        start.x + (end.x - start.x) * t,
        start.y + (end.y - start.y) * t,
        start.z + (end.z - start.z) * t
    };
}

float GhostRecorder::SmoothStep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

Vector3 GhostRecorder::CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
    Vector3 result;
    float t2 = t * t;
    float t3 = t * t * t;
    result.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
    result.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
    result.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
    return result;
}

Vector3 GhostRecorder::GetSplinePoint(const std::vector<Vector3>& points, float t, bool isLoop) {
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
Vector3 GhostRecorder::TransformCoord(const Vector3& vec, const Matrix4x4& mat) {
    Vector3 result;
    // 行列乗算 (Row-major前提: vec * mat)
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
void GhostRecorder::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size) {
#ifdef USE_IMGUI
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

    // --- 2. 描画位置のオフセット(基準点)計算 ---
    Vector3 drawOffset = { 0, 0, 0 };
    Vector3 drawRotOffset = { 0, 0, 0 };

    // =======================================================
    // ★大修正：isRelative_ ではなく、UIのチェックボックス状態を直接見る！
    // =======================================================
    if (genParams_.generateRelative && target_) {
        FindAnchor();

        if (state_ == State::Playing || isScrubbing_) {
            // 再生中・シーク中はロックされた基準位置に追従
            drawOffset = {
                basePosition_.x - genParams_.startPos.x,
                basePosition_.y - genParams_.startPos.y,
                basePosition_.z - genParams_.startPos.z
            };
            drawRotOffset = {
                baseRotation_.x - genParams_.startRot.x,
                baseRotation_.y - genParams_.startRot.y,
                baseRotation_.z - genParams_.startRot.z
            };
        } else {
            Vector3 currentBase;
            Vector3 currentBaseRot;

            if (anchor_) {
                // アンカーがいる場合は、アンカーの現在地＋オフセットを基準にする
                currentBase = {
                    anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
                    anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
                    anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
                };
                currentBaseRot = {
                    anchor_->GetRotation().x + genParams_.anchorOffsetRot.x,
                    anchor_->GetRotation().y + genParams_.anchorOffsetRot.y,
                    anchor_->GetRotation().z + genParams_.anchorOffsetRot.z
                };
            } else {
                // アンカーがいない場合は、ターゲット自身の現在地を基準にする
                currentBase = target_->GetTranslate();
                currentBaseRot = target_->GetRotation();
            }

            drawOffset = {
                currentBase.x - genParams_.startPos.x,
                currentBase.y - genParams_.startPos.y,
                currentBase.z - genParams_.startPos.z
            };
            drawRotOffset = {
                currentBaseRot.x - genParams_.startRot.x,
                currentBaseRot.y - genParams_.startRot.y,
                currentBaseRot.z - genParams_.startRot.z
            };
        }
    }

    auto applyOffset = [&](const Vector3& p) {
        return Vector3{ p.x + drawOffset.x, p.y + drawOffset.y, p.z + drawOffset.z };
        };
    auto applyRotOffset = [&](const Vector3& r) {
        return Vector3{ r.x + drawRotOffset.x, r.y + drawRotOffset.y, r.z + drawRotOffset.z };
        };

    // --- 3. パスを構成する全頂点のリストを作成 ---
    std::vector<Vector3> allPos;
    std::vector<Vector3> allRot;

    allPos.push_back(applyOffset(genParams_.startPos));
    allRot.push_back(applyRotOffset(genParams_.startRot));
    for (const auto& wp : genParams_.waypoints) {
        allPos.push_back(applyOffset(wp.pos));
        allRot.push_back(applyRotOffset(wp.rot));
    }
    allPos.push_back(applyOffset(genParams_.endPos));
    allRot.push_back(applyRotOffset(genParams_.endRot));

    if (allPos.size() < 2) return;

    // --- 4. パスの軌跡(ライン)を描画 ---
    const int samples = 60;
    ImVec2 prevScreenPos = WorldToScreen(LocalToWorld(allPos[0]));
    for (int i = 1; i <= samples; ++i) {
        float t = (float)i / (float)samples;
        Vector3 currentPos = genParams_.useSpline ? GetSplinePoint(allPos, t, false) :
            [&]() {
            float p = t * (allPos.size() - 1);
            int idx = (int)p;
            float lt = p - idx;
            if (idx >= (int)allPos.size() - 1) { idx = (int)allPos.size() - 2; lt = 1.0f; }
            return Lerp(allPos[idx], allPos[idx + 1], lt);
            }();

        ImVec2 screenPos = WorldToScreen(LocalToWorld(currentPos));
        if (screenPos.x > -5000.0f && prevScreenPos.x > -5000.0f) {
            drawList->AddLine(prevScreenPos, screenPos, IM_COL32(255, 255, 255, 100), 1.5f);
        }
        prevScreenPos = screenPos;
    }

    // --- 5. 進行方向を示す矢印の描画 ---
    const int arrowSteps = 10;
    for (int i = 0; i <= arrowSteps; ++i) {
        float t = (float)i / (float)arrowSteps;

        Vector3 pos, rot;
        float p = t * (allPos.size() - 1);
        int idx = (int)p;
        float lt = p - idx;
        if (idx >= (int)allPos.size() - 1) { idx = (int)allPos.size() - 2; lt = 1.0f; }

        pos = genParams_.useSpline ? GetSplinePoint(allPos, t, false) : Lerp(allPos[idx], allPos[idx + 1], lt);
        rot = Lerp(allRot[idx], allRot[idx + 1], lt);

        ImVec2 baseScr = WorldToScreen(LocalToWorld(pos));
        if (baseScr.x < -5000.0f) continue;

        float cosX = cosf(rot.x);
        Vector3 forward = { cosX * sinf(rot.y), -sinf(rot.x), cosX * cosf(rot.y) };
        Vector3 lookTarget = { pos.x + forward.x * 3.0f, pos.y + forward.y * 3.0f, pos.z + forward.z * 3.0f };
        ImVec2 lookScr = WorldToScreen(LocalToWorld(lookTarget));

        if (lookScr.x > -5000.0f) {
            drawList->AddLine(baseScr, lookScr, IM_COL32(0, 255, 255, 200), 2.0f);
            drawList->AddCircleFilled(lookScr, 2.0f, IM_COL32(0, 255, 255, 255));
        }

        if (i < arrowSteps) {
            float nextT = t + 0.02f;
            Vector3 nPos = genParams_.useSpline ? GetSplinePoint(allPos, nextT, false) : Lerp(allPos[idx], allPos[idx + 1], lt + 0.02f);
            ImVec2 nScr = WorldToScreen(LocalToWorld(nPos));
            if (nScr.x > -5000.0f) {
                float dx = nScr.x - baseScr.x, dy = nScr.y - baseScr.y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 0.1f) {
                    dx /= len; dy /= len;
                    float sz = 8.0f;
                    drawList->AddTriangleFilled(
                        ImVec2(baseScr.x + dx * sz, baseScr.y + dy * sz),
                        ImVec2(baseScr.x - dy * sz * 0.4f, baseScr.y + dx * sz * 0.4f),
                        ImVec2(baseScr.x + dy * sz * 0.4f, baseScr.y - dx * sz * 0.4f),
                        IM_COL32(255, 255, 0, 180)
                    );
                }
            }
        }
    }

    // --- 6. アニメーションして流れる光点の描画 ---
    for (int i = 0; i < 5; ++i) {
        float t = fmodf(time * 0.4f + (float)i / 5.0f, 1.0f);
        Vector3 pPos = genParams_.useSpline ? GetSplinePoint(allPos, t, false) :
            [&]() {
            float p = t * (allPos.size() - 1);
            int idx = (int)p;
            float lt = p - idx;
            if (idx >= (int)allPos.size() - 1) { idx = (int)allPos.size() - 2; lt = 1.0f; }
            return Lerp(allPos[idx], allPos[idx + 1], lt);
            }();
        ImVec2 pScr = WorldToScreen(LocalToWorld(pPos));
        if (pScr.x > -5000.0f) drawList->AddCircleFilled(pScr, 3.0f, IM_COL32(255, 255, 0, 200));
    }

    // --- 7. 各ノード(Start/End/Waypoints)とイベントIDの描画 ---
    ImVec2 startScr = WorldToScreen(LocalToWorld(allPos[0]));
    if (startScr.x > -5000.0f) {
        drawList->AddCircleFilled(startScr, 6.0f, IM_COL32(0, 255, 0, 255));
        std::string startText = "[START]";
        if (genParams_.startEventID != 0) {
            startText += "  [Event: " + std::to_string(genParams_.startEventID) + "]";
            drawList->AddText(ImVec2(startScr.x + 10, startScr.y - 15), IM_COL32(255, 100, 100, 255), startText.c_str());
        } else {
            drawList->AddText(ImVec2(startScr.x + 10, startScr.y - 15), IM_COL32(0, 255, 0, 255), startText.c_str());
        }
    }

    ImVec2 endScr = WorldToScreen(LocalToWorld(allPos.back()));
    if (endScr.x > -5000.0f) {
        drawList->AddCircleFilled(endScr, 6.0f, IM_COL32(100, 100, 255, 255));
        std::string endText = "[END]";
        if (genParams_.endEventID != 0) {
            endText += "  [Event: " + std::to_string(genParams_.endEventID) + "]";
            drawList->AddText(ImVec2(endScr.x + 10, endScr.y - 15), IM_COL32(255, 100, 100, 255), endText.c_str());
        } else {
            drawList->AddText(ImVec2(endScr.x + 10, endScr.y - 15), IM_COL32(100, 100, 255, 255), endText.c_str());
        }
    }

    for (int i = 0; i < (int)genParams_.waypoints.size(); ++i) {
        ImVec2 wpScr = WorldToScreen(LocalToWorld(allPos[i + 1]));
        if (wpScr.x > -5000.0f) {
            std::string wpText = "P" + std::to_string(i + 1);
            if (genParams_.waypoints[i].eventID != 0) {
                drawList->AddCircleFilled(wpScr, 6.0f, IM_COL32(255, 165, 0, 255));
                wpText += "  [Event: " + std::to_string(genParams_.waypoints[i].eventID) + "]";
                drawList->AddText(ImVec2(wpScr.x + 8, wpScr.y - 12), IM_COL32(255, 165, 0, 255), wpText.c_str());
            } else {
                drawList->AddCircleFilled(wpScr, 4.0f, IM_COL32(255, 255, 0, 255));
                drawList->AddText(ImVec2(wpScr.x + 8, wpScr.y - 12), IM_COL32(255, 255, 0, 255), wpText.c_str());
            }
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
    static char fName[64] = "anim_path";

    // -------------------------------------------------------------
    // 1. ファイル管理 (File IO)
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader("ファイル管理 (File IO)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string dirPath = "Resources/json/animation/";
        if (std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath)) {
            if (ImGui::BeginCombo("既存データからロード", fName)) {
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
        ImGui::InputText("ファイル名", fName, sizeof(fName));

        if (ImGui::Button("Save")) Save(fName);
        ImGui::SameLine();
        if (ImGui::Button("Load")) Load(fName);
    }
    ImGui::Separator();

    // -------------------------------------------------------------
    // 2. ターゲット & アンカー選択
    // -------------------------------------------------------------
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        std::string currentTargetName = target_ ? target_->GetName() : "(未選択)";
        if (ImGui::BeginCombo("ターゲット", currentTargetName.c_str())) {
            for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                bool isSelected = (target_ == obj.get());
                if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) target_ = obj.get();
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        std::string currentAnchorName = anchorName_.empty() ? "(なし: 自身を基準)" : anchorName_;
        if (ImGui::BeginCombo("アンカー(相対基準)", currentAnchorName.c_str())) {
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
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (anchor_ && genParams_.generateRelative) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "▼ アンカーからの相対距離 (微調整用)");
        ImGui::DragFloat3("Offset Pos", &genParams_.anchorOffsetPos.x, 0.1f);
        ImGui::DragFloat3("Offset Rot", &genParams_.anchorOffsetRot.x, 1.0f);
        ImGui::Unindent();
    }

    const char* stateStr = (state_ == State::Recording) ? "録画中" : (state_ == State::Playing) ? "再生中" : "待機中";
    ImGui::Text("状態: %s | フレーム: %d", stateStr, (int)frames_.size());
    ImGui::Separator();

    // -------------------------------------------------------------
    // 3. 手動録画
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader("手動録画", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state_ == State::Idle) {
            if (!target_) ImGui::BeginDisabled();
            if (ImGui::Button("● 録画開始")) StartRecording();
            if (!target_) ImGui::EndDisabled();
        } else if (state_ == State::Recording) {
            if (ImGui::Button("■ 録画停止")) StopRecording();
        }
    }

    // -------------------------------------------------------------
    // 4. 自動生成 (Path Editor)
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader("パス生成 (Path Editor)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (target_) {
            ImGui::Checkbox("プレビュー線を表示", &isShowPreview_);

            static const char* easingNames[] = {
                "Linear(等速)", "InSine", "OutSine", "InOutSine", "InQuad", "OutQuad", "InOutQuad",
                "InCubic", "OutCubic", "InOutCubic", "InQuart", "OutQuart", "InOutQuart",
                "InQuint", "OutQuint", "InOutQuint", "InExpo", "OutExpo", "InOutExpo",
                "InCirc", "OutCirc", "InOutCirc"
            };

            // --- Start 設定 ---
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[ START ]");
            ImGui::SameLine();
            if (ImGui::Button("現在地で上書き##Start")) {
                genParams_.startPos = target_->GetTranslate();
                genParams_.startRot = target_->GetRotation();
                genParams_.startScale = target_->GetScale();
            }
            ImGui::SameLine();
            if (ImGui::Button("ワープ(Warp)##Start")) {
                target_->SetTranslate(genParams_.startPos);
                target_->SetRotation(genParams_.startRot);
                target_->UpdateWorldMatrix();
            }
            ImGui::DragFloat3("Pos##Start", &genParams_.startPos.x, 0.1f);
            ImGui::DragFloat3("Rot##Start", &genParams_.startRot.x, 1.0f);
            ImGui::DragFloat3("Scale##Start", &genParams_.startScale.x, 0.1f);
            ImGui::InputInt("Event ID##Start", &genParams_.startEventID);
            ImGui::DragFloat("待機(sec)##Start", &genParams_.startWaitTime, 0.1f, 0.0f, 10.0f);
            ImGui::DragFloat("次への移動時間(sec)##Start", &genParams_.startDurationToNext, 0.1f, 0.1f, 10.0f);
            ImGui::Combo("次へのイージング##Start", &genParams_.startEasingToNext, easingNames, IM_ARRAYSIZE(easingNames));

            // --- Waypoints 設定 ---
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "[ WAYPOINTS ]");

            int waypointToDelete = -1; // 削除を安全に行うための予約変数

            for (int i = 0; i < genParams_.waypoints.size(); ++i) {
                ImGui::PushID(i);
                ImGui::Text("P%d", i + 1);
                ImGui::SameLine();

                if (ImGui::Button("ワープ")) {
                    target_->SetTranslate(genParams_.waypoints[i].pos);
                    target_->SetRotation(genParams_.waypoints[i].rot);
                    target_->UpdateWorldMatrix();
                }
                ImGui::SameLine();
                if (ImGui::Button("上書き")) {
                    genParams_.waypoints[i].pos = target_->GetTranslate();
                    genParams_.waypoints[i].rot = target_->GetRotation();
                    genParams_.waypoints[i].scale = target_->GetScale();
                }
                ImGui::SameLine();
                if (ImGui::Button("Del")) {
                    waypointToDelete = i;
                }

                ImGui::DragFloat3("Pos", &genParams_.waypoints[i].pos.x, 0.1f);
                ImGui::DragFloat3("Rot", &genParams_.waypoints[i].rot.x, 1.0f);
                ImGui::DragFloat3("Scale", &genParams_.waypoints[i].scale.x, 0.1f);
                ImGui::InputInt("Event ID", &genParams_.waypoints[i].eventID);
                ImGui::DragFloat("待機(sec)", &genParams_.waypoints[i].waitTime, 0.1f, 0.0f, 10.0f);
                ImGui::DragFloat("次への移動時間(sec)", &genParams_.waypoints[i].durationToNext, 0.1f, 0.1f, 10.0f);
                ImGui::Combo("次へのイージング", &genParams_.waypoints[i].easingToNext, easingNames, IM_ARRAYSIZE(easingNames));
                ImGui::Separator();
                ImGui::PopID();
            }

            if (waypointToDelete >= 0) {
                genParams_.waypoints.erase(genParams_.waypoints.begin() + waypointToDelete);
            }

            if (ImGui::Button("+ 現在地を追加")) {
                GenerationParams::Waypoint wp;
                wp.pos = target_->GetTranslate();
                wp.rot = target_->GetRotation();
                wp.scale = target_->GetScale();
                wp.eventID = 0;
                wp.waitTime = 0.0f;
                wp.durationToNext = 1.0f;
                wp.easingToNext = 0;
                genParams_.waypoints.push_back(wp);
            }

            // --- End 設定 ---
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "[ END ]");
            ImGui::SameLine();
            if (ImGui::Button("現在地で上書き##End")) {
                genParams_.endPos = target_->GetTranslate();
                genParams_.endRot = target_->GetRotation();
                genParams_.endScale = target_->GetScale();
            }
            ImGui::SameLine();
            if (ImGui::Button("ワープ(Warp)##End")) {
                target_->SetTranslate(genParams_.endPos);
                target_->SetRotation(genParams_.endRot);
                target_->UpdateWorldMatrix();
            }
            ImGui::SameLine();
            if (ImGui::Button("Startと同期(Loop)##End")) {
                genParams_.endPos = genParams_.startPos;
                genParams_.endRot = genParams_.startRot;
            }
            ImGui::DragFloat3("Pos##End", &genParams_.endPos.x, 0.1f);
            ImGui::DragFloat3("Rot##End", &genParams_.endRot.x, 1.0f);
            ImGui::DragFloat3("Scale##End", &genParams_.endScale.x, 0.1f);
            ImGui::InputInt("Event ID##End", &genParams_.endEventID);
            ImGui::DragFloat("待機(sec)##End", &genParams_.endWaitTime, 0.1f, 0.0f, 10.0f);

            ImGui::Separator();

            // --- 生成パラメータ ---
            ImGui::Checkbox("スプライン曲線", &genParams_.useSpline);
            ImGui::SameLine();
            ImGui::Checkbox("相対データ化", &genParams_.generateRelative);

            // =========================================================================
            // ★ 生成ロジック（区間ごとの完全リレー方式）
            // =========================================================================
            if (ImGui::Button("★ 生成実行 (Generate & AutoSave)", ImVec2(-1, 40))) {
                frames_.clear();
                FindAnchor();
                if (anchor_ && genParams_.generateRelative) {
                    genParams_.anchorOffsetPos = {
                        genParams_.startPos.x - anchor_->GetTranslate().x,
                        genParams_.startPos.y - anchor_->GetTranslate().y,
                        genParams_.startPos.z - anchor_->GetTranslate().z
                    };
                    genParams_.anchorOffsetRot = {
                        genParams_.startRot.x - anchor_->GetRotation().x,
                        genParams_.startRot.y - anchor_->GetRotation().y,
                        genParams_.startRot.z - anchor_->GetRotation().z
                    };
                } else {
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
                        f.rotation = { pts[i].rot.x - rotOffset.x, pts[i].rot.y - rotOffset.y, pts[i].rot.z - rotOffset.z };
                        f.scale = pts[i].scale;
                        f.eventID = (w == 0) ? pts[i].eventID : 0;
                        frames_.push_back(f);
                    }

                    if (i == (int)pts.size() - 1) {
                        if (waitFrames == 0) {
                            GhostFrame f;
                            f.position = { pts[i].pos.x - offset.x, pts[i].pos.y - offset.y, pts[i].pos.z - offset.z };
                            f.rotation = { pts[i].rot.x - rotOffset.x, pts[i].rot.y - rotOffset.y, pts[i].rot.z - rotOffset.z };
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
                        } else {
                            currentPos = Lerp(pts[i].pos, pts[i + 1].pos, t);
                        }
                        Vector3 currentRot = Lerp(pts[i].rot, pts[i + 1].rot, t);
                        Vector3 currentScale = Lerp(pts[i].scale, pts[i + 1].scale, t);

                        GhostFrame frame;
                        frame.position = { currentPos.x - offset.x, currentPos.y - offset.y, currentPos.z - offset.z };
                        frame.rotation = { currentRot.x - rotOffset.x, currentRot.y - rotOffset.y, currentRot.z - rotOffset.z };
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

        } else {
            ImGui::TextDisabled("ターゲットを選択してください");
        }
    }

    ImGui::Separator();
    ImGui::Checkbox("再生時にカメラを乗っ取る (Cinema Mode)", &isOverrideCamera_);
    ImGui::Separator();

    // -------------------------------------------------------------
    // 5. 再生制御 (シークバー ＆ 実行)
    // -------------------------------------------------------------
    if (!frames_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 1.0f, 1.0f), "[ タイムライン ]");

        int maxFrame = static_cast<int>(frames_.size()) - 1;
        int displayFrame = static_cast<int>(currentFrameIndex_);
        if (displayFrame > maxFrame) displayFrame = maxFrame;

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##SeekBar", &displayFrame, 0, maxFrame, "Frame: %d");

        if (ImGui::IsItemActivated()) {
            isScrubbing_ = true;
            state_ = State::Idle;
            FindAnchor();
            if (anchor_ && genParams_.generateRelative) {
                basePosition_ = {
                    anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
                    anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
                    anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
                };
                baseRotation_ = {
                    anchor_->GetRotation().x + genParams_.anchorOffsetRot.x,
                    anchor_->GetRotation().y + genParams_.anchorOffsetRot.y,
                    anchor_->GetRotation().z + genParams_.anchorOffsetRot.z
                };
            } else if (target_) {
                basePosition_ = target_->GetTranslate();
                baseRotation_ = target_->GetRotation();
            }
        }
        if (ImGui::IsItemActive()) {
            currentFrameIndex_ = static_cast<decltype(currentFrameIndex_)>(displayFrame);
            EvaluateAtFrame(displayFrame);
        }
        if (ImGui::IsItemDeactivated()) {
            isScrubbing_ = false;
        }
        ImGui::Separator();
    }

    ImGui::Checkbox("Loop再生", &isLoop_);
    ImGui::SameLine();
    if (genParams_.generateRelative) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[相対データ]");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "[絶対データ]");
    }

    if (state_ != State::Recording) {
        if (!target_) ImGui::BeginDisabled();

        if (ImGui::Button("メモリから再生 (生成直後用)")) {
            StartPlayingInternal();
        }
        ImGui::SameLine();
        if (ImGui::Button("ファイルからPlay (本番用)")) {
            bool isCinematic = (target_->GetClassName() == "CinematicCamera");
            Play(fName, isLoop_, isRelative_, isCinematic);
        }

        if (!target_) ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("停止")) Stop();

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
    } else {
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
            } else {
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
        } else {
            genParams_.startScale = { 1.0f, 1.0f, 1.0f };
        }

        genParams_.startEventID = pj.value("startEventID", 0);
        genParams_.startWaitTime = pj.value("startWaitTime", 0.0f);


        if (pj.contains("endPos"))   genParams_.endPos = { pj["endPos"][0], pj["endPos"][1], pj["endPos"][2] };
        if (pj.contains("endRot"))   genParams_.endRot = { pj["endRot"][0], pj["endRot"][1], pj["endRot"][2] };
        if (pj.contains("endScale")) {
            genParams_.endScale = { pj["endScale"][0], pj["endScale"][1], pj["endScale"][2] };
        } else {
            genParams_.endScale = { 1.0f, 1.0f, 1.0f };
        }
        if (pj.contains("anchorOffsetPos")) {
            genParams_.anchorOffsetPos = { pj["anchorOffsetPos"][0], pj["anchorOffsetPos"][1], pj["anchorOffsetPos"][2] };
        } else {
            genParams_.anchorOffsetPos = { 0.0f, 0.0f, 0.0f };
        }
        if (pj.contains("anchorOffsetRot")) {
            genParams_.anchorOffsetRot = { pj["anchorOffsetRot"][0], pj["anchorOffsetRot"][1], pj["anchorOffsetRot"][2] };
        } else {
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
                } else {
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
    if (frames_.empty() || !target_) return;

    if (frameIndex < 0) frameIndex = 0;
    if (frameIndex >= frames_.size()) frameIndex = static_cast<int>(frames_.size()) - 1;

    const auto& frame = frames_[frameIndex];

    if (isRelative_) {
        target_->SetTranslate({
            basePosition_.x + frame.position.x,
            basePosition_.y + frame.position.y,
            basePosition_.z + frame.position.z
            });
        target_->SetRotation({
            baseRotation_.x + frame.rotation.x,
            baseRotation_.y + frame.rotation.y,
            baseRotation_.z + frame.rotation.z
            });
    } else {
        target_->SetTranslate(frame.position);
        target_->SetRotation(frame.rotation);
    }
    target_->SetScale(frame.scale);
    target_->UpdateLocalMatrix();
    target_->UpdateWorldMatrix();
}
void GhostRecorder::CaptureBasePose() {
    FindAnchor();
    if (anchor_ && genParams_.generateRelative) {
        
        basePosition_ = {
            anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
            anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
            anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
        };
        baseRotation_ = {
            anchor_->GetRotation().x + genParams_.anchorOffsetRot.x,
            anchor_->GetRotation().y + genParams_.anchorOffsetRot.y,
            anchor_->GetRotation().z + genParams_.anchorOffsetRot.z
        };
        baseScale_ = target_ ? target_->GetScale() : Vector3{ 1.0f, 1.0f, 1.0f };
    } else if (target_) {
        basePosition_ = target_->GetTranslate();
        baseRotation_ = target_->GetRotation();
        baseScale_ = target_->GetScale();
    }
}

void GhostRecorder::RestoreBasePose() {
    if (target_ && isRelative_) {
        FindAnchor(); 

        if (anchor_) {
            target_->SetTranslate({
                anchor_->GetTranslate().x + genParams_.anchorOffsetPos.x,
                anchor_->GetTranslate().y + genParams_.anchorOffsetPos.y,
                anchor_->GetTranslate().z + genParams_.anchorOffsetPos.z
                });
            target_->SetRotation({
                anchor_->GetRotation().x + genParams_.anchorOffsetRot.x,
                anchor_->GetRotation().y + genParams_.anchorOffsetRot.y,
                anchor_->GetRotation().z + genParams_.anchorOffsetRot.z
                });
        } else {
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
    if (!anchorName_.empty() && !anchor_ && sceneManager_ && sceneManager_->GetCurrentScene()) {
        for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (obj->GetName() == anchorName_) {
                anchor_ = obj.get();
                break;
            }
        }
    }
}