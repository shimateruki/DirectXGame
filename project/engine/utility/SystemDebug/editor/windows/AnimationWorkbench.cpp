#include "AnimationWorkbench.h"
#include "EffectPreviewStage.h"
#include "EditorToolSettings.h"

#include "BaseScene.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#ifdef USE_IMGUI
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "ImGuizmo.h"
#endif
#include "ModelManager.h"
#include "MeshRenderer.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>

namespace {
constexpr const char* kPreviewObjectName = "__Editor_AnimationWorkbenchPreview";
constexpr const char* kSaveDirectory = "Resources/json/animation_clip/";
constexpr const char* kLegacySaveDirectory = "Resources/json/enemy_animation/";
constexpr float kPi = 3.14159265358979323846f;
constexpr float kKeyEpsilon = 0.001f;

const char* kEasingNames[] = {
    "Linear",
    "Ease In",
    "Ease Out",
    "Ease In Out",
    "Smoother Step"
};

const char* kEventTypeNames[] = {
    "Attack On",
    "Attack Off",
    "Move Start",
    "Move End",
    "Effect",
    "SE"
};
constexpr int kEventTypeCount = static_cast<int>(sizeof(kEventTypeNames) / sizeof(kEventTypeNames[0]));

void CopyToBuffer(char* buffer, size_t size, const std::string& text) {
    if (!buffer || size == 0) return;
    strncpy_s(buffer, size, text.c_str(), _TRUNCATE);
}
}

void AnimationWorkbench::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
    // プレビューのシーンと描画基盤は借用し、ワークベンチ側で所有しない。
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
    RefreshAssetFiles();
}

void AnimationWorkbench::Finalize() {
    // シーンへ追加したプレビュー専用 Object を片付けて参照を無効にする。
    StopPreview();
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 1, false);
    RemovePreviewObject();
    previewModel_ = nullptr;
    ClearAllEditOverrides();
}

void AnimationWorkbench::StartPreview() {
    if (enabled_ || !sceneManager_ || sceneManager_->IsPlaying()) return;
    enabled_ = true;
    // カメラ上書きより先に元の視点を保存する。
    Update(0.0f);
}

void AnimationWorkbench::Update(float deltaTime) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 1, enabled_);

    if (enabled_ && !wasEnabled_) {
        previewScene_ = sceneManager_->GetCurrentScene();
        if (auto* camera = CameraManager::GetInstance()->GetMainCamera()) {
            storedEye_ = camera->GetEye();
            storedTarget_ = camera->GetTargetPoint();
            storedRotation_ = camera->GetRotation();
            storedFollow_ = camera->GetFollowTarget();
            storedCameraMode_ = static_cast<int>(CameraEditor::GetInstance()->GetMode());
            storedCameraValid_ = true;
        }
        hasPlacedCamera_ = false;
        auto* studio = EffectPreviewStage::GetInstance();
        studio->EnableForToolPreview();
        // 初回の床・照明・カメラ状態をここで確定し、次フレームの再配置を防ぐ。
        studio->Update();
        ownsStudio_ = true;
        previewOrigin_ = studio->GetGroundPosition();
        if (!previewObject_) {
            CreatePreviewObject();
        }
    }
    if (!enabled_ && wasEnabled_) {
        StopPreview();
    }
    if (enabled_ && (sceneManager_->IsPlaying() || previewScene_ != sceneManager_->GetCurrentScene())) StopPreview();
    wasEnabled_ = enabled_;

    if (!enabled_) return;

    previewObject_ = FindPreviewObject();
    if (previewObject_) {
        previewObject_->SetTranslate(previewOrigin_);
        previewObject_->SetScale(previewScale_);
        previewObject_->UpdateLocalMatrix();
        previewObject_->UpdateWorldMatrix();
    }

    float previousTime = currentTime_;
    bool timelineAdvanced = false;
    if (play_ && duration_ > 0.0f) {
        currentTime_ += deltaTime * playbackSpeed_;
        timelineAdvanced = true;
        if (loop_) {
            currentTime_ = std::fmod(currentTime_, duration_);
            if (currentTime_ < 0.0f) currentTime_ += duration_;
        } else {
            if (currentTime_ > duration_) {
                currentTime_ = duration_;
                play_ = false;
            } else if (currentTime_ < 0.0f) {
                currentTime_ = 0.0f;
                play_ = false;
            }
        }
    }

    if (timelineAdvanced && previewEvents_) {
        UpdateEventPreview(previousTime, currentTime_);
    }
    if (eventPreviewTimer_ > 0.0f) {
        eventPreviewTimer_ = (std::max)(0.0f, eventPreviewTimer_ - deltaTime);
    }

    if (autoApply_) {
        ApplyTimelinePose();
    }
}

void AnimationWorkbench::StopPreview() {
    if (ownsStudio_) {
        EffectPreviewStage::GetInstance()->ReturnToScene();
        ownsStudio_ = false;
    }
    timelineDragKind_ = timelineDragIndex_ = -1;
    const bool sameScene = sceneManager_ && sceneManager_->GetCurrentScene() == previewScene_;
    if (sameScene) {
        RemovePreviewObject();
        if (storedCameraValid_) {
            if (auto* camera = CameraManager::GetInstance()->GetMainCamera()) {
                Object3d* follow = nullptr;
                for (const auto& object : previewScene_->GetObjects())
                    if (object.get() == storedFollow_) follow = object.get();
                camera->SetFollowTarget(follow);
                camera->SetEye(storedEye_);
                camera->SetTarget(storedTarget_);
                camera->SetRotation(storedRotation_);
                CameraEditor::GetInstance()->SetMode(static_cast<CameraEditor::Mode>(storedCameraMode_));
                camera->Update();
            }
        }
    }
    // クリップの編集内容は残し、仮Objectとカメラの占有だけを解除する。
    enabled_ = wasEnabled_ = play_ = hasPlacedCamera_ = storedCameraValid_ = false;
    previewObject_ = nullptr;
    previewScene_ = nullptr;
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 1, false);
}

