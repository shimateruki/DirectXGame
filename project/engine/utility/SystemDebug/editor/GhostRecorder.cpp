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
// DrawPreview (可視化機能)
// ==========================================================================

// ==========================================================================
// DrawPreview (パスとイベントの可視化機能)
// ==========================================================================
void GhostRecorder::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size) {
#ifdef USE_IMGUI
    if (!isShowPreview_ || !target_) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    float time = (float)ImGui::GetTime();

    // --- 1. 座標変換用のヘルパー関数 ---

    // ワールド座標をスクリーン(画面)座標に変換
    auto WorldToScreen = [&](const Vector3& worldPos) -> ImVec2 {
        Vector3 clip = TransformCoord(worldPos, viewProjection);
        if (clip.z < 0.0f || clip.z > 1.0f) return ImVec2(-10000.0f, -10000.0f);
        float screenX = offset.x + (clip.x + 1.0f) * 0.5f * size.x;
        float screenY = offset.y + (1.0f - clip.y) * 0.5f * size.y;
        return ImVec2(screenX, screenY);
        };

    // ローカル座標を親(または自身)のワールド座標に変換
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

    if (isRelative_ && target_) {
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
         
                currentBase = target_->GetTranslate();
                currentBaseRot = target_->GetRotation();
            }

            // 決定した基準点から、Start地点との差分（オフセット）を計算する
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
        t = ApplyEasing(genParams_.easingType, t);

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

    // Start ノード
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

    // End ノード
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

    // Waypoints ノード
    for (int i = 0; i < (int)genParams_.waypoints.size(); ++i) {
        ImVec2 wpScr = WorldToScreen(LocalToWorld(allPos[i + 1]));
        if (wpScr.x > -5000.0f) {
            std::string wpText = "P" + std::to_string(i + 1);
            if (genParams_.waypoints[i].eventID != 0) {
                // イベントがある地点はピンを少し大きくして色を変える
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
    // ★ 1. ファイル管理 (上部に配置してコンボボックス化)
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
                            Load(fName); // 選んだ瞬間にロードして復元
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
    // 2. ターゲット選択
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

            // --- Waypoints 設定 ---
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "[ WAYPOINTS ]");

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
                    genParams_.waypoints.erase(genParams_.waypoints.begin() + i);
                    ImGui::PopID();
                    break;
                }

                ImGui::DragFloat3("Pos", &genParams_.waypoints[i].pos.x, 0.1f);
                ImGui::DragFloat3("Rot", &genParams_.waypoints[i].rot.x, 1.0f);
                ImGui::DragFloat3("Scale", &genParams_.waypoints[i].scale.x, 0.1f);
                ImGui::InputInt("Event ID", &genParams_.waypoints[i].eventID);
                ImGui::DragFloat("待機(sec)", &genParams_.waypoints[i].waitTime, 0.1f, 0.0f, 10.0f);
                ImGui::Separator();
                ImGui::PopID();
            }

            if (ImGui::Button("+ 現在地を追加")) {
                GenerationParams::Waypoint wp;
                wp.pos = target_->GetTranslate();
                wp.rot = target_->GetRotation();
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
            ImGui::DragFloat("時間 (sec)", &genParams_.duration, 0.1f, 0.1f, 120.0f);

            static const char* easingNames[] = {
                "Linear (等速)",
                "InSine", "OutSine", "InOutSine",
                "InQuad", "OutQuad", "InOutQuad",
                "InCubic", "OutCubic", "InOutCubic",
                "InQuart", "OutQuart", "InOutQuart",
                "InQuint", "OutQuint", "InOutQuint",
                "InExpo", "OutExpo", "InOutExpo",
                "InCirc", "OutCirc", "InOutCirc"
            };
            ImGui::Combo("イージング", &genParams_.easingType, easingNames, IM_ARRAYSIZE(easingNames));

            if (ImGui::Button("★ 生成実行 (Generate & AutoSave)", ImVec2(-1, 40))) {
                frames_.clear();
                int totalFrames = static_cast<int>(genParams_.duration * 60.0f);
                if (totalFrames < 1) totalFrames = 1;
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

                std::vector<Vector3> posPoints = { genParams_.startPos };
                for (auto& wp : genParams_.waypoints) posPoints.push_back(wp.pos);
                posPoints.push_back(genParams_.endPos);

                std::vector<Vector3> rotPoints = { genParams_.startRot };
                for (auto& wp : genParams_.waypoints) rotPoints.push_back(wp.rot);
                rotPoints.push_back(genParams_.endRot);

                std::vector<Vector3> scalePoints = { genParams_.startScale };
                for (auto& wp : genParams_.waypoints) scalePoints.push_back(wp.scale);
                scalePoints.push_back(genParams_.endScale);

                Vector3 offset = genParams_.generateRelative ? genParams_.startPos : Vector3{ 0,0,0 };
                Vector3 rotOffset = genParams_.generateRelative ? genParams_.startRot : Vector3{ 0,0,0 };

                // 【STEP 1】まずは待機時間なしの純粋な移動フレーム(temp)を作る
                std::vector<GhostFrame> tempFrames;
                std::vector<float> waitTimes(totalFrames + 1, 0.0f); // 待機時間記録用

                for (int i = 0; i <= totalFrames; ++i) {
                    float t = (float)i / (float)totalFrames;
                    t = ApplyEasing(genParams_.easingType, t);

                    Vector3 pos;
                    if (genParams_.useSpline) {
                        pos = GetSplinePoint(posPoints, t, false);
                    } else {
                        float p = t * (posPoints.size() - 1);
                        int idx = (int)p;
                        float lt = p - idx;
                        if (idx >= posPoints.size() - 1) { idx = static_cast<int>(posPoints.size()) - 2; lt = 1.0f; }
                        pos = Lerp(posPoints[idx], posPoints[idx + 1], lt);
                    }

                    Vector3 rot;
                    float rp = t * (rotPoints.size() - 1);
                    int rIdx = (int)rp;
                    float rLt = rp - rIdx;
                    if (rIdx >= rotPoints.size() - 1) { rIdx = static_cast<int>(rotPoints.size()) - 2; rLt = 1.0f; }
                    rot = Lerp(rotPoints[rIdx], rotPoints[rIdx + 1], rLt);
                    Vector3 scl;
                    float sp = t * (scalePoints.size() - 1);
                    int sIdx = (int)sp;
                    float sLt = sp - sIdx;
                    if (sIdx >= scalePoints.size() - 1) { sIdx = static_cast<int>(scalePoints.size()) - 2; sLt = 1.0f; }
                    scl = Lerp(scalePoints[sIdx], scalePoints[sIdx + 1], sLt);


                    GhostFrame f;
                    f.position = { pos.x - offset.x, pos.y - offset.y, pos.z - offset.z };
                    f.rotation = { rot.x - rotOffset.x, rot.y - rotOffset.y, rot.z - rotOffset.z };
                    f.scale = scl;
                    f.eventID = 0;
                    tempFrames.push_back(f);
                }

                // イベントIDと待機時間を該当フレームに紐付け
                tempFrames[0].eventID = genParams_.startEventID;
                waitTimes[0] = genParams_.startWaitTime;

                tempFrames.back().eventID = genParams_.endEventID;
                waitTimes.back() = genParams_.endWaitTime;

                int numSegments = static_cast<int>(genParams_.waypoints.size()) + 1;
                for (int i = 0; i < static_cast<int>(genParams_.waypoints.size()); ++i) {
                    float t = (float)(i + 1) / (float)numSegments;
                    int frameIndex = static_cast<int>(std::round(t * totalFrames));
                    if (frameIndex >= 0 && frameIndex <= totalFrames) {
                        tempFrames[frameIndex].eventID = genParams_.waypoints[i].eventID;
                        waitTimes[frameIndex] = genParams_.waypoints[i].waitTime;
                    }
                }

                // 【STEP 2】待機時間を反映させて最終フレーム群(frames_)を構築
                for (size_t i = 0; i < tempFrames.size(); ++i) {
                    frames_.push_back(tempFrames[i]); // 通常の移動フレーム

                    // 待機時間が設定されていたら、その場でフレームを複製して一時停止させる！
                    if (waitTimes[i] > 0.0f) {
                        int waitFrameCount = static_cast<int>(waitTimes[i] * 60.0f);
                        for (int w = 0; w < waitFrameCount; ++w) {
                            GhostFrame wf = tempFrames[i];
                            wf.eventID = 0; // イベントは最初の1回だけ発火させるため0にする
                            frames_.push_back(wf);
                        }
                    }
                }

                isRelative_ = genParams_.generateRelative;
                Stop();
                Save(fName);
                ImGui::OpenPopup("Done");
            }
            if (ImGui::BeginPopup("Done")) { ImGui::Text("生成＆セーブ完了!"); ImGui::EndPopup(); }

        } else {
            ImGui::TextDisabled("ターゲットを選択してください");
        }
    }

    ImGui::Separator();
    ImGui::Checkbox("再生時にカメラを乗っ取る (Cinema Mode)", &isOverrideCamera_);
    ImGui::Separator();

    // -------------------------------------------------------------
    // 5. 再生制御
    // -------------------------------------------------------------
    if (!frames_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 1.0f, 1.0f), "[ タイムライン ]");

        int maxFrame = static_cast<int>(frames_.size()) - 1;
        int displayFrame = static_cast<int>(currentFrameIndex_);
        if (displayFrame > maxFrame) displayFrame = maxFrame;

        // ★ シークバー (スライダー) の描画
        ImGui::SetNextItemWidth(-1); // 横幅いっぱいに広げる
        ImGui::SliderInt("##SeekBar", &displayFrame, 0, maxFrame, "Frame: %d");

        // --- シーク操作の裏側処理 ---
        if (ImGui::IsItemActivated()) {
            // スライダーを「掴んだ瞬間」に、今のアンカー位置を基準点としてロックする
            isScrubbing_ = true;
            state_ = State::Idle; // 再生中なら止める
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
            // スライダーを「動かしている最中」は、そのフレームの姿勢を反映させる
            currentFrameIndex_ = static_cast<decltype(currentFrameIndex_)>(displayFrame);
            EvaluateAtFrame(displayFrame);
        }
        if (ImGui::IsItemDeactivated()) {
            // スライダーから「マウスを離した瞬間」
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
    paramsJson["waypoints"] = json::array();
    for (const auto& wp : genParams_.waypoints) {
        paramsJson["waypoints"].push_back({
            {"pos", {wp.pos.x, wp.pos.y, wp.pos.z}},
            {"rot", {wp.rot.x, wp.rot.y, wp.rot.z}},
            {"scale", {wp.scale.x, wp.scale.y, wp.scale.z}}, 
            {"eventID", wp.eventID},
            {"waitTime", wp.waitTime}
            });
    }

    paramsJson["useSpline"] = genParams_.useSpline;
    paramsJson["generateRelative"] = genParams_.generateRelative;
    paramsJson["duration"] = genParams_.duration;
    paramsJson["easingType"] = genParams_.easingType;

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
                genParams_.waypoints.push_back(wp);
            }
        }
        if (pj.contains("useSpline")) genParams_.useSpline = pj["useSpline"];


        if (pj.contains("generateRelative")) {
            genParams_.generateRelative = pj["generateRelative"];
            isRelative_ = genParams_.generateRelative;
        }

        if (pj.contains("duration")) genParams_.duration = pj["duration"];
        if (pj.contains("easingType")) genParams_.easingType = pj["easingType"];
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