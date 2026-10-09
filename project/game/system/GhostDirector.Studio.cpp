#define NOMINMAX
#include "GhostDirector.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "Object3d.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugEditor.h"
#include "EffectPreviewStage.h"
#include "PostEffect.h"
#include "SRVManager.h"
#include "ModelManager.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "imgui.h"
#include <cmath>
#include <memory>
#include <algorithm>

namespace {
constexpr const char* kNames[] = {"__Editor_Cinematic_Actor", "__Editor_Cinematic_Gate", "__Editor_Cinematic_Camera",
                                  "__Editor_Cinematic_Floor"};
constexpr const char* kRoles[] = {"$Actor", "$Gate", "$Camera"};
constexpr const char* kLabels[] = {"演出対象", "基準オブジェクト", "演出カメラ"};
constexpr Vector3 kOrigin{16000.0f, 10000.0f, 16000.0f};
} // namespace

int GhostDirector::GetRoleIndex(const std::string& name) {
    for (int index = 0; index < 3; ++index)
        if (name == kRoles[index])
            return index;
    return -1;
}

Object3d* GhostDirector::FindStudioObject(int role) const {
    if (role < 0 || role >= 4 || !sceneManager_ || sceneManager_->GetCurrentScene() != studioScene_)
        return nullptr;
    for (const auto& object : studioScene_->GetObjects())
        if (object && object->IsEditorInternal() && object->GetName() == kNames[role])
            return object.get();
    return nullptr;
}

void GhostDirector::CreateStudioObjects() {
    for (int index = 0; index < 4; ++index) {
        auto object = std::make_unique<Object3d>();
        object->Initialize(studioScene_->GetObject3dCommon());
        object->SetName(kNames[index]);
        object->SetClassName(index == 2 ? "CinematicCamera" : "EditorOnly");
        object->SetEditorInternal(true);
        object->SetIsLocked(true);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
        object->SetTranslate(kOrigin);
        object->SetCastShadow(false);
        if (index != 2) {
            object->SetModel(index < 2 ? sequence_.previewModels[index] : "Primitives/cube");
            object->SetEnableLighting(false);
        }
        if (index == 0) {
            object->SetScale({1.7f, 1.7f, 1.7f});
            object->SetTranslate(kOrigin + Vector3{0, 0, 4});
        } else if (index == 2) {
            object->SetTranslate(kOrigin + Vector3{-6, 4, -10});
            object->SetRotation({0.2f, 0.4f, 0});
        } else if (index == 3) {
            object->SetTranslate(kOrigin + Vector3{0, -0.3f, 0});
            object->SetScale({24, 0.2f, 24});
            object->SetColor({0.22f, 0.25f, 0.29f, 1});
        }
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();
        studioScene_->AddObject(std::move(object));
    }
}

bool GhostDirector::BeginStudio() {
    if (studioEnabled_)
        return true;
    if (!sceneManager_ || sceneManager_->IsPlaying() || !sceneManager_->GetCurrentScene() ||
        !sceneManager_->GetCurrentScene()->GetObject3dCommon()) {
        fileOperationStatus_ = "制作空間はゲームを停止した編集モードで開いてください。";
        return false;
    }
    if (EffectPreviewStage::GetInstance()->IsEnabled() ||
        (editor_ &&
         (editor_->GetAnimationWorkbench()->IsEnabled() || editor_->GetEnemyAttackPreviewWindow()->IsEnabled()))) {
        fileOperationStatus_ =
            "他の独立プレビューを終了してから制作空間を開いてください。カメラ・エフェクトの競合を防ぎます。";
        return false;
    }
    auto* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera)
        return false;
    player_.Stop(true);
    studioStoredEye_ = camera->GetEye();
    studioStoredTarget_ = camera->GetTargetPoint();
    studioStoredRotation_ = camera->GetRotation();
    studioStoredFov_ = camera->GetFovY();
    studioStoredNear_ = camera->GetNearClip();
    studioStoredFar_ = camera->GetFarClip();
    studioStoredFollow_ = camera->GetFollowTarget();
    studioStoredFollowMode_ = static_cast<int>(camera->GetFollowMode());
    studioStoredMode_ = static_cast<int>(CameraEditor::GetInstance()->GetMode());
    studioCameraSaved_ = true;
    studioScene_ = sceneManager_->GetCurrentScene();
    studioEnabled_ = true;
    CreateStudioObjects();
    if (!FindStudioObject(0) || !FindStudioObject(0)->GetModel() || !FindStudioObject(1) ||
        !FindStudioObject(1)->GetModel()) {
        EndStudio();
        fileOperationStatus_ = "仮モデルを読み込めませんでした。モデルアセットを確認してください。";
        return false;
    }
    player_.SetSequence(&sequence_);
    PlaceStudioCamera();
    fileOperationStatus_ = "制作空間を開きました。中央の映像と下側の時間軸で確認できます。";
    return true;
}