void AnimationWorkbench::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_RUNNING " アニメーション制作");
    ImGui::TextWrapped("1. モデル・クリップを選ぶ → 2. 下で時刻を選ぶ → 3. 姿勢を編集してキー登録");
    ImGui::Separator();

    ImGui::TextDisabled("制作専用グリッド空間 / 実ステージには保存されません");
    ImGui::Checkbox("自動反映", &autoApply_);

    ImGui::SeparatorText("編集対象");
    if (ImGui::RadioButton("身体全体の変形", editBodyTransform_)) {
        editBodyTransform_ = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("ボーンの姿勢", !editBodyTransform_)) {
        editBodyTransform_ = false;
    }
    ImGui::TextWrapped("身体全体：伸縮・傾き / ボーン：関節の姿勢。再生・時刻は画面下で操作します。");

    DrawPreviewControls();
    DrawAssetControls();
    // クリップ切り替えで編集中のキーを無言で捨てない。
    if (pendingClipAction_ != 0) ImGui::OpenPopup("クリップの切り替え確認");
    if (ImGui::BeginPopupModal("クリップの切り替え確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("現在の編集内容を置き換えます。必要な場合は先に保存してください。");
        if (ImGui::Button("置き換える")) {
            play_ = false;
            if (pendingClipAction_ == 1) NewAuthoringClip();
            else {
                CopyToBuffer(saveFileBuffer_, sizeof(saveFileBuffer_), pendingClipPath_);
                LoadAuthoringJson();
            }
            pendingClipAction_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) { pendingClipAction_ = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (editBodyTransform_) {
        DrawBodyControls();
    } else {
        DrawJointControls();
        DrawKeyframeControls();
    }
    if (ImGui::CollapsingHeader("イベント（上級）")) DrawEventControls();
    if (editBodyTransform_) {
        DrawBodyGizmo();
    } else {
        DrawBoneOverlayAndGizmo();
    }
#endif
}


void AnimationWorkbench::DrawPreviewControls() {
    // 再生位置・速度・ループを編集し、保存済みキーと現在の見た目を同期する。
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("プレビュー", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputText("モデル名", modelNameBuffer_, sizeof(modelNameBuffer_));

    std::vector<std::string> loadedModels = ModelManager::GetInstance()->GetAvailableModelNames();
    if (ImGui::BeginCombo("ロード済みモデル", modelNameBuffer_[0] ? modelNameBuffer_ : "選択なし")) {
        for (const std::string& name : loadedModels) {
            bool selected = (name == modelNameBuffer_);
            if (ImGui::Selectable(name.c_str(), selected)) {
                CopyToBuffer(modelNameBuffer_, sizeof(modelNameBuffer_), name);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button(ICON_FA_FOLDER_OPEN " モデルを読み込み")) {
        LoadPreviewModel(modelNameBuffer_);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH_ALT " プレビュー削除")) {
        RemovePreviewObject();
        previewModel_ = nullptr;
        ClearAllEditOverrides();
        lastEventPreviewText_.clear();
        eventPreviewTimer_ = 0.0f;
    }

    ImGui::DragFloat3("表示スケール", &previewScale_.x, 0.01f, 0.01f, 100.0f);
    ImGui::DragFloat("カメラ距離", &cameraDistance_, 0.1f, 1.0f, 80.0f);
    ImGui::DragFloat("カメラ高さ", &cameraHeight_, 0.1f, -20.0f, 40.0f);
    if (ImGui::Button(ICON_FA_CROSSHAIRS " カメラをプレビューへ")) {
        recenterCameraRequested_ = true;
    }

    if (previewModel_) {
        const auto& data = previewModel_->GetModelData();
        ImGui::Text("メッシュ: %u / ボーン: %d / アニメーション: %d",
            previewModel_->GetMeshCount(),
            static_cast<int>(data.skeleton.joints.size()),
            static_cast<int>(data.animations.size()));

        if (ImGui::BeginCombo("ベースアニメーション", animationNameBuffer_[0] ? animationNameBuffer_ : "なし")) {
            if (ImGui::Selectable("なし", animationNameBuffer_[0] == '\0')) {
                animationNameBuffer_[0] = '\0';
                useBaseAnimation_ = false;
            }
            for (const auto& anim : data.animations) {
                bool selected = (anim.name == animationNameBuffer_);
                if (ImGui::Selectable(anim.name.c_str(), selected)) {
                    CopyToBuffer(animationNameBuffer_, sizeof(animationNameBuffer_), anim.name);
                    duration_ = (std::max)(duration_, anim.duration);
                    useBaseAnimation_ = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("ベースアニメーションを重ねる", &useBaseAnimation_);
    }
#endif
}


void AnimationWorkbench::DrawAssetControls() {
    // モデル、基礎アニメーション、作業 JSON の選択と保存をここに集める。
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("クリップの読み込み・保存", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const char* currentFile = saveFileBuffer_[0] ? saveFileBuffer_ : "(新規)";
    if (ImGui::BeginCombo("既存Clip", currentFile)) {
        for (const std::string& fileName : assetFiles_) {
            const bool selected = fileName == std::filesystem::path(saveFileBuffer_).stem().string();
            if (ImGui::Selectable(fileName.c_str(), selected)) {
                pendingClipPath_ = fileName + ".json";
                pendingClipAction_ = 2;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::InputText("保存ファイル", saveFileBuffer_, sizeof(saveFileBuffer_));
    if (ImGui::Button(ICON_FA_FILE " 新規")) {
        pendingClipAction_ = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SAVE " 保存")) {
        SaveAuthoringJson();
        RefreshAssetFiles();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " 再読込")) {
        pendingClipPath_ = saveFileBuffer_;
        pendingClipAction_ = 2;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " 一覧更新")) {
        RefreshAssetFiles();
    }

    ImGui::Text("Body %zu / Bone %zu / Event %zu", bodyKeys_.size(), keys_.size(), events_.size());
    ImGui::TextDisabled("保存先: %s", GetSavePath().c_str());
#endif
}

void AnimationWorkbench::DrawBodyControls() {
    // 本体の移動・回転・伸縮・せん断を関節キーとは分けて編集する。
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("身体の姿勢", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextDisabled("Scaleは基準Scaleへの倍率、RotationとPositionは見た目用Offsetです。");
    ImGui::TextDisabled("Player Runtimeでは物理座標を守るためPosition Offsetを移動処理へ加算しません。");

    bool edited = false;
    edited |= ImGui::DragFloat3("位置のずれ", &bodyTranslateUi_.x, 0.01f);
    edited |= ImGui::DragFloat3("回転（度）", &bodyRotateDegUi_.x, 0.5f);
    edited |= ImGui::DragFloat3("伸縮倍率", &bodyScaleUi_.x, 0.01f, 0.01f, 8.0f);
    edited |= ImGui::DragFloat("上体の傾き X", &bodyShearUi_.x, 0.01f, -2.0f, 2.0f);
    edited |= ImGui::DragFloat("上体の傾き Z", &bodyShearUi_.z, 0.01f, -2.0f, 2.0f);
    edited |= ImGui::DragFloat("傾きの支点 Y", &bodyShearPivotY_, 0.005f);
    edited |= ImGui::Combo("次のキーへのEasing", &bodyEasingToNext_, kEasingNames, IM_ARRAYSIZE(kEasingNames));
    if (ImGui::RadioButton("移動##BodyGizmo", gizmoOperation_ == 0)) gizmoOperation_ = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("回転##BodyGizmo", gizmoOperation_ == 1)) gizmoOperation_ = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("拡縮##BodyGizmo", gizmoOperation_ == 2)) gizmoOperation_ = 2;
    ImGui::Checkbox("Body Gizmo", &enableBodyGizmo_);
    ImGui::SameLine();
    ImGui::Checkbox("Gizmo操作で自動キー", &autoKeyBodyOnGizmo_);

    if (edited) {
        bodyEditOverrideActive_ = true;
        ApplyTimelinePose();
    }

    if (bodyEditOverrideActive_) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "編集中: キー未登録のBody姿勢があります");
    }

    if (ImGui::Button(ICON_FA_KEY " 現在Body姿勢をキー登録")) {
        AddOrUpdateBodyKey();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " 編集姿勢を破棄")) {
        bodyEditOverrideActive_ = false;
        SyncBodyUiFromTimeline();
        ApplyTimelinePose();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedBodyKeyIndex_ < 0 || selectedBodyKeyIndex_ >= static_cast<int>(bodyKeys_.size()));
    if (ImGui::Button(ICON_FA_TRASH " 選択キー削除")) {
        DeleteSelectedBodyKey();
    }
    ImGui::EndDisabled();

    if (ImGui::BeginTable("AnimationWorkbenchBodyKeyTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 190.0f))) {
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Rotation");
        ImGui::TableSetupColumn("Scale");
        ImGui::TableSetupColumn("Easing", ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableHeadersRow();

        for (int index = 0; index < static_cast<int>(bodyKeys_.size()); ++index) {
            const BodyAnimationClip::Key& key = bodyKeys_[index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(20000 + index);
            char timeLabel[32]{};
            std::snprintf(timeLabel, sizeof(timeLabel), "%.3f", key.time);
            if (ImGui::Selectable(timeLabel, selectedBodyKeyIndex_ == index, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedBodyKeyIndex_ = index;
                currentTime_ = key.time;
                bodyEditOverrideActive_ = false;
                SyncBodyUiFromKey(key);
                ApplyTimelinePose();
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f %.2f %.2f", key.translate.x, key.translate.y, key.translate.z);
            ImGui::TableSetColumnIndex(2);
            const Vector3 rotateDeg = ToDegrees(key.rotate);
            ImGui::Text("%.1f %.1f %.1f", rotateDeg.x, rotateDeg.y, rotateDeg.z);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f %.2f %.2f", key.scale.x, key.scale.y, key.scale.z);
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(kEasingNames[std::clamp(key.easingToNext, 0, 4)]);
        }
        ImGui::EndTable();
    }
#endif
}

void AnimationWorkbench::DrawJointControls() {
    // 選択関節のローカル姿勢だけを編集し、全身の基礎アニメーションに重ねる。
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("ボーン編集", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!previewModel_ || !previewModel_->HasSkeleton()) {
        ImGui::TextDisabled("モデルを読み込むとボーン一覧が表示されます。");
        return;
    }

    ImGui::Checkbox("ボーン表示", &showBoneOverlay_);
    ImGui::SameLine();
    ImGui::Checkbox("名前表示", &showBoneNames_);
    ImGui::SameLine();
    ImGui::Checkbox("選択軸表示", &showBoneAxes_);
    ImGui::Checkbox("ボーンギズモ", &enableBoneGizmo_);
    ImGui::SameLine();
    ImGui::Checkbox("ギズモ操作で自動キー", &autoKeyOnGizmo_);
    ImGui::DragFloat("ボーン点サイズ", &bonePointRadius_, 0.1f, 2.0f, 16.0f);
    ImGui::DragFloat("ボーン線の太さ", &boneLineThickness_, 0.1f, 1.0f, 8.0f);

    if (ImGui::RadioButton("移動", gizmoOperation_ == 0)) gizmoOperation_ = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("回転", gizmoOperation_ == 1)) gizmoOperation_ = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("拡縮", gizmoOperation_ == 2)) gizmoOperation_ = 2;

    ImGui::InputText("検索", jointSearchBuffer_, sizeof(jointSearchBuffer_));

    const auto& joints = previewModel_->GetJoints();
    ImGui::BeginChild("AnimationWorkbenchJoints", ImVec2(0, 180), true);
    for (const auto& joint : joints) {
        if (jointSearchBuffer_[0] != '\0' && joint.name.find(jointSearchBuffer_) == std::string::npos) {
            continue;
        }
        int depth = 0;
        std::optional<int> parent = joint.parent;
        while (parent && *parent >= 0 && *parent < static_cast<int>(joints.size()) && depth < static_cast<int>(joints.size())) {
            ++depth;
            parent = joints[*parent].parent;
        }
        ImGui::Indent(static_cast<float>(depth) * 12.0f);
        const std::string label = joint.name + "##joint" + std::to_string(joint.index);
        bool selected = (selectedJointIndex_ == joint.index);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selectedJointIndex_ = joint.index;
            SyncUiFromJoint(selectedJointIndex_);
        }
        ImGui::Unindent(static_cast<float>(depth) * 12.0f);
    }
    ImGui::EndChild();

    if (selectedJointIndex_ < 0) return;

    bool edited = false;
    edited |= ImGui::DragFloat3("移動", &jointTranslateUi_.x, 0.01f);
    edited |= ImGui::DragFloat3("回転", &jointRotateDegUi_.x, 0.5f);
    edited |= ImGui::DragFloat3("拡縮", &jointScaleUi_.x, 0.01f, 0.01f, 100.0f);

    if (edited && previewModel_) {
        StoreEditOverrideFromUi();
        ApplyTimelinePose();
    }

    if (editPoseOverrides_.find(selectedJointIndex_) != editPoseOverrides_.end()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "編集中: キー未登録の姿勢があります");
    }

    if (ImGui::Button(ICON_FA_KEY " 現在ポーズをキー登録")) {
        AddOrUpdateKey();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " 編集ポーズを破棄")) {
        ClearEditOverride(selectedJointIndex_);
        ApplyTimelinePose();
        SyncUiFromJoint(selectedJointIndex_);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH " 全編集ポーズ破棄")) {
        ClearAllEditOverrides();
        ApplyTimelinePose();
        SyncUiFromJoint(selectedJointIndex_);
    }
#endif
}

void AnimationWorkbench::DrawKeyframeControls() {
    // タイムラインのキーを選択し、同時刻の値を更新できるようにする。
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("キー", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (ImGui::Button(ICON_FA_KEY " 選択ボーンにキー登録")) {
        AddOrUpdateKey();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH " 現在時間のキー削除")) {
        DeleteSelectedJointKeyAtCurrentTime();
    }

    if (ImGui::BeginTable("AnimationWorkbenchKeyTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Joint");
        ImGui::TableSetupColumn("Move");
        ImGui::TableSetupColumn("Rot");
        ImGui::TableSetupColumn("Scale");
        ImGui::TableHeadersRow();

        for (const PoseKey& key : keys_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%.3f", key.time);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(key.jointName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f %.2f %.2f", key.translate.x, key.translate.y, key.translate.z);
            ImGui::TableSetColumnIndex(3);
            Vector3 rotDeg = ToDegrees(key.rotate);
            ImGui::Text("%.1f %.1f %.1f", rotDeg.x, rotDeg.y, rotDeg.z);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f %.2f %.2f", key.scale.x, key.scale.y, key.scale.z);
        }
        ImGui::EndTable();
    }

#endif
}

void AnimationWorkbench::DrawEventControls() {
    // 音や演出の発火点は姿勢キーと別のイベント列で管理する。
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("イベントマーカー")) return;

    ImGui::Combo("種類", &eventType_, kEventTypeNames, IM_ARRAYSIZE(kEventTypeNames));
    ImGui::InputText("名前", eventNameBuffer_, sizeof(eventNameBuffer_));
    ImGui::DragFloat3("オフセット", &eventOffsetUi_.x, 0.01f);
    ImGui::Checkbox("再生時にイベントをプレビュー発火", &previewEvents_);

    if (!lastEventPreviewText_.empty()) {
        ImVec4 color = eventPreviewTimer_ > 0.0f ? ImVec4(1.0f, 0.78f, 0.2f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        ImGui::TextColored(color, "Last: %s", lastEventPreviewText_.c_str());
    }

    if (ImGui::Button(ICON_FA_PLUS " 現在時間に追加")) {
        EventMarker marker;
        marker.time = currentTime_;
        marker.type = eventType_;
        marker.name = eventNameBuffer_;
        marker.offset = eventOffsetUi_;
        events_.push_back(marker);
        std::sort(events_.begin(), events_.end(), [](const EventMarker& a, const EventMarker& b) { return a.time < b.time; });
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH " 選択削除") && selectedEventIndex_ >= 0 && selectedEventIndex_ < static_cast<int>(events_.size())) {
        events_.erase(events_.begin() + selectedEventIndex_);
        selectedEventIndex_ = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_BOLT " 選択イベントを発火") &&
        selectedEventIndex_ >= 0 && selectedEventIndex_ < static_cast<int>(events_.size())) {
        FireEventPreview(events_[selectedEventIndex_]);
    }

    if (ImGui::BeginTable("AnimationWorkbenchEventTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Offset");
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(events_.size()); ++i) {
            const EventMarker& marker = events_[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            std::string rowId = std::to_string(i) + "##event";
            if (ImGui::Selectable(rowId.c_str(), selectedEventIndex_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedEventIndex_ = i;
                currentTime_ = marker.time;
                SyncBodyUiFromTimeline();
                ApplyTimelinePose();
            }
            ImGui::SameLine();
            ImGui::Text("%.3f", marker.time);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(kEventTypeNames[(std::clamp)(marker.type, 0, static_cast<int>(IM_ARRAYSIZE(kEventTypeNames)) - 1)]);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(marker.name.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f %.2f %.2f", marker.offset.x, marker.offset.y, marker.offset.z);
        }
        ImGui::EndTable();
    }
#endif
}

bool AnimationWorkbench::LoadPreviewModel(const std::string& modelName) {
    // メッシュ実体のあるモデルだけ採用し、前モデルのキーや一時編集を残さない。
    if (modelName.empty()) return false;

    Model* model = ModelManager::GetInstance()->LoadModel(modelName);
    if (!model || model->GetMeshCount() == 0 || model->GetVertexCount() == 0 || model->GetPolygonCount() == 0) {
        DebugConsole::GetInstance()->AddLog(("Animation Workbench failed to load mesh data: " + modelName).c_str());
        return false;
    }

    previewModel_ = model;
    bodyKeys_.clear();
    keys_.clear();
    events_.clear();
    ClearAllEditOverrides();
    lastEventPreviewText_.clear();
    eventPreviewTimer_ = 0.0f;
    selectedJointIndex_ = -1;
    selectedBodyKeyIndex_ = -1;
    bodyTranslateUi_ = {};
    bodyShearUi_ = {};
    bodyShearPivotY_ = 0.0f;
    bodyRotateDegUi_ = {};
    bodyScaleUi_ = { 1.0f, 1.0f, 1.0f };
    bodyEasingToNext_ = 4;
    currentTime_ = 0.0f;

    const auto& animations = previewModel_->GetModelData().animations;
    if (!animations.empty()) {
        CopyToBuffer(animationNameBuffer_, sizeof(animationNameBuffer_), animations.front().name);
        duration_ = (std::max)(0.01f, animations.front().duration);
        useBaseAnimation_ = true;
    } else {
        animationNameBuffer_[0] = '\0';
        duration_ = 2.0f;
        useBaseAnimation_ = false;
    }

    if (enabled_) {
        CreatePreviewObject();
    }
    ApplyTimelinePose();
    DebugConsole::GetInstance()->AddLog(("Animation Workbench loaded: " + modelName).c_str());
    return true;
}

void AnimationWorkbench::CreatePreviewObject() {
    // 専用 Object をシーンに一つだけ置き、保存や当たり判定の対象から外す。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene->GetObject3dCommon()) return;

    Object3d* existing = FindPreviewObject();
    if (existing) {
        previewObject_ = existing;
    } else {
        auto object = std::make_unique<Object3d>();
        object->Initialize(scene->GetObject3dCommon());
        object->SetName(kPreviewObjectName);
        object->SetClassName("EditorOnly");
        object->SetEditorInternal(true);
        object->SetSaveCategory("Object");
        object->SetIsLocked(true);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
        object->SetTranslate(previewOrigin_);
        object->SetScale(previewScale_);
        object->animName_.clear();
        previewObject_ = object.get();
        scene->AddObject(std::move(object));
    }

    if (previewObject_ && previewModel_) {
        previewObject_->SetModel(previewModel_);
        previewObject_->SetIsVisible(true);
    }
}

void AnimationWorkbench::RemovePreviewObject() {
    // Scene の走査中に直接消さず、非表示にして削除要求へ渡す。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        previewObject_ = nullptr;
        return;
    }
    if (Object3d* object = FindPreviewObject()) {
        object->SetIsVisible(false);
        object->SetName("__Editor_AnimationWorkbenchRetired");
        sceneManager_->GetCurrentScene()->RequestRemoveObject(object);
    }
    previewObject_ = nullptr;
}

Object3d* AnimationWorkbench::FindPreviewObject() const {
    // シーン切り替え後は保持ポインタを信用せず、現シーン内で名前から探す。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;
    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == kPreviewObjectName) {
            return object.get();
        }
    }
    return nullptr;
}

