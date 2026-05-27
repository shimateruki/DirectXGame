#include "AnimationWorkbench.h"

#include "BaseScene.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "IconsFontAwesome5.h"
#include "ImGuizmo.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
constexpr const char* kPreviewObjectName = "__Editor_AnimationWorkbenchPreview";
constexpr const char* kSaveDirectory = "Resources/json/enemy_animation/";
constexpr float kPi = 3.14159265358979323846f;
constexpr float kKeyEpsilon = 0.001f;

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
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
}

void AnimationWorkbench::Finalize() {
    RemovePreviewObject();
    previewModel_ = nullptr;
    ClearAllEditOverrides();
}

void AnimationWorkbench::Update(float deltaTime) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    if (enabled_ && !wasEnabled_) {
        hasPlacedCamera_ = false;
        if (!previewObject_) {
            CreatePreviewObject();
        }
    }
    if (!enabled_ && wasEnabled_) {
        RemovePreviewObject();
        previewModel_ = nullptr;
        ClearAllEditOverrides();
        hasPlacedCamera_ = false;
    }
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

void AnimationWorkbench::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_RUNNING " Animation Workbench");
    ImGui::Separator();

    ImGui::Checkbox("有効", &enabled_);
    ImGui::SameLine();
    ImGui::Checkbox("自動反映", &autoApply_);

    DrawPreviewControls();
    DrawTimelineControls();
    DrawJointControls();
    DrawKeyframeControls();
    DrawEventControls();
    DrawBoneOverlayAndGizmo();
#endif
}

void AnimationWorkbench::DrawPreviewControls() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("プレビュー", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputText("モデル名", modelNameBuffer_, sizeof(modelNameBuffer_));

    std::vector<std::string> loadedModels = ModelManager::GetInstance()->GetLoadedModelNames();
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

    ImGui::DragFloat3("位置", &previewOrigin_.x, 0.1f);
    ImGui::DragFloat3("スケール", &previewScale_.x, 0.01f, 0.01f, 100.0f);
    ImGui::Checkbox("入室時にカメラ移動", &cameraMoveOnEnter_);
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

void AnimationWorkbench::DrawTimelineControls() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("タイムライン", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (ImGui::Button(play_ ? ICON_FA_PAUSE " 停止" : ICON_FA_PLAY " 再生")) {
        play_ = !play_;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STEP_BACKWARD " 先頭")) {
        currentTime_ = 0.0f;
        play_ = false;
        ApplyTimelinePose();
    }
    ImGui::SameLine();
    ImGui::Checkbox("ループ", &loop_);

    ImGui::DragFloat("長さ", &duration_, 0.01f, 0.01f, 60.0f, "%.2f");
    ImGui::DragFloat("再生速度", &playbackSpeed_, 0.01f, -5.0f, 5.0f, "%.2f");
    if (ImGui::SliderFloat("現在時間", &currentTime_, 0.0f, duration_, "%.3f")) {
        ApplyTimelinePose();
    }

    const float frameStep = 1.0f / 30.0f;
    if (ImGui::Button("-1F")) {
        currentTime_ = (std::max)(0.0f, currentTime_ - frameStep);
        ApplyTimelinePose();
    }
    ImGui::SameLine();
    if (ImGui::Button("+1F")) {
        currentTime_ = (std::min)(duration_, currentTime_ + frameStep);
        ApplyTimelinePose();
    }
#endif
}

void AnimationWorkbench::DrawJointControls() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("ボーン編集", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!previewModel_ || !previewModel_->HasSkeleton()) {
        ImGui::TextDisabled("モデルを読み込むとボーン一覧が表示されます。");
        return;
    }

    ImGui::Checkbox("ボーン表示", &showBoneOverlay_);
    ImGui::SameLine();
    ImGui::Checkbox("名前表示", &showBoneNames_);
    ImGui::Checkbox("ボーンギズモ", &enableBoneGizmo_);
    ImGui::SameLine();
    ImGui::Checkbox("ギズモ操作で自動キー", &autoKeyOnGizmo_);
    ImGui::DragFloat("ボーン点サイズ", &bonePointRadius_, 0.1f, 2.0f, 16.0f);

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
        std::string label = std::string(joint.parent ? "  " : "") + joint.name + "##joint" + std::to_string(joint.index);
        bool selected = (selectedJointIndex_ == joint.index);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selectedJointIndex_ = joint.index;
            SyncUiFromJoint(selectedJointIndex_);
        }
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

    ImGui::InputText("保存ファイル", saveFileBuffer_, sizeof(saveFileBuffer_));
    if (ImGui::Button(ICON_FA_SAVE " 保存")) {
        SaveAuthoringJson();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " 読み込み")) {
        LoadAuthoringJson();
    }
