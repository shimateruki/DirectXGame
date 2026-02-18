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

using json = nlohmann::json;

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

                target_->SetTranslate(frame.position);
                target_->SetRotation(frame.rotation);
            } else {
                target_->SetTranslate(frame.position);
                target_->SetRotation(frame.rotation);
            }

            if (isOverrideCamera_) {
                CameraEditor* camEditor = CameraEditor::GetInstance();
                if (camEditor) {
                    // ターゲットの位置・回転に合わせてカメラを配置
                    // (ターゲットの回転を考慮して、ターゲットが見ている方向を向く)
                    Vector3 camPos = target_->GetTranslate();
                    Vector3 camRot = target_->GetRotation();

                    // CameraEditorに座標を流し込む
                    camEditor->SetEditorCameraTransform(camPos, camRot);
                }
            }
            currentFrameIndex_++;
        } else {
            if (isLoop_) currentFrameIndex_ = 0;
            else state_ = State::Idle;
            Stop();
        }
    }
}

void GhostRecorder::Play(const std::string& fileName, bool loop, bool isRelative, bool isCinematic) {
    Load(fileName);
    if (frames_.empty()) return;

    isLoop_ = loop;
    isRelative_ = isRelative;
    isOverrideCamera_ = isCinematic; // 演出の時だけカメラ乗っ取り

    // ターゲットの表示設定
    if (target_) {
        if (isCinematic) {
            // カメラ演出なら、再生中は隠す (エディタで見ている時用)
            target_->SetIsVisible(false);
        } else {
            // 動く床なら隠さない
            target_->SetIsVisible(true);
        }
    }

    StartPlayingInternal();
}