void AnimationWorkbench::ApplyTimelinePose() {
    if (!previewModel_ || !previewObject_) return;

    // UI の一時調整を保存済みキーより優先し、未確定の姿勢をその場で確認できるようにする。
    BodyAnimationClip::Key bodyPose;
    const bool hasBodyPose = bodyEditOverrideActive_
        ? (bodyPose = BuildBodyKeyFromUi(), true)
        : TryGetInterpolatedBodyKey(currentTime_, bodyPose);
    if (hasBodyPose) {
        previewObject_->SetTranslate({
            previewOrigin_.x + bodyPose.translate.x,
            previewOrigin_.y + bodyPose.translate.y,
            previewOrigin_.z + bodyPose.translate.z
        });
        previewObject_->SetRotation(bodyPose.rotate);
        previewObject_->SetScale({
            previewScale_.x * bodyPose.scale.x,
            previewScale_.y * bodyPose.scale.y,
            previewScale_.z * bodyPose.scale.z
        });
    } else {
        previewObject_->SetTranslate(previewOrigin_);
        previewObject_->SetRotation({});
        previewObject_->SetScale(previewScale_);
    }
    previewObject_->UpdateLocalMatrix();
    previewObject_->UpdateWorldMatrix();

    if (auto* renderer = previewObject_->GetMeshRenderer()) {
        renderer->SetVisualShear(hasBodyPose ? bodyPose.shear : Vector3{}, hasBodyPose ? bodyPose.shearPivotY : 0.0f);
    }

    // 毎フレーム基準姿勢から組み立て直し、前フレームの関節回転を蓄積させない。
    previewModel_->ResetSkeletonPose();

    if (useBaseAnimation_ && animationNameBuffer_[0] != '\0') {
        if (const Model::Animation* anim = previewModel_->GetAnimation(animationNameBuffer_)) {
            float time = currentTime_;
            if (anim->duration > 0.0f) {
                time = (std::min)(time, anim->duration);
            }
            previewModel_->ApplyAnimation(*anim, time);
            previewModel_->RebuildSkeletonForEditor();
        }
    }

    std::map<int, PoseKey> poseByJoint;
    for (const PoseKey& key : keys_) {
        PoseKey interpolated;
        if (TryGetInterpolatedKey(key.jointIndex, currentTime_, interpolated)) {
            poseByJoint[key.jointIndex] = interpolated;
        }
    }

    for (const auto& [jointIndex, key] : poseByJoint) {
        ApplyPoseKey(key);
    }

    // 個別関節の一時編集は、ベースアニメと補間済みキーを適用した最後に重ねる。
    for (const auto& [jointIndex, key] : editPoseOverrides_) {
        (void)jointIndex;
        ApplyPoseKey(key);
    }
}