#endif
}

void AnimationWorkbench::DrawEventControls() {
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
    if (modelName.empty()) return false;

    Model* model = ModelManager::GetInstance()->LoadModel(modelName);
    if (!model || model->GetMeshCount() == 0 || model->GetVertexCount() == 0 || model->GetPolygonCount() == 0) {
        DebugConsole::GetInstance()->AddLog(("Animation Workbench failed to load mesh data: " + modelName).c_str());
        return false;
    }

    previewModel_ = model;
    keys_.clear();
    events_.clear();
    ClearAllEditOverrides();
    lastEventPreviewText_.clear();
    eventPreviewTimer_ = 0.0f;
    selectedJointIndex_ = -1;
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
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        previewObject_ = nullptr;
        return;
    }
    if (Object3d* object = FindPreviewObject()) {
        sceneManager_->GetCurrentScene()->RequestRemoveObject(object);
    }
    previewObject_ = nullptr;
}

Object3d* AnimationWorkbench::FindPreviewObject() const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;
    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == kPreviewObjectName) {
            return object.get();
        }
    }
    return nullptr;
}

void AnimationWorkbench::ApplyTimelinePose() {
    if (!previewModel_) return;

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

    for (const auto& [jointIndex, key] : editPoseOverrides_) {
        (void)jointIndex;
        ApplyPoseKey(key);
    }
}

void AnimationWorkbench::ApplyPoseKey(const PoseKey& key) {
    if (!previewModel_) return;
    previewModel_->SetJointTransform(key.jointIndex, {
        key.scale,
        Math::EulerToQuaternion(key.rotate),
        key.translate
    });
}

AnimationWorkbench::PoseKey AnimationWorkbench::BuildPoseKeyFromUi() const {
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
    if (selectedJointIndex_ < 0) return;
    keys_.erase(
        std::remove_if(keys_.begin(), keys_.end(), [&](const PoseKey& key) {
            return key.jointIndex == selectedJointIndex_ && std::abs(key.time - currentTime_) <= kKeyEpsilon;
        }),
        keys_.end());
    ApplyTimelinePose();
}

bool AnimationWorkbench::TryGetInterpolatedKey(int jointIndex, float time, PoseKey& keyOut) const {
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
    if (!previewModel_) return;
    Model::QuaternionTransform transform = previewModel_->GetJointTransform(jointIndex);
    jointTranslateUi_ = transform.translate;
    jointRotateDegUi_ = ToDegrees(Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(transform.rotate)));
    jointScaleUi_ = transform.scale;
}

void AnimationWorkbench::SortKeys() {
    std::sort(keys_.begin(), keys_.end(), [](const PoseKey& a, const PoseKey& b) {
        if (std::abs(a.time - b.time) > kKeyEpsilon) return a.time < b.time;
        return a.jointIndex < b.jointIndex;
    });
}

void AnimationWorkbench::SaveAuthoringJson() {
    namespace fs = std::filesystem;
    fs::create_directories(kSaveDirectory);

    nlohmann::json data;
    data["model"] = modelNameBuffer_;
    data["baseAnimation"] = animationNameBuffer_;
    data["duration"] = duration_;
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
    std::ifstream ifs(GetSavePath(), std::ios::binary);
    if (!ifs) {
        DebugConsole::GetInstance()->AddLog(("Animation Workbench json not found: " + GetSavePath()).c_str());
        return;
    }

    nlohmann::json data;
    ifs >> data;

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
    useBaseAnimation_ = animationNameBuffer_[0] != '\0';

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

    SortKeys();
    ApplyTimelinePose();
    DebugConsole::GetInstance()->AddLog(("Animation Workbench loaded json: " + GetSavePath()).c_str());
}

std::string AnimationWorkbench::GetSavePath() const {
    std::string filename = saveFileBuffer_;
    if (filename.empty()) filename = "enemy_animation.json";
    if (filename.find(".json") == std::string::npos) {
        filename += ".json";
    }
    return std::string(kSaveDirectory) + filename;
}