void GhostDirector::PlaceStudioCamera() {
    if (!studioEnabled_)
        return;
    player_.Stop(true);
    currentScrubTime_ = 0.0f;
    if (auto* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFollowTarget(nullptr);
        camera->SetEye(kOrigin + Vector3{-10, 7, -15});
        camera->SetTarget(kOrigin + Vector3{0, 1, 3});
        camera->SetRotation({0.284f, 0.507f, 0});
        camera->SetFovY(0.65f);
        CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
        camera->Update();
    }
}

void GhostDirector::EndStudio() {
    if (!studioEnabled_)
        return;
    const bool sameScene = sceneManager_ && sceneManager_->GetCurrentScene() == studioScene_;
    player_.Stop(sameScene);
    if (sameScene) {
        // カメラカットの復帰Blend中に仮Objectを破棄しない。
        if (auto* camera = CameraManager::GetInstance()->GetMainCamera())
            camera->EndOverride(0.0f);
        if (CameraEditor::GetInstance()->GetSelectedCameraObject() == FindStudioObject(2))
            CameraEditor::GetInstance()->SetSelectedCameraObject(nullptr);
        for (int index = 0; index < 4; ++index) {
            if (auto* object = FindStudioObject(index)) {
                object->SetIsVisible(false);
                // 削除キューの処理前に再入場しても同名の古いObjectを拾わない。
                object->SetName("__Editor_Cinematic_Retired");
                studioScene_->RequestRemoveObject(object);
            }
        }
        if (studioCameraSaved_) {
            if (auto* camera = CameraManager::GetInstance()->GetMainCamera()) {
                Object3d* follow = nullptr;
                for (const auto& object : studioScene_->GetObjects())
                    if (object.get() == studioStoredFollow_)
                        follow = object.get();
                camera->SetFollowTarget(follow);
                camera->SetFollowMode(static_cast<Camera::FollowMode>(studioStoredFollowMode_));
                camera->SetEye(studioStoredEye_);
                camera->SetTarget(studioStoredTarget_);
                camera->SetRotation(studioStoredRotation_);
                camera->SetFovY(studioStoredFov_);
                camera->SetClipRange(studioStoredNear_, studioStoredFar_);
                CameraEditor::GetInstance()->SetMode(static_cast<CameraEditor::Mode>(studioStoredMode_));
                camera->Update();
            }
        }
    }
    studioEnabled_ = false;
    studioCameraSaved_ = false;
    studioScene_ = nullptr;
    currentScrubTime_ = 0;
    player_.SetSequence(&sequence_);
}