BodyAnimationClip::Key AnimationWorkbench::BuildBodyKeyFromUi() const {
    // UI の角度を保存形式のラジアンへ変換し、ゼロに近いスケールを制限する。
    BodyAnimationClip::Key key;
    key.time = currentTime_;
    key.translate = bodyTranslateUi_;
    key.shear = bodyShearUi_;
    key.shearPivotY = bodyShearPivotY_;
    key.rotate = ToRadians(bodyRotateDegUi_);
    key.scale = {
        (std::max)(0.01f, bodyScaleUi_.x),
        (std::max)(0.01f, bodyScaleUi_.y),
        (std::max)(0.01f, bodyScaleUi_.z)
    };
    key.easingToNext = std::clamp(bodyEasingToNext_, 0, 4);
    return key;
}

void AnimationWorkbench::AddOrUpdateBodyKey() {
    // 同時刻のキーを上書きし、新しい時刻だけ追加して並びを維持する。
    BodyAnimationClip::Key key = BuildBodyKeyFromUi();
    for (int index = 0; index < static_cast<int>(bodyKeys_.size()); ++index) {
        if (std::abs(bodyKeys_[index].time - key.time) <= kKeyEpsilon) {
            bodyKeys_[index] = key;
            selectedBodyKeyIndex_ = index;
            bodyEditOverrideActive_ = false;
            SortBodyKeys();
            ApplyTimelinePose();
            return;
        }
    }

    bodyKeys_.push_back(key);
    bodyEditOverrideActive_ = false;
    duration_ = (std::max)(duration_, key.time);
    SortBodyKeys();
    for (int index = 0; index < static_cast<int>(bodyKeys_.size()); ++index) {
        if (std::abs(bodyKeys_[index].time - key.time) <= kKeyEpsilon) {
            selectedBodyKeyIndex_ = index;
            break;
        }
    }
    ApplyTimelinePose();
}

void AnimationWorkbench::DeleteSelectedBodyKey() {
    // 削除後はその時刻の補間姿勢へ UI を戻し、未保存の一時姿勢を消す。
    if (selectedBodyKeyIndex_ < 0 || selectedBodyKeyIndex_ >= static_cast<int>(bodyKeys_.size())) {
        return;
    }
    bodyKeys_.erase(bodyKeys_.begin() + selectedBodyKeyIndex_);
    selectedBodyKeyIndex_ = -1;
    bodyEditOverrideActive_ = false;
    SyncBodyUiFromTimeline();
    ApplyTimelinePose();
}

bool AnimationWorkbench::TryGetInterpolatedBodyKey(float time, BodyAnimationClip::Key& keyOut) const {
    // 区間の開始キーに保存されたイージングを補間率へ適用する。
    if (bodyKeys_.empty()) {
        return false;
    }
    if (bodyKeys_.size() == 1 || time <= bodyKeys_.front().time) {
        keyOut = bodyKeys_.front();
        return true;
    }
    if (time >= bodyKeys_.back().time) {
        keyOut = bodyKeys_.back();
        return true;
    }

    for (size_t index = 0; index + 1 < bodyKeys_.size(); ++index) {
        const BodyAnimationClip::Key& from = bodyKeys_[index];
        const BodyAnimationClip::Key& to = bodyKeys_[index + 1];
        if (time < from.time || time > to.time) {
            continue;
        }
        const float segmentDuration = (std::max)(kKeyEpsilon, to.time - from.time);
        const float rawRate = (time - from.time) / segmentDuration;
        const float rate = AnimationInterpolation::ApplyEasing(
            rawRate,
            static_cast<AnimationInterpolation::EasingType>(std::clamp(from.easingToNext, 0, 4)));
        keyOut = from;
        keyOut.time = time;
        keyOut.translate = AnimationInterpolation::Lerp(from.translate, to.translate, rate);
        keyOut.rotate = AnimationInterpolation::SlerpEuler(from.rotate, to.rotate, rate);
        keyOut.scale = AnimationInterpolation::Lerp(from.scale, to.scale, rate);
        keyOut.shear = AnimationInterpolation::Lerp(from.shear, to.shear, rate);
        keyOut.shearPivotY = Math::Lerp(from.shearPivotY, to.shearPivotY, rate);
        return true;
    }
    keyOut = bodyKeys_.back();
    return true;
}