void AnimationWorkbench::StoreEditOverrideFromUi() {
    if (!previewModel_ || selectedJointIndex_ < 0) return;
    editPoseOverrides_[selectedJointIndex_] = BuildPoseKeyFromUi();
}

void AnimationWorkbench::ClearEditOverride(int jointIndex) {
    editPoseOverrides_.erase(jointIndex);
}

void AnimationWorkbench::ClearAllEditOverrides() {
    editPoseOverrides_.clear();
}

void AnimationWorkbench::UpdateEventPreview(float previousTime, float currentTime) {
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
    lastEventPreviewText_ = FormatEventMarker(marker);
    eventPreviewTimer_ = 1.5f;
    DebugConsole::GetInstance()->AddLog(("Animation Event Preview: " + lastEventPreviewText_).c_str());
}

std::string AnimationWorkbench::FormatEventMarker(const EventMarker& marker) const {
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
    return {
        radians.x * 180.0f / kPi,
        radians.y * 180.0f / kPi,
        radians.z * 180.0f / kPi
    };
}

Vector3 AnimationWorkbench::ToRadians(const Vector3& degrees) const {
    return {
        degrees.x * kPi / 180.0f,
        degrees.y * kPi / 180.0f,
        degrees.z * kPi / 180.0f
    };
}

void AnimationWorkbench::SetGameViewRegion(const Vector2& offset, const Vector2& size) {
    gameViewOffset_ = offset;
    gameViewSize_ = size;
}

void AnimationWorkbench::DrawBoneOverlayAndGizmo() {
#ifdef USE_IMGUI
    if (!enabled_ || !previewModel_ || !previewObject_ || !showBoneOverlay_) return;
    if (gameViewSize_.x <= 1.0f || gameViewSize_.y <= 1.0f) return;

    const auto& joints = previewModel_->GetJoints();
    if (joints.empty()) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 mouseScreen = ImGui::GetIO().MousePos;
    int clickedJoint = -1;
    float bestClickDistance = bonePointRadius_ + 3.0f;

    for (const auto& joint : joints) {
        Vector3 jointWorld = GetMatrixTranslation(GetJointWorldMatrix(joint.index));
        Vector2 jointScreen;
        if (!ProjectWorldToGameView(jointWorld, jointScreen)) continue;

        if (joint.parent) {
            Vector3 parentWorld = GetMatrixTranslation(GetJointWorldMatrix(*joint.parent));
            Vector2 parentScreen;
            if (ProjectWorldToGameView(parentWorld, parentScreen)) {
                drawList->AddLine(
                    ImVec2(parentScreen.x, parentScreen.y),
                    ImVec2(jointScreen.x, jointScreen.y),
                    IM_COL32(70, 220, 255, 160),
                    1.5f);
            }
        }

        bool selected = (joint.index == selectedJointIndex_);
        ImU32 fillColor = selected ? IM_COL32(255, 210, 40, 255) : IM_COL32(90, 190, 255, 220);
        ImU32 outlineColor = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(10, 30, 40, 230);
        float radius = selected ? bonePointRadius_ + 2.0f : bonePointRadius_;

        drawList->AddCircleFilled(ImVec2(jointScreen.x, jointScreen.y), radius, fillColor, 16);
        drawList->AddCircle(ImVec2(jointScreen.x, jointScreen.y), radius + 1.0f, outlineColor, 16, 1.5f);
        if (showBoneNames_) {
            drawList->AddText(ImVec2(jointScreen.x + radius + 4.0f, jointScreen.y - 7.0f), IM_COL32(255, 255, 255, 220), joint.name.c_str());
        }

        float dx = mouseScreen.x - jointScreen.x;
        float dy = mouseScreen.y - jointScreen.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        if (isGameViewHovered_ && distance < bestClickDistance) {
            bestClickDistance = distance;
            clickedJoint = joint.index;
        }
    }

    if (clickedJoint >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing()) {
        selectedJointIndex_ = clickedJoint;
        SyncUiFromJoint(selectedJointIndex_);
    }

    if (!enableBoneGizmo_ || selectedJointIndex_ < 0 || selectedJointIndex_ >= static_cast<int>(joints.size())) return;

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return;

    Matrix4x4 view = camera->GetViewMatrix();
    Matrix4x4 proj = camera->GetProjectionMatrix();
    Matrix4x4 jointWorld = GetJointWorldMatrix(selectedJointIndex_);

    ImGuizmo::SetDrawlist();
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
    return { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };
}

void AnimationWorkbench::ApplyCameraOverride() {
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