void GhostRecorder::Stop() {
    state_ = State::Idle;
    currentFrameIndex_ = 0;

    if (isOverrideCamera_) {
        CameraEditor* camEditor = CameraEditor::GetInstance();
        if (camEditor) {
            // とりあえず Game モード (通常プレイ) に戻す
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
    if (!target_ || frames_.empty()) return;
    state_ = State::Playing;
    currentFrameIndex_ = 0;

    if (isOverrideCamera_) {

        CameraEditor* camEditor = CameraEditor::GetInstance();
        if (camEditor) {
            // 現在のモードを保存しておく変数がGhostRecorderにあればベスト
     /*        previousCameraMode_ = camEditor->GetMode(); */

            camEditor->SetMode(CameraEditor::Mode::Editor);
        }
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
void GhostRecorder::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size) {
#ifdef USE_IMGUI
    if (!isShowPreview_ || !target_) return;

    // ★重要: Backgroundではなく ForegroundDrawList を使うことでウィンドウの上に描画される
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    float time = (float)ImGui::GetTime();

    // ★座標変換：World -> Clip -> Screen (GameViewウィンドウ内)
    auto WorldToScreen = [&](const Vector3& worldPos) -> ImVec2 {
        Vector3 clip = TransformCoord(worldPos, viewProjection);

        // Z値によるカリング（カメラの後ろなら描画しない）
        if (clip.z < 0.0f || clip.z > 1.0f) return ImVec2(-10000.0f, -10000.0f);

        // クリップ空間(-1.0～1.0)を、ウィンドウ内のピクセル座標(offset～offset+size)に変換
        float screenX = offset.x + (clip.x + 1.0f) * 0.5f * size.x;
        float screenY = offset.y + (1.0f - clip.y) * 0.5f * size.y;
        return ImVec2(screenX, screenY);
        };

    // --- 計算用の全ポイント整理 ---
    std::vector<Vector3> allPos;
    std::vector<Vector3> allRot;
    allPos.push_back(genParams_.startPos);
    allRot.push_back(genParams_.startRot);
    for (const auto& wp : genParams_.waypoints) {
        allPos.push_back(wp.pos);
        allRot.push_back(wp.rot);
    }
    allPos.push_back(genParams_.endPos);
    allRot.push_back(genParams_.endRot);

    if (allPos.size() < 2) return;

    // --- 1. 軌跡のメインライン（白い点線） ---
    const int samples = 60;
    ImVec2 prevScreenPos = WorldToScreen(allPos[0]);
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

        ImVec2 screenPos = WorldToScreen(currentPos);
        // 両方の点が有効な座標（-5000より大きい）場合のみ線を引く
        if (screenPos.x > -5000.0f && prevScreenPos.x > -5000.0f) {
            drawList->AddLine(prevScreenPos, screenPos, IM_COL32(255, 255, 255, 100), 1.5f);
        }
        prevScreenPos = screenPos;
    }

    // --- 2. 視線ベクトルと進行方向の可視化 ---
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

        ImVec2 baseScr = WorldToScreen(pos);
        if (baseScr.x < -5000.0f) continue;

        // 視線ベクトル計算
        float cosX = cosf(rot.x);
        Vector3 forward = { cosX * sinf(rot.y), -sinf(rot.x), cosX * cosf(rot.y) };
        Vector3 lookTarget = { pos.x + forward.x * 3.0f, pos.y + forward.y * 3.0f, pos.z + forward.z * 3.0f };
        ImVec2 lookScr = WorldToScreen(lookTarget);

        if (lookScr.x > -5000.0f) {
            drawList->AddLine(baseScr, lookScr, IM_COL32(0, 255, 255, 200), 2.0f);
            drawList->AddCircleFilled(lookScr, 2.0f, IM_COL32(0, 255, 255, 255));
        }

        // 進行方向の矢印
        if (i < arrowSteps) {
            float nextT = t + 0.02f;
            Vector3 nPos = genParams_.useSpline ? GetSplinePoint(allPos, nextT, false) : Lerp(allPos[idx], allPos[idx + 1], lt + 0.02f);
            ImVec2 nScr = WorldToScreen(nPos);
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

    // --- 3. アニメーションドット（流れる光） ---
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
        ImVec2 pScr = WorldToScreen(pPos);
        if (pScr.x > -5000.0f) drawList->AddCircleFilled(pScr, 3.0f, IM_COL32(255, 255, 0, 200));
    }

    // --- 4. 重要ポイントのラベル ---
    ImVec2 startScr = WorldToScreen(genParams_.startPos);
    if (startScr.x > -5000.0f) {
        drawList->AddCircleFilled(startScr, 6.0f, IM_COL32(0, 255, 0, 255));
        drawList->AddText(ImVec2(startScr.x + 10, startScr.y - 15), IM_COL32(0, 255, 0, 255), "[START]");
    }
    ImVec2 endScr = WorldToScreen(genParams_.endPos);
    if (endScr.x > -5000.0f) {
        drawList->AddCircleFilled(endScr, 6.0f, IM_COL32(100, 100, 255, 255));
        drawList->AddText(ImVec2(endScr.x + 10, endScr.y - 15), IM_COL32(100, 100, 255, 255), "[END]");
    }
    for (int i = 0; i < (int)genParams_.waypoints.size(); ++i) {
        ImVec2 wpScr = WorldToScreen(genParams_.waypoints[i].pos);
        if (wpScr.x > -5000.0f) {
            drawList->AddCircleFilled(wpScr, 4.0f, IM_COL32(255, 255, 0, 255));
            char buf[16]; snprintf(buf, sizeof(buf), "P%d", i + 1);
            drawList->AddText(ImVec2(wpScr.x + 8, wpScr.y - 12), IM_COL32(255, 255, 0, 255), buf);
        }
    }
#endif
}
// ==========================================================================
// DrawImGui (UI部分)
// ==========================================================================

void GhostRecorder::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Begin("Ghost Recorder");

    // ターゲット選択
    if (sceneManager_) {
        BaseScene* scene = sceneManager_->GetCurrentScene();
        if (scene) {
            std::string currentTargetName = target_ ? target_->GetName() : "(未選択)";
            if (ImGui::BeginCombo("ターゲット", currentTargetName.c_str())) {
                auto& objects = scene->GetObjects();
                for (auto& obj : objects) {
                    bool isSelected = (target_ == obj.get());
                    if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) target_ = obj.get();
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
    }

    const char* stateStr = "待機中";
    if (state_ == State::Recording) stateStr = "録画中";
    if (state_ == State::Playing) stateStr = "再生中";
    ImGui::Text("状態: %s | フレーム: %d", stateStr, (int)frames_.size());

    ImGui::Separator();

    // 手動録画
    if (ImGui::CollapsingHeader("手動録画", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state_ == State::Idle) {
            if (!target_) ImGui::BeginDisabled();
            if (ImGui::Button("● 録画開始")) StartRecording();
            if (!target_) ImGui::EndDisabled();
        } else if (state_ == State::Recording) {
            if (ImGui::Button("■ 録画停止")) StopRecording();
        }
    }

    // 自動生成 (パスエディタ)
    if (ImGui::CollapsingHeader("パス生成 (Path Editor)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (target_) {
            ImGui::Spacing();
            // ★ 可視化チェックボックス
            ImGui::Checkbox("プレビュー線を表示 (Show Path)", &isShowPreview_);

            // =========================================================
            // Start 設定
            // =========================================================
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[ START ]");
            ImGui::SameLine();
            if (ImGui::Button("現在地をセット##Start")) {
                genParams_.startPos = target_->GetTranslate();
                genParams_.startRot = target_->GetRotation();
            }
            ImGui::DragFloat3("Pos##Start", &genParams_.startPos.x, 0.1f);
            ImGui::DragFloat3("Rot##Start", &genParams_.startRot.x, 1.0f);


            // =========================================================
            // Waypoints 設定
            // =========================================================
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "[ WAYPOINTS ]");

            for (int i = 0; i < genParams_.waypoints.size(); ++i) {
                ImGui::PushID(i);
                ImGui::Text("P%d", i + 1);
                ImGui::SameLine();

                // 構造体のメンバ (.pos, .rot) を操作
                ImGui::DragFloat3("Pos", &genParams_.waypoints[i].pos.x, 0.1f);
                ImGui::DragFloat3("Rot", &genParams_.waypoints[i].rot.x, 1.0f);

                ImGui::SameLine();
                if (ImGui::Button("Del")) {
                    genParams_.waypoints.erase(genParams_.waypoints.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            // 新規追加時は構造体を作成してpushする
            if (ImGui::Button("+ 現在地を追加")) {
                GenerationParams::Waypoint wp;
                wp.pos = target_->GetTranslate();
                wp.rot = target_->GetRotation();
                genParams_.waypoints.push_back(wp);
            }
            ImGui::SameLine();
            if (ImGui::Button("クリア")) genParams_.waypoints.clear();


            // =========================================================
            // End 設定
            // =========================================================
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "[ END ]");
            ImGui::SameLine();
            if (ImGui::Button("現在地をセット##End")) {
                genParams_.endPos = target_->GetTranslate();
                genParams_.endRot = target_->GetRotation();
            }

            // ループ設定ボタン
            ImGui::SameLine();
            if (ImGui::Button("Startと同じにする(Loop)")) {
                genParams_.endPos = genParams_.startPos;
                genParams_.endRot = genParams_.startRot;
            }
            ImGui::DragFloat3("Pos##End", &genParams_.endPos.x, 0.1f);
            ImGui::DragFloat3("Rot##End", &genParams_.endRot.x, 1.0f);


            ImGui::Separator();
            ImGui::Checkbox("スプライン曲線", &genParams_.useSpline);
            ImGui::SameLine();
            ImGui::Checkbox("相対データ化", &genParams_.generateRelative);
            ImGui::DragFloat("時間 (sec)", &genParams_.duration, 0.1f, 0.1f, 120.0f);
            ImGui::Checkbox("イージング", &genParams_.useEasing);

            // =========================================================
            // 生成実行ロジック (ここが修正のキモです)
            // =========================================================
            if (ImGui::Button("★ 生成実行 (Generate)", ImVec2(-1, 40))) {
                frames_.clear();
                int totalFrames = static_cast<int>(genParams_.duration * 60.0f);
                if (totalFrames < 1) totalFrames = 1;

                // 1. 位置計算用のリスト作成
                std::vector<Vector3> posPoints;
                posPoints.push_back(genParams_.startPos);
                // 構造体から pos だけを取り出す
                for (auto& wp : genParams_.waypoints) {
                    posPoints.push_back(wp.pos);
                }
                posPoints.push_back(genParams_.endPos);


                // 2. 回転計算用のリスト作成
                std::vector<Vector3> rotPoints;
                rotPoints.push_back(genParams_.startRot);
                // 構造体から rot だけを取り出す
                for (auto& wp : genParams_.waypoints) {
                    rotPoints.push_back(wp.rot);
                }
                rotPoints.push_back(genParams_.endRot);


                Vector3 offset = genParams_.generateRelative ? genParams_.startPos : Vector3{ 0,0,0 };

                for (int i = 0; i <= totalFrames; ++i) {
                    float t = (float)i / (float)totalFrames;
                    if (genParams_.useEasing) t = SmoothStep(t);

                    // --- A. 位置の計算 ---
                    Vector3 pos;
                    if (genParams_.useSpline) {
                        pos = GetSplinePoint(posPoints, t, false);
                    } else {
                        // 線形補間 (Position)
                        float p = t * (posPoints.size() - 1);
                        int idx = (int)p;
                        float lt = p - idx;
                        if (idx >= posPoints.size() - 1) { idx = (int)posPoints.size() - 2; lt = 1.0f; }

                        pos = Lerp(posPoints[idx], posPoints[idx + 1], lt);
                    }

                    // --- B. 回転の計算 ---
                    // 位置と同じロジックで「今の区間」を探して補間する
                    Vector3 rot;
                    float rp = t * (rotPoints.size() - 1);
                    int rIdx = (int)rp;
                    float rLt = rp - rIdx;

                    if (rIdx >= rotPoints.size() - 1) {
                        rIdx = (int)rotPoints.size() - 2;
                        rLt = 1.0f;
                    }

                    rot = Lerp(rotPoints[rIdx], rotPoints[rIdx + 1], rLt);


                    // フレーム作成・登録
                    GhostFrame f;
                    f.position = { pos.x - offset.x, pos.y - offset.y, pos.z - offset.z };
                    f.rotation = rot;

                    frames_.push_back(f);
                }

                isRelative_ = genParams_.generateRelative;
                Stop();
                ImGui::OpenPopup("Done");
            }
            if (ImGui::BeginPopup("Done")) { ImGui::Text("完了!"); ImGui::EndPopup(); }

        } else {
            ImGui::TextDisabled("ターゲットを選択してください");
        }
    }
    ImGui::Separator();
    ImGui::Checkbox("再生時にカメラを乗っ取る (Cinema Mode)", &isOverrideCamera_);
    ImGui::Separator();
    // ファイルIO
    static char fName[64] = "anim_path";
    ImGui::InputText("ファイル名", fName, 64);
    if (ImGui::Button("Save")) Save(fName);
    ImGui::SameLine();
    if (ImGui::Button("Load")) Load(fName);

    ImGui::Separator();
    ImGui::Checkbox("Loop再生", &isLoop_);
    ImGui::SameLine();
    ImGui::Checkbox("相対再生", &isRelative_);
    if (state_ != State::Recording) {
        if (!target_) ImGui::BeginDisabled();
        if (ImGui::Button("再生")) StartPlayingInternal();
        if (!target_) ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("停止")) Stop();

    ImGui::End();
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
        root["frames"].push_back(frameJson);
    }
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
    if (root.contains("frames")) {
        for (const auto& j : root["frames"]) {
            GhostFrame f;
            f.position = { j["pos"][0], j["pos"][1], j["pos"][2] };
            f.rotation = { j["rot"][0], j["rot"][1], j["rot"][2] };
            frames_.push_back(f);
        }
    }
}