void AnimationWorkbench::SyncBodyUiFromKey(const BodyAnimationClip::Key& key) {
    // 保存値の回転はラジアンなので、操作欄には度へ戻して表示する。
    bodyTranslateUi_ = key.translate;
    bodyShearUi_ = key.shear;
    bodyShearPivotY_ = key.shearPivotY;
    bodyRotateDegUi_ = ToDegrees(key.rotate);
    bodyScaleUi_ = key.scale;
    bodyEasingToNext_ = std::clamp(key.easingToNext, 0, 4);
}

void AnimationWorkbench::SyncBodyUiFromTimeline() {
    // キーが無い場合は単位姿勢に戻し、前クリップの値を UI に残さない。
    BodyAnimationClip::Key key;
    if (TryGetInterpolatedBodyKey(currentTime_, key)) {
        SyncBodyUiFromKey(key);
    } else {
        bodyTranslateUi_ = {};
        bodyShearUi_ = {};
        bodyShearPivotY_ = 0.0f;
        bodyRotateDegUi_ = {};
        bodyScaleUi_ = { 1.0f, 1.0f, 1.0f };
        bodyEasingToNext_ = 4;
    }
    bodyEditOverrideActive_ = false;
}

void AnimationWorkbench::SortBodyKeys() {
    // 補間時に隣接キーを探せるよう時刻順を維持する。
    std::sort(bodyKeys_.begin(), bodyKeys_.end(), [](const BodyAnimationClip::Key& lhs, const BodyAnimationClip::Key& rhs) {
        return lhs.time < rhs.time;
    });
}

void AnimationWorkbench::ApplyPoseKey(const PoseKey& key) {
    // 関節の Euler 回転を骨格が使う Quaternion へ変換して適用する。
    if (!previewModel_) return;
    previewModel_->SetJointTransform(key.jointIndex, {
        key.scale,
        Math::EulerToQuaternion(key.rotate),
        key.translate
    });
}

AnimationWorkbench::PoseKey AnimationWorkbench::BuildPoseKeyFromUi() const {
    // 保存時は関節番号と名前を両方持ち、モデル更新後の再割り当てに備える。
    PoseKey key;
    key.time = currentTime_;
    key.jointIndex = selectedJointIndex_;
    key.translate = jointTranslateUi_;
    key.rotate = ToRadians(jointRotateDegUi_);
    key.scale = jointScaleUi_;
    if (previewModel_ && selectedJointIndex_ >= 0 && selectedJointIndex_ < static_cast<int>(previewModel_->GetJoints().size())) {
        key.jointName = previewModel_->GetJoints()[selectedJointIndex_].name;
    }
    return key;
}

void AnimationWorkbench::AddOrUpdateKey() {
    // 同じ関節・同じ時刻のキーは置換し、一時プレビュー値を確定値へ移す。
    if (!previewModel_ || selectedJointIndex_ < 0) return;

    PoseKey key = BuildPoseKeyFromUi();
    for (PoseKey& existing : keys_) {
        if (existing.jointIndex == key.jointIndex && std::abs(existing.time - key.time) <= kKeyEpsilon) {
            existing = key;
            SortKeys();
            ClearEditOverride(key.jointIndex);
            ApplyTimelinePose();
            return;
        }
    }

    keys_.push_back(key);
    SortKeys();
    ClearEditOverride(key.jointIndex);
    ApplyTimelinePose();
}

void AnimationWorkbench::DeleteSelectedJointKeyAtCurrentTime() {
    // 他の関節や別時刻のキーを残して、選択関節の現在キーだけ消す。
    if (selectedJointIndex_ < 0) return;
    keys_.erase(
        std::remove_if(keys_.begin(), keys_.end(), [&](const PoseKey& key) {
            return key.jointIndex == selectedJointIndex_ && std::abs(key.time - currentTime_) <= kKeyEpsilon;
        }),
        keys_.end());
    ApplyTimelinePose();
}

bool AnimationWorkbench::TryGetInterpolatedKey(int jointIndex, float time, PoseKey& keyOut) const {
    // 対象関節の前後キーだけを取り、回転は Quaternion 補間でつなぐ。
    std::vector<PoseKey> jointKeys;
    for (const PoseKey& key : keys_) {
        if (key.jointIndex == jointIndex) {
            jointKeys.push_back(key);
        }
    }
    if (jointKeys.empty()) return false;
    std::sort(jointKeys.begin(), jointKeys.end(), [](const PoseKey& a, const PoseKey& b) { return a.time < b.time; });

    if (time <= jointKeys.front().time) {
        keyOut = jointKeys.front();
        return true;
    }
    if (time >= jointKeys.back().time) {
        keyOut = jointKeys.back();
        return true;
    }

    for (size_t i = 0; i + 1 < jointKeys.size(); ++i) {
        const PoseKey& a = jointKeys[i];
        const PoseKey& b = jointKeys[i + 1];
        if (time >= a.time && time <= b.time) {
            float t = (time - a.time) / (b.time - a.time);
            keyOut = a;
            keyOut.time = time;
            keyOut.translate = Math::Lerp(a.translate, b.translate, t);
            Quaternion qA = Math::EulerToQuaternion(a.rotate);
            Quaternion qB = Math::EulerToQuaternion(b.rotate);
            keyOut.rotate = Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(Math::Slerp(qA, qB, t)));
            keyOut.scale = Math::Lerp(a.scale, b.scale, t);
            return true;
        }
    }
    return false;
}

void AnimationWorkbench::SyncUiFromJoint(int jointIndex) {
    // プレビュー骨格の現在姿勢を編集欄へ戻す際は角度を度へ変換する。
    if (!previewModel_) return;
    Model::QuaternionTransform transform = previewModel_->GetJointTransform(jointIndex);
    jointTranslateUi_ = transform.translate;
    jointRotateDegUi_ = ToDegrees(Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(transform.rotate)));
    jointScaleUi_ = transform.scale;
}

void AnimationWorkbench::SortKeys() {
    // 時刻が近いキーは関節番号で整列し、一覧の順序を安定させる。
    std::sort(keys_.begin(), keys_.end(), [](const PoseKey& a, const PoseKey& b) {
        if (std::abs(a.time - b.time) > kKeyEpsilon) return a.time < b.time;
        return a.jointIndex < b.jointIndex;
    });
}

void AnimationWorkbench::RefreshAssetFiles() {
    // 作業ディレクトリ内の JSON だけを候補にし、名前順で表示する。
    namespace fs = std::filesystem;
    assetFiles_.clear();
    if (!fs::exists(kSaveDirectory)) {
        return;
    }
    for (const auto& entry : fs::directory_iterator(kSaveDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            assetFiles_.push_back(entry.path().stem().string());
        }
    }
    std::sort(assetFiles_.begin(), assetFiles_.end());
}

void AnimationWorkbench::NewAuthoringClip() {
    // モデルは維持したままキー・イベント・一時姿勢を初期状態へ戻す。
    CopyToBuffer(saveFileBuffer_, sizeof(saveFileBuffer_), "new_body_animation.json");
    bodyKeys_.clear();
    keys_.clear();
    events_.clear();
    ClearAllEditOverrides();
    bodyEditOverrideActive_ = false;
    selectedBodyKeyIndex_ = -1;
    selectedJointIndex_ = -1;
    selectedEventIndex_ = -1;
    currentTime_ = 0.0f;
    duration_ = 1.0f;
    playbackSpeed_ = 1.0f;
    loop_ = true;
    play_ = false;
    bodyTranslateUi_ = {};
    bodyShearUi_ = {};
    bodyShearPivotY_ = 0.0f;
    bodyRotateDegUi_ = {};
    bodyScaleUi_ = { 1.0f, 1.0f, 1.0f };
    bodyEasingToNext_ = 4;
    lastEventPreviewText_.clear();
    eventPreviewTimer_ = 0.0f;
    ApplyTimelinePose();
}