void GhostDirector::DrawStudioControls() {
#ifdef USE_IMGUI
    ImGui::TextWrapped(
        "1. 制作空間を開く → 2. 動作・VFX・カメラを組み合わせる → 3. 保存 → 4. 実ステージへ役割を割り当てて確認");
    if (ImGui::Button(studioEnabled_ ? "制作空間を終了・元の画面へ" : "制作空間を開く（仮モデルを自動準備）")) {
        if (studioEnabled_)
            EndStudio();
        else
            BeginStudio();
    }
    if (studioEnabled_) {
        ImGui::TextColored({0.4f, 0.9f, 0.7f, 1}, "制作空間 ON / 実ステージのObjectは操作しません");
        if (ImGui::Button("全体を見渡す（停止）"))
            PlaceStudioCamera();
        for (int index = 0; index < 2; ++index) {
            ImGui::PushID(index);
            if (ImGui::BeginCombo(kLabels[index], sequence_.previewModels[index].c_str())) {
                for (const auto& model : ModelManager::GetInstance()->GetAvailableModelNames()) {
                    if (ImGui::Selectable(model.c_str(), model == sequence_.previewModels[index])) {
                        player_.Stop(true);
                        if (auto* object = FindStudioObject(index)) {
                            object->SetModel(model);
                            sequence_.previewModels[index] = model;
                            RefreshPlayer(false);
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::TextWrapped("仮モデルはシーケンスと一緒に保存します。対象・基準・カメラの基本構成で、実ステージのモデル"
                           "は変更しません。");
        if (ImGui::TreeNode("サンプル（任意）")) {
            if (ImGui::Button("入口サンプルを新規作成"))
                ImGui::OpenPopup("入口サンプルを作成");
            if (ImGui::BeginPopupModal("入口サンプルを作成", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("現在の未保存シーケンスを置き換えます。シーンや保存済みファイルは変更しません。");
                if (ImGui::Button("置き換えて作成")) {
                    player_.Stop(true);
                    sequence_.Clear();
                    sequence_.previewModels[1] = "Gimmicks/portal_gate";
                    if (auto* actor = FindStudioObject(0))
                        actor->SetModel(sequence_.previewModels[0]);
                    if (auto* reference = FindStudioObject(1))
                        reference->SetModel(sequence_.previewModels[1]);
                    sequence_.name = "entry_studio";
                    strcpy_s(scenarioNameBuf_, "entry_studio");
                    sequence_.anchor = {"$Gate", -1};
                    templateActor_ = {"$Actor", -1};
                    templateCamera_ = {"$Camera", -1};
                    CinematicTransformTrack actor, camera;
                    actor.name = "登場キャラの移動";
                    actor.binding = templateActor_;
                    actor.anchorRelative = true;
                    camera.name = "演出カメラの移動";
                    camera.binding = templateCamera_;
                    camera.anchorRelative = true;
                    const Vector3 positions[] = {{0, 0, 0}, {0, 2, 2.3f}, {0, 0, 5}, {0, 0, 5}};
                    const float times[] = {0, 0.9f, 1.8f, 2.7f};
                    for (int index = 0; index < 4; ++index) {
                        actor.keys.push_back({times[index], positions[index], {}, {1.7f, 1.7f, 1.7f}, 4});
                        const Vector3 eye{-6.0f + index * 0.6f, 4, -10.0f + index};
                        const Vector3 direction = positions[index] + Vector3{0, 1, 0} - eye;
                        Vector3 rotation{
                            -std::atan2(direction.y, std::sqrt(direction.x * direction.x + direction.z * direction.z)),
                            std::atan2(direction.x, direction.z), 0};
                        camera.keys.push_back({times[index], eye, rotation, {1, 1, 1}, 4});
                    }
                    sequence_.transformTracks = {actor, camera};
                    CinematicCameraShot shot;
                    shot.name = "入口を映す";
                    shot.binding = templateCamera_;
                    shot.duration = 2.7f;
                    shot.blendInDuration = shot.blendOutDuration = 0;
                    sequence_.cameraShots.push_back(shot);
                    CinematicAnimationClipData motion;
                    motion.name = "跳び出しの身体動作";
                    motion.binding = templateActor_;
                    motion.driver = "Body";
                    motion.clipName = "player_jump";
                    motion.duration = 1.8f;
                    motion.playbackSpeed = 1.0f / 1.8f;
                    sequence_.animationClips.push_back(motion);
                    sequence_.Sort();
                    currentScrubTime_ = 0;
                    selectedTrackKind_ = 0;
                    selectedTrackIndex_ = 0;
                    selectedKeyIndex_ = 0;
                    RefreshPlayer(false);
                    fileOperationStatus_ =
                        "入口サンプルを作成しました。再生して確認できます。まだファイルには保存していません。";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::TreePop();
        }
        ImGui::TextWrapped(
            "実Object名のトラックは制作空間では再生しません。実シーンを使う場合は制作空間を終了してください。");
    } else if (ImGui::TreeNode("実ステージで確認するための役割割り当て")) {
        for (int index = 0; index < 3; ++index) {
            auto& binding = sequence_.roleBindings[index];
            ImGui::PushID(index);
            if (ImGui::BeginCombo(kLabels[index], binding.targetName.empty() ? "未設定" : binding.targetName.c_str())) {
                if (ImGui::Selectable("未設定に戻す")) {
                    player_.Stop(true);
                    binding = {};
                    RefreshPlayer(false);
                }
                if (sceneManager_ && sceneManager_->GetCurrentScene()) {
                    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
                        if (!object || object->IsEditorInternal() || (index == 2 && !object->IsCameraObject()))
                            continue;
                        if (ImGui::Selectable(object->GetName().c_str(), binding.targetName == object->GetName())) {
                            player_.Stop(true);
                            binding = {object->GetName(), object->GetEventID()};
                            RefreshPlayer(false);
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::TextWrapped("これはエディター上の確認用割り当てです。ゲームの入口処理には接続しません。");
        ImGui::TreePop();
    }
    if (!fileOperationStatus_.empty())
        ImGui::TextWrapped("%s", fileOperationStatus_.c_str());
#endif
}

void GhostDirector::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::SeparatorText("演出制作");
    ImGui::TextWrapped("中央：映像 / 下：再生とトラック / 右：選択項目の編集");
    if (ImGui::CollapsingHeader("プレビュー空間と対象", ImGuiTreeNodeFlags_DefaultOpen))
        DrawStudioControls();
    DrawInspectorContents();
    if (ImGui::CollapsingHeader("設定チェック"))
        DrawAuthoringGuide();
    if (ImGui::CollapsingHeader("補助：基準点・入口テンプレート"))
        DrawAnchorTemplateEditor();
#endif
}

void GhostDirector::DrawTimelinePanel() {
#ifdef USE_IMGUI
    // 映像は中央のGame Viewだけを使用し、時間軸は既存の下側ドックへ配置する。
    if (ImGui::Begin("演出制作 - Timeline", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted("演出制作   |   トラックを選択 → 右側で編集   /   時間軸をクリックして確認");
        DrawTransportControls();
        ImGui::SetNextItemWidth(150);
        ImGui::SliderFloat("時間軸の倍率", &timelinePixelsPerSecond_, 40, 240, "%.0f px/s");
        DrawTimelineCanvas();
    }
    ImGui::End();
#endif
}
