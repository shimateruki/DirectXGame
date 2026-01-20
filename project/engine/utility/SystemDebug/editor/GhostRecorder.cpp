#include "GhostRecorder.h"
#include "imgui.h" 
#include "SceneManager.h" 
#include "BaseScene.h"
#include <fstream>
#include "json.hpp"
#include <cmath> 
#include <algorithm> 

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
            currentFrameIndex_++;
        } else {
            if (isLoop_) currentFrameIndex_ = 0;
            else state_ = State::Idle;
        }
    }
}

void GhostRecorder::Play(const std::string& fileName, bool loop, bool isRelative) {
    Load(fileName);
    if (frames_.empty()) return;
    isLoop_ = loop;
    isRelative_ = isRelative;
    StartPlayingInternal();
}

void GhostRecorder::Stop() {
    state_ = State::Idle;
    currentFrameIndex_ = 0;
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
void GhostRecorder::DrawPreview(const Matrix4x4& viewProjection) {
    if (!isShowPreview_ || !target_) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;

    // ワールド座標 -> 画面座標への変換ラムダ
    auto WorldToScreen = [&](const Vector3& worldPos) -> ImVec2 {
        Vector3 clip = TransformCoord(worldPos, viewProjection);
        // カメラの後ろ(z<0)や遠すぎる(z>1)場合は描画しない
        if (clip.z < 0.0f || clip.z > 1.0f) return ImVec2(-10000, -10000);

        // NDC(-1~1) -> Screen(0~w, 0~h)
        float screenX = (clip.x + 1.0f) * 0.5f * screenSize.x;
        float screenY = (1.0f - clip.y) * 0.5f * screenSize.y;
        return ImVec2(screenX, screenY);
        };

    // ポイントリストの作成
    std::vector<Vector3> allPoints;
    allPoints.push_back(genParams_.startPos);
    for (const auto& p : genParams_.waypoints) allPoints.push_back(p);
    allPoints.push_back(genParams_.endPos);

    if (allPoints.empty()) return;

    // 1. 軌跡のライン描画
    if (allPoints.size() >= 2) {
        const int samples = 50; // 分割数
        ImVec2 prevScreenPos = WorldToScreen(allPoints[0]);

        for (int i = 1; i <= samples; ++i) {
            float t = (float)i / (float)samples;
            Vector3 currentPos;

            if (genParams_.useSpline) {
                currentPos = GetSplinePoint(allPoints, t, false);
            } else {
                float p = t * (allPoints.size() - 1);
                int idx = (int)p;
                float localT = p - idx;
                if (idx >= allPoints.size() - 1) { idx = (int)allPoints.size() - 2; localT = 1.0f; }
                currentPos = Lerp(allPoints[idx], allPoints[idx + 1], localT);
            }

            ImVec2 screenPos = WorldToScreen(currentPos);
            if (screenPos.x > -5000 && prevScreenPos.x > -5000) {
                // 白い線
                drawList->AddLine(prevScreenPos, screenPos, IM_COL32(255, 255, 255, 200), 2.0f);
            }
            prevScreenPos = screenPos;
        }
    }

    // 2. 各ポイントの描画
    // START (緑)
    ImVec2 startScr = WorldToScreen(genParams_.startPos);
    if (startScr.x > -5000) {
        drawList->AddCircleFilled(startScr, 6.0f, IM_COL32(0, 255, 0, 255));
        drawList->AddText(ImVec2(startScr.x + 10, startScr.y), IM_COL32(0, 255, 0, 255), "START");
    }
    // END (青)
    ImVec2 endScr = WorldToScreen(genParams_.endPos);
    if (endScr.x > -5000) {
        drawList->AddCircleFilled(endScr, 6.0f, IM_COL32(100, 100, 255, 255));
        drawList->AddText(ImVec2(endScr.x + 10, endScr.y), IM_COL32(100, 100, 255, 255), "END");
    }
    // WAYPOINTS (黄)
    for (int i = 0; i < genParams_.waypoints.size(); ++i) {
        ImVec2 wpScr = WorldToScreen(genParams_.waypoints[i]);
        if (wpScr.x > -5000) {
            drawList->AddCircleFilled(wpScr, 4.0f, IM_COL32(255, 255, 0, 255));
            char buf[16]; snprintf(buf, sizeof(buf), "%d", i + 1);
            drawList->AddText(ImVec2(wpScr.x + 8, wpScr.y), IM_COL32(255, 255, 0, 255), buf);
        }
    }
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

            // Start
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[ START ]");
            ImGui::SameLine();
            if (ImGui::Button("現在地をセット##Start")) {
                genParams_.startPos = target_->GetTranslate();
                genParams_.startRot = target_->GetRotation();
            }
            ImGui::DragFloat3("##S_Pos", &genParams_.startPos.x, 0.1f);

            // Waypoints
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "[ WAYPOINTS ]");
            for (int i = 0; i < genParams_.waypoints.size(); ++i) {
                ImGui::PushID(i);
                ImGui::Text("P%d", i + 1); ImGui::SameLine();
                ImGui::DragFloat3("##W_Pos", &genParams_.waypoints[i].x, 0.1f);
                ImGui::SameLine();
                if (ImGui::Button("Del")) {
                    genParams_.waypoints.erase(genParams_.waypoints.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ 現在地を追加")) genParams_.waypoints.push_back(target_->GetTranslate());
            ImGui::SameLine();
            if (ImGui::Button("クリア")) genParams_.waypoints.clear();

            // End
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "[ END ]");
            ImGui::SameLine();
            if (ImGui::Button("現在地をセット##End")) {
                genParams_.endPos = target_->GetTranslate();
                genParams_.endRot = target_->GetRotation();
            }

            // ★ ループ用ボタン
            ImGui::SameLine();
            if (ImGui::Button("Startと同じにする(Loop)")) {
                genParams_.endPos = genParams_.startPos;
                genParams_.endRot = genParams_.startRot;
            }
            ImGui::DragFloat3("##E_Pos", &genParams_.endPos.x, 0.1f);

            ImGui::Separator();
            ImGui::Checkbox("スプライン曲線", &genParams_.useSpline);
            ImGui::SameLine();
            ImGui::Checkbox("相対データ化", &genParams_.generateRelative);
            ImGui::DragFloat("時間 (sec)", &genParams_.duration, 0.1f, 0.1f, 120.0f);
            ImGui::Checkbox("イージング", &genParams_.useEasing);

            if (ImGui::Button("★ 生成実行 (Generate)", ImVec2(-1, 40))) {
                // 生成ロジック 
                frames_.clear();
                int totalFrames = static_cast<int>(genParams_.duration * 60.0f);
                if (totalFrames < 1) totalFrames = 1;

                std::vector<Vector3> pts;
                pts.push_back(genParams_.startPos);
                for (auto& p : genParams_.waypoints) pts.push_back(p);
                pts.push_back(genParams_.endPos);

                Vector3 offset = genParams_.generateRelative ? genParams_.startPos : Vector3{ 0,0,0 };

                for (int i = 0; i <= totalFrames; ++i) {
                    float t = (float)i / (float)totalFrames;
                    if (genParams_.useEasing) t = SmoothStep(t);

                    Vector3 pos;
                    if (genParams_.useSpline) pos = GetSplinePoint(pts, t, false);
                    else {
                        // 線形補間
                        float p = t * (pts.size() - 1);
                        int idx = (int)p;
                        float lt = p - idx;
                        if (idx >= pts.size() - 1) { idx = (int)pts.size() - 2; lt = 1.0f; }
                        pos = Lerp(pts[idx], pts[idx + 1], lt);
                    }

                    GhostFrame f;
                    f.position = { pos.x - offset.x, pos.y - offset.y, pos.z - offset.z };
                    f.rotation = Lerp(genParams_.startRot, genParams_.endRot, t);
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

// Save/Loadは前回のまま変更なし
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