void AnimationWorkbench::SaveAuthoringJson() {
    // 本体キー、関節キー、イベントを同じファイルに保存して再編集可能にする。
    namespace fs = std::filesystem;
    fs::create_directories(kSaveDirectory);

    nlohmann::json data;
    data["version"] = BodyAnimationClip::kCurrentVersion;
    data["name"] = fs::path(saveFileBuffer_).stem().string();
    data["model"] = modelNameBuffer_;
    data["baseAnimation"] = animationNameBuffer_;
    data["duration"] = duration_;
    data["loop"] = loop_;
    data["previewPlaybackSpeed"] = playbackSpeed_;
    data["bodyKeys"] = nlohmann::json::array();
    for (const BodyAnimationClip::Key& key : bodyKeys_) {
        data["bodyKeys"].push_back({
            {"time", key.time},
            {"translate", {key.translate.x, key.translate.y, key.translate.z}},
            {"rotate", {key.rotate.x, key.rotate.y, key.rotate.z}},
            {"scale", {key.scale.x, key.scale.y, key.scale.z}},
            {"shear", {key.shear.x, 0.0f, key.shear.z}},
            {"shearPivotY", key.shearPivotY},
            {"easingToNext", key.easingToNext}
        });
    }
    data["keys"] = nlohmann::json::array();
    for (const PoseKey& key : keys_) {
        data["keys"].push_back({
            {"time", key.time},
            {"jointIndex", key.jointIndex},
            {"jointName", key.jointName},
            {"translate", {key.translate.x, key.translate.y, key.translate.z}},
            {"rotate", {key.rotate.x, key.rotate.y, key.rotate.z}},
            {"scale", {key.scale.x, key.scale.y, key.scale.z}}
        });
    }

    data["events"] = nlohmann::json::array();
    for (const EventMarker& marker : events_) {
        data["events"].push_back({
            {"time", marker.time},
            {"type", marker.type},
            {"name", marker.name},
            {"offset", {marker.offset.x, marker.offset.y, marker.offset.z}}
        });
    }

    std::ofstream ofs(GetSavePath(), std::ios::binary);
    ofs << data.dump(2);
    DebugConsole::GetInstance()->AddLog(("Animation Workbench saved: " + GetSavePath()).c_str());
}

void AnimationWorkbench::LoadAuthoringJson() {
    // 現行保存先を優先し、旧フォルダのファイルも読み込めるようにする。
    std::ifstream ifs(GetSavePath(), std::ios::binary);
    if (!ifs) {
        std::string legacyFileName = saveFileBuffer_;
        if (legacyFileName.empty()) legacyFileName = "enemy_animation.json";
        if (legacyFileName.find(".json") == std::string::npos) legacyFileName += ".json";
        const std::string legacyPath = std::string(kLegacySaveDirectory) + legacyFileName;
        ifs.clear();
        ifs.open(legacyPath, std::ios::binary);
        if (!ifs) {
            DebugConsole::GetInstance()->AddLog(("Animation Workbench json not found: " + GetSavePath()).c_str());
            return;
        }
    }

    nlohmann::json data = nlohmann::json::parse(ifs, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        DebugConsole::GetInstance()->AddLog("Animation Workbench: invalid clip JSON");
        return;
    }

    ClearAllEditOverrides();
    lastEventPreviewText_.clear();
    eventPreviewTimer_ = 0.0f;

    std::string modelName = data.value("model", "");
    std::string baseAnimation = data.value("baseAnimation", "");
    float loadedDuration = data.value("duration", 2.0f);
    CopyToBuffer(modelNameBuffer_, sizeof(modelNameBuffer_), modelName);

    if (modelNameBuffer_[0] != '\0') {
        LoadPreviewModel(modelNameBuffer_);
    }
    CopyToBuffer(animationNameBuffer_, sizeof(animationNameBuffer_), baseAnimation);
    duration_ = loadedDuration;
    loop_ = data.value("loop", true);
    playbackSpeed_ = data.value("previewPlaybackSpeed", 1.0f);
    useBaseAnimation_ = animationNameBuffer_[0] != '\0';

    bodyKeys_.clear();
    for (const auto& item : data.value("bodyKeys", nlohmann::json::array())) {
        BodyAnimationClip::Key key;
        key.time = item.value("time", 0.0f);
        auto translate = item.value("translate", std::vector<float>{0.0f, 0.0f, 0.0f});
        auto rotate = item.value("rotate", std::vector<float>{0.0f, 0.0f, 0.0f});
        auto scale = item.value("scale", std::vector<float>{1.0f, 1.0f, 1.0f});
        if (translate.size() >= 3) key.translate = { translate[0], translate[1], translate[2] };
        if (rotate.size() >= 3) key.rotate = { rotate[0], rotate[1], rotate[2] };
        if (scale.size() >= 3) key.scale = { scale[0], scale[1], scale[2] };
        const auto shear = item.value("shear", std::vector<float>{0.0f, 0.0f, 0.0f});
        if (shear.size() >= 3) key.shear = { shear[0], 0.0f, shear[2] };
        key.shearPivotY = item.value("shearPivotY", 0.0f);
        key.easingToNext = std::clamp(item.value("easingToNext", 4), 0, 4);
        bodyKeys_.push_back(key);
    }

    keys_.clear();
    for (const auto& item : data.value("keys", nlohmann::json::array())) {
        PoseKey key;
        key.time = item.value("time", 0.0f);
        key.jointIndex = item.value("jointIndex", -1);
        key.jointName = item.value("jointName", "");
        if (previewModel_ && !key.jointName.empty()) {
            int index = previewModel_->FindJointIndex(key.jointName);
            if (index >= 0) key.jointIndex = index;
        }
        auto translate = item.value("translate", std::vector<float>{0.0f, 0.0f, 0.0f});
        auto rotate = item.value("rotate", std::vector<float>{0.0f, 0.0f, 0.0f});
        auto scale = item.value("scale", std::vector<float>{1.0f, 1.0f, 1.0f});
        if (translate.size() >= 3) key.translate = { translate[0], translate[1], translate[2] };
        if (rotate.size() >= 3) key.rotate = { rotate[0], rotate[1], rotate[2] };
        if (scale.size() >= 3) key.scale = { scale[0], scale[1], scale[2] };
        keys_.push_back(key);
    }

    events_.clear();
    for (const auto& item : data.value("events", nlohmann::json::array())) {
        EventMarker marker;
        marker.time = item.value("time", 0.0f);
        marker.type = item.value("type", 0);
        marker.name = item.value("name", "");
        auto offset = item.value("offset", std::vector<float>{0.0f, 0.0f, 0.0f});
        if (offset.size() >= 3) marker.offset = { offset[0], offset[1], offset[2] };
        events_.push_back(marker);
    }

    SortBodyKeys();
    SortKeys();
    selectedBodyKeyIndex_ = -1;
    bodyEditOverrideActive_ = false;
    SyncBodyUiFromTimeline();
    ApplyTimelinePose();
    DebugConsole::GetInstance()->AddLog(("Animation Workbench loaded json: " + GetSavePath()).c_str());
}

std::string AnimationWorkbench::GetSavePath() const {
    // 拡張子が省略された名前でもワークベンチ専用ディレクトリの JSON へ揃える。
    std::string filename = saveFileBuffer_;
    if (filename.empty()) filename = "new_body_animation.json";
    if (filename.find(".json") == std::string::npos) {
        filename += ".json";
    }
    return std::string(kSaveDirectory) + filename;
}

void AnimationWorkbench::StoreEditOverrideFromUi() {
    // ギズモ操作中の未確定姿勢をキーへ保存せず、プレビュー上だけ優先する。
    if (!previewModel_ || selectedJointIndex_ < 0) return;
    editPoseOverrides_[selectedJointIndex_] = BuildPoseKeyFromUi();
}

void AnimationWorkbench::ClearEditOverride(int jointIndex) {
    // 確定した関節の一時姿勢だけ消し、他の編集中関節には触れない。
    editPoseOverrides_.erase(jointIndex);
}

void AnimationWorkbench::ClearAllEditOverrides() {
    // クリップやモデル変更時に旧骨格の一時編集を持ち越さない。
    editPoseOverrides_.clear();
}

// 順再生・逆再生の双方で、前回時刻と今回時刻の間を横切ったイベントだけをプレビューします。
// 終端から先頭へループした場合は区間を二つに分け、境界での二重発火を避けます。
void AnimationWorkbench::UpdateEventPreview(float previousTime, float currentTime) {
    // 順再生と逆再生で区間境界を変え、キー通過時の二重発火を防ぐ。
    if (events_.empty()) return;

    bool forward = playbackSpeed_ >= 0.0f;
    for (const EventMarker& marker : events_) {
        bool crossed = false;
        if (forward) {
            if (currentTime >= previousTime) {
                crossed = marker.time > previousTime + kKeyEpsilon && marker.time <= currentTime + kKeyEpsilon;
            } else {
                crossed = marker.time > previousTime + kKeyEpsilon || marker.time <= currentTime + kKeyEpsilon;
            }
        } else {
            if (currentTime <= previousTime) {
                crossed = marker.time < previousTime - kKeyEpsilon && marker.time >= currentTime - kKeyEpsilon;
            } else {
                crossed = marker.time < previousTime - kKeyEpsilon || marker.time >= currentTime - kKeyEpsilon;
            }
        }

        if (crossed) {
            FireEventPreview(marker);
        }
    }
}

void AnimationWorkbench::FireEventPreview(const EventMarker& marker) {
    // ゲーム本体のイベント処理は呼ばず、内容を画面とログへ短時間表示する。
    lastEventPreviewText_ = FormatEventMarker(marker);
    eventPreviewTimer_ = 1.5f;
    DebugConsole::GetInstance()->AddLog(("Animation Event Preview: " + lastEventPreviewText_).c_str());
}

std::string AnimationWorkbench::FormatEventMarker(const EventMarker& marker) const {
    // 時刻・種類・位置を一行にまとめてイベントの確認欄へ表示する。
    int typeIndex = (std::clamp)(marker.type, 0, kEventTypeCount - 1);
    char buffer[256] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "[%.3f] %s : %s (%.2f, %.2f, %.2f)",
        marker.time,
        kEventTypeNames[typeIndex],
        marker.name.c_str(),
        marker.offset.x,
        marker.offset.y,
        marker.offset.z);
    return buffer;
}

Vector3 AnimationWorkbench::ToDegrees(const Vector3& radians) const {
    // 保存と骨格で使うラジアンを UI の度へ戻す。
    return {
        radians.x * 180.0f / kPi,
        radians.y * 180.0f / kPi,
        radians.z * 180.0f / kPi
    };
}

Vector3 AnimationWorkbench::ToRadians(const Vector3& degrees) const {
    // UI で入力した度を保存と骨格で使うラジアンへ変換する。
    return {
        degrees.x * kPi / 180.0f,
        degrees.y * kPi / 180.0f,
        degrees.z * kPi / 180.0f
    };
}

void AnimationWorkbench::SetGameViewRegion(const Vector2& offset, const Vector2& size) {
    // 3D ギズモの入力と投影を Game View の表示矩形に合わせる。
    gameViewOffset_ = offset;
    gameViewSize_ = size;
}

void AnimationWorkbench::DrawBodyGizmo() {
    // ワールド操作をプレビュー原点からの相対値に戻して本体キーへ反映する。
#ifdef USE_IMGUI
    if (!EditorToolSettings::Get().IsVisible("transformGizmo")) return;
    if (!enabled_ || !previewObject_ || !enableBodyGizmo_) return;
    if (gameViewSize_.x <= 1.0f || gameViewSize_.y <= 1.0f) return;

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return;

    Matrix4x4 view = camera->GetViewMatrix();
    Matrix4x4 projection = camera->GetProjectionMatrix();
    Matrix4x4 bodyWorld = previewObject_->GetWorldMatrix();

    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(gameViewOffset_.x, gameViewOffset_.y, gameViewSize_.x, gameViewSize_.y);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (gizmoOperation_ == 1) operation = ImGuizmo::ROTATE;
    if (gizmoOperation_ == 2) operation = ImGuizmo::SCALE;

    ImGuizmo::Manipulate(
        &view.m[0][0],
        &projection.m[0][0],
        operation,
        ImGuizmo::WORLD,
        &bodyWorld.m[0][0],
        nullptr,
        nullptr);

    if (!ImGuizmo::IsUsing()) return;

    Vector3 worldScale;
    Vector3 worldRotateDeg;
    Vector3 worldTranslate;
    ImGuizmo::DecomposeMatrixToComponents(
        &bodyWorld.m[0][0],
        &worldTranslate.x,
        &worldRotateDeg.x,
        &worldScale.x);

    bodyTranslateUi_ = {
        worldTranslate.x - previewOrigin_.x,
        worldTranslate.y - previewOrigin_.y,
        worldTranslate.z - previewOrigin_.z
    };
    bodyRotateDegUi_ = worldRotateDeg;
    bodyScaleUi_ = {
        worldScale.x / (std::max)(previewScale_.x, 0.0001f),
        worldScale.y / (std::max)(previewScale_.y, 0.0001f),
        worldScale.z / (std::max)(previewScale_.z, 0.0001f)
    };
    bodyEditOverrideActive_ = true;
    ApplyTimelinePose();
    if (autoKeyBodyOnGizmo_) {
        AddOrUpdateBodyKey();
    }
#endif
}

void AnimationWorkbench::DrawBoneOverlayAndGizmo() {
#ifdef USE_IMGUI
    if (!EditorToolSettings::Get().IsVisible("bones")) return;
    if (!enabled_ || !previewModel_ || !previewObject_ || !showBoneOverlay_) return;
    if (gameViewSize_.x <= 1.0f || gameViewSize_.y <= 1.0f) return;

    const auto& joints = previewModel_->GetJoints();
    if (joints.empty()) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 clipMin(gameViewOffset_.x, gameViewOffset_.y);
    const ImVec2 clipMax(gameViewOffset_.x + gameViewSize_.x, gameViewOffset_.y + gameViewSize_.y);
    drawList->PushClipRect(clipMin, clipMax, true);

    const ImVec2 mouseScreen = ImGui::GetIO().MousePos;
    int hoveredJoint = -1;
    float bestHoverDistance = (std::max)(bonePointRadius_ + 4.0f, boneLineThickness_ + 4.0f);

    auto distanceToSegment = [](const ImVec2& point, const ImVec2& start, const ImVec2& end) {
        const float segmentX = end.x - start.x;
        const float segmentY = end.y - start.y;
        const float lengthSquared = segmentX * segmentX + segmentY * segmentY;
        if (lengthSquared <= 0.0001f) {
            const float dx = point.x - start.x;
            const float dy = point.y - start.y;
            return std::sqrt(dx * dx + dy * dy);
        }
        const float projection = std::clamp(
            ((point.x - start.x) * segmentX + (point.y - start.y) * segmentY) / lengthSquared,
            0.0f,
            1.0f);
        const float closestX = start.x + segmentX * projection;
        const float closestY = start.y + segmentY * projection;
        const float dx = point.x - closestX;
        const float dy = point.y - closestY;
        return std::sqrt(dx * dx + dy * dy);
    };

    auto isSelectedAncestor = [&](int jointIndex) {
        if (selectedJointIndex_ < 0 || selectedJointIndex_ >= static_cast<int>(joints.size())) return false;
        int current = selectedJointIndex_;
        for (size_t guard = 0; guard < joints.size(); ++guard) {
            if (current == jointIndex) return true;
            if (!joints[current].parent) break;
            current = *joints[current].parent;
            if (current < 0 || current >= static_cast<int>(joints.size())) break;
        }
        return false;
    };

    // ボーン線を先に描き、ジョイント点が常に手前に見えるようにする。
    for (const auto& joint : joints) {
        if (!joint.parent || *joint.parent < 0 || *joint.parent >= static_cast<int>(joints.size())) continue;

        const Vector3 jointWorld = GetMatrixTranslation(GetJointWorldMatrix(joint.index));
        const Vector3 parentWorld = GetMatrixTranslation(GetJointWorldMatrix(*joint.parent));
        Vector2 jointScreen{};
        Vector2 parentScreen{};
        if (!ProjectWorldToGameView(jointWorld, jointScreen) || !ProjectWorldToGameView(parentWorld, parentScreen)) continue;

        const bool selectedChain = joint.index == selectedJointIndex_ ||
            *joint.parent == selectedJointIndex_ || isSelectedAncestor(joint.index);
        const ImU32 lineColor = selectedChain
            ? IM_COL32(255, 205, 64, 245)
            : IM_COL32(90, 215, 255, 215);
        const ImVec2 start(parentScreen.x, parentScreen.y);
        const ImVec2 end(jointScreen.x, jointScreen.y);
        drawList->AddLine(start, end, IM_COL32(5, 18, 28, 235), boneLineThickness_ + 3.0f);
        drawList->AddLine(start, end, lineColor, boneLineThickness_);

        if (isGameViewHovered_) {
            const float distance = distanceToSegment(mouseScreen, start, end);
            if (distance < bestHoverDistance) {
                bestHoverDistance = distance;
                hoveredJoint = joint.index;
            }
        }
    }

    for (const auto& joint : joints) {
        const Vector3 jointWorld = GetMatrixTranslation(GetJointWorldMatrix(joint.index));
        Vector2 jointScreen{};
        if (!ProjectWorldToGameView(jointWorld, jointScreen)) continue;

        const float dx = mouseScreen.x - jointScreen.x;
        const float dy = mouseScreen.y - jointScreen.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (isGameViewHovered_ && distance < bestHoverDistance) {
            bestHoverDistance = distance;
            hoveredJoint = joint.index;
        }

        const bool selected = joint.index == selectedJointIndex_;
        const bool hovered = joint.index == hoveredJoint;
        const ImU32 fillColor = selected
            ? IM_COL32(255, 210, 40, 255)
            : (hovered ? IM_COL32(150, 245, 255, 255) : IM_COL32(75, 185, 255, 235));
        const ImU32 outlineColor = selected || hovered
            ? IM_COL32(255, 255, 255, 255)
            : IM_COL32(6, 24, 36, 240);
        const float radius = selected ? bonePointRadius_ + 2.0f : (hovered ? bonePointRadius_ + 1.0f : bonePointRadius_);

        drawList->AddCircleFilled(ImVec2(jointScreen.x, jointScreen.y), radius, fillColor, 18);
        drawList->AddCircle(ImVec2(jointScreen.x, jointScreen.y), radius + 1.0f, outlineColor, 18, 1.5f);
        if (showBoneNames_ || hovered) {
            drawList->AddText(
                ImVec2(jointScreen.x + radius + 5.0f, jointScreen.y - 8.0f),
                hovered ? IM_COL32(255, 240, 150, 255) : IM_COL32(255, 255, 255, 225),
                joint.name.c_str());
        }
    }

    if (showBoneAxes_ && selectedJointIndex_ >= 0 && selectedJointIndex_ < static_cast<int>(joints.size())) {
        const Matrix4x4 selectedWorld = GetJointWorldMatrix(selectedJointIndex_);
        const Vector3 originWorld = GetMatrixTranslation(selectedWorld);
        Vector2 originScreen{};
        if (ProjectWorldToGameView(originWorld, originScreen)) {
            float axisLength = 0.5f;
            if (joints[selectedJointIndex_].parent) {
                const Vector3 parentWorld = GetMatrixTranslation(GetJointWorldMatrix(*joints[selectedJointIndex_].parent));
                axisLength = std::clamp(Math::Length(originWorld - parentWorld) * 0.45f, 0.2f, 0.85f);
            }
            const Vector3 axes[] = {
                Math::Normalize(Math::TransformNormal({ 1.0f, 0.0f, 0.0f }, selectedWorld)),
                Math::Normalize(Math::TransformNormal({ 0.0f, 1.0f, 0.0f }, selectedWorld)),
                Math::Normalize(Math::TransformNormal({ 0.0f, 0.0f, 1.0f }, selectedWorld)),
            };
            const ImU32 axisColors[] = {
                IM_COL32(255, 82, 82, 255),
                IM_COL32(92, 235, 112, 255),
                IM_COL32(84, 145, 255, 255),
            };
            for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
                Vector2 axisScreen{};
                if (ProjectWorldToGameView(originWorld + axes[axisIndex] * axisLength, axisScreen)) {
                    drawList->AddLine(
                        ImVec2(originScreen.x, originScreen.y),
                        ImVec2(axisScreen.x, axisScreen.y),
                        axisColors[axisIndex],
                        2.5f);
                }
            }
        }
    }

    drawList->PopClipRect();

    if (hoveredJoint >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing()) {
        selectedJointIndex_ = hoveredJoint;
        SyncUiFromJoint(selectedJointIndex_);
    }

    if (!enableBoneGizmo_ || !EditorToolSettings::Get().IsVisible("transformGizmo") || selectedJointIndex_ < 0 || selectedJointIndex_ >= static_cast<int>(joints.size())) return;

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return;

    Matrix4x4 view = camera->GetViewMatrix();
    Matrix4x4 proj = camera->GetProjectionMatrix();
    Matrix4x4 jointWorld = GetJointWorldMatrix(selectedJointIndex_);

    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(gameViewOffset_.x, gameViewOffset_.y, gameViewSize_.x, gameViewSize_.y);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (gizmoOperation_ == 1) operation = ImGuizmo::ROTATE;
    if (gizmoOperation_ == 2) operation = ImGuizmo::SCALE;

    ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], operation, ImGuizmo::WORLD, &jointWorld.m[0][0], nullptr, nullptr);

    if (ImGuizmo::IsUsing()) {
        const auto& selectedJoint = joints[selectedJointIndex_];
        Matrix4x4 parentWorld = previewObject_->GetWorldMatrix();
        if (selectedJoint.parent) {
            parentWorld = GetJointWorldMatrix(*selectedJoint.parent);
        }
        Matrix4x4 localMatrix = Math::Multiply(jointWorld, Math::Inverse(parentWorld));

        Vector3 scale;
        Vector3 rotateDeg;
        Vector3 translate;
        ImGuizmo::DecomposeMatrixToComponents(&localMatrix.m[0][0], &translate.x, &rotateDeg.x, &scale.x);

        jointTranslateUi_ = translate;
        jointRotateDegUi_ = rotateDeg;
        jointScaleUi_ = scale;

        StoreEditOverrideFromUi();
        ApplyTimelinePose();

        if (autoKeyOnGizmo_) {
            AddOrUpdateKey();
        }
    }
#endif
}

bool AnimationWorkbench::ProjectWorldToGameView(const Vector3& world, Vector2& screenOut) const {
    // 骨格のワールド位置を Game View 内のピクセル座標へ変換する。
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return false;

    Matrix4x4 viewProjection = Math::Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    Vector3 ndc = Math::Transform(world, viewProjection);
    if (ndc.z < 0.0f || ndc.z > 1.0f) return false;

    screenOut.x = gameViewOffset_.x + (ndc.x + 1.0f) * 0.5f * gameViewSize_.x;
    screenOut.y = gameViewOffset_.y + (1.0f - ndc.y) * 0.5f * gameViewSize_.y;
    return screenOut.x >= gameViewOffset_.x - 64.0f &&
           screenOut.x <= gameViewOffset_.x + gameViewSize_.x + 64.0f &&
           screenOut.y >= gameViewOffset_.y - 64.0f &&
           screenOut.y <= gameViewOffset_.y + gameViewSize_.y + 64.0f;
}

Matrix4x4 AnimationWorkbench::GetJointWorldMatrix(int jointIndex) const {
    // 骨格空間の行列へプレビュー Object のワールド行列を重ねる。
    if (!previewModel_ || !previewObject_) {
        return Math::MakeIdentity4x4();
    }
    const auto& joints = previewModel_->GetJoints();
    if (jointIndex < 0 || jointIndex >= static_cast<int>(joints.size())) {
        return previewObject_->GetWorldMatrix();
    }
    return Math::Multiply(joints[jointIndex].skeletonSpaceMatrix, previewObject_->GetWorldMatrix());
}

Vector3 AnimationWorkbench::GetMatrixTranslation(const Matrix4x4& matrix) const {
    // 行列の平行移動成分を骨表示の起点として取り出す。
    return { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };
}

void AnimationWorkbench::ApplyCameraOverride() {
    // 初回と再中心化要求時だけプレビュー模型を見やすい位置へ捉え直す。
    if (!enabled_) return;
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);

    if ((!hasPlacedCamera_ && cameraMoveOnEnter_) || recenterCameraRequested_) {
        Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        if (camera) {
            Vector3 target = previewOrigin_;
            Vector3 eye = {
                target.x,
                target.y + cameraHeight_,
                target.z - cameraDistance_
            };
            float pitch = std::atan2(cameraHeight_, cameraDistance_);
            camera->SetFollowTarget(nullptr);
            camera->SetEye(eye);
            camera->SetTarget(target);
            camera->SetRotation({ pitch, 0.0f, 0.0f });
            camera->Update();
        }
        hasPlacedCamera_ = true;
        recenterCameraRequested_ = false;
    }
}
