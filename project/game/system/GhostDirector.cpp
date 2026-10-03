#define NOMINMAX
#include "GhostDirector.h"

#include "AnimationInterpolation.h"
#include "BaseScene.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "EditorManager.h"
#include "EditorCommandRegistry.h"
#include "VFXSequencerEditor.h"
#include "GameAudioSettings.h"
#include "GhostRecorder.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
constexpr const char* kScenarioDirectory = "Resources/json/scenario/";
constexpr const char* kAnimationDirectory = "Resources/json/animation/";
constexpr const char* kVFXDirectory = "Resources/json/vfx_sequence/";

std::vector<std::string> CollectJsonStems(const std::string& directory) {
    std::vector<std::string> names;
    if (!fs::exists(directory)) {
        return names;
    }
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void AddDebugLog(const std::string& text) {
    if (DebugConsole::GetInstance()) {
        DebugConsole::GetInstance()->AddLog(text);
    }
}

std::string BuildTrackName(Object3d* object) {
    if (!object || object->GetName().empty()) {
        return "Transform Track";
    }
    return object->GetName();
}

Vector3 SubtractVector3(const Vector3& a, const Vector3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

Vector3 AddVector3(const Vector3& a, const Vector3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

Vector3 DivideScale(const Vector3& value, const Vector3& base) {
    constexpr float epsilon = 0.00001f;
    return {
        std::abs(base.x) > epsilon ? value.x / base.x : 1.0f,
        std::abs(base.y) > epsilon ? value.y / base.y : 1.0f,
        std::abs(base.z) > epsilon ? value.z / base.z : 1.0f
    };
}

Vector3 MultiplyScale(const Vector3& value, const Vector3& base) {
    return { value.x * base.x, value.y * base.y, value.z * base.z };
}

AnimationInterpolation::EasingType ToPreviewEasingType(int value) {
    return static_cast<AnimationInterpolation::EasingType>(std::clamp(value, 0, 4));
}

CinematicTransformKey EvaluateInlineTransform(const CinematicTransformTrack& track, float localTime) {
    if (track.keys.empty()) {
        return {};
    }
    if (localTime <= track.keys.front().time) {
        return track.keys.front();
    }
    if (localTime >= track.keys.back().time) {
        return track.keys.back();
    }

    for (size_t index = 0; index + 1 < track.keys.size(); ++index) {
        const CinematicTransformKey& from = track.keys[index];
        const CinematicTransformKey& to = track.keys[index + 1];
        if (localTime < from.time || localTime > to.time) {
            continue;
        }
        const float rate = AnimationInterpolation::SegmentRate(localTime, from.time, to.time);
        const float eased = AnimationInterpolation::ApplyEasing(rate, ToPreviewEasingType(from.easingToNext));
        CinematicTransformKey result;
        result.time = localTime;
        result.position = AnimationInterpolation::Lerp(from.position, to.position, eased);
        result.rotation = AnimationInterpolation::SlerpEuler(from.rotation, to.rotation, eased);
        result.scale = AnimationInterpolation::Lerp(from.scale, to.scale, eased);
        result.easingToNext = from.easingToNext;
        return result;
    }
    return track.keys.back();
}
}

void GhostDirector::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    sequence_.Clear();
    sequence_.name = scenarioNameBuf_;
    player_.Initialize(sceneManager_);
    player_.SetBindingResolver([this](const CinematicObjectBinding& binding) { return ResolveTrackTarget(binding); });
    player_.SetSequence(&sequence_);
    currentScrubTime_ = 0.0f;
    selectedTrackKind_ = 0;
    selectedTrackIndex_ = -1;
    selectedKeyIndex_ = -1;
}

void GhostDirector::Update(float deltaTime) {
    if (studioEnabled_ && (!sceneManager_ || sceneManager_->IsPlaying() ||
        sceneManager_->GetCurrentScene() != studioScene_)) EndStudio();
    const bool wasPlaying = player_.IsPlaying();
    player_.Update(deltaTime);
    if (wasPlaying || player_.IsPlaying()) {
        currentScrubTime_ = player_.GetCurrentTime();
    }
}

void GhostDirector::DrawInspectorContents() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("素材とタイミングを編集");

    if (ImGui::CollapsingHeader(ICON_FA_SAVE " シーケンス", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto scenarioFiles = CollectJsonStems(kScenarioDirectory);
        if (ImGui::BeginCombo("既存シーケンス", scenarioNameBuf_)) {
            for (const auto& fileName : scenarioFiles) {
                const bool selected = fileName == scenarioNameBuf_;
                if (ImGui::Selectable(fileName.c_str(), selected)) {
                    strncpy_s(scenarioNameBuf_, sizeof(scenarioNameBuf_), fileName.c_str(), _TRUNCATE);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("ファイル名", scenarioNameBuf_, sizeof(scenarioNameBuf_));
        if (ImGui::Button(ICON_FA_DOWNLOAD " 保存")) {
            SaveScenario(scenarioNameBuf_);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " 読込")) {
            ImGui::OpenPopup("演出シーケンスの読込確認");
        }
        ImGui::TextWrapped("一覧の選択だけでは読み込みません。編集内容は保存ボタンで保存してください。");
        if (!fileOperationStatus_.empty()) ImGui::TextWrapped("直近の操作: %s", fileOperationStatus_.c_str());
        if (ImGui::BeginPopupModal("演出シーケンスの読込確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("読込先: %s", scenarioNameBuf_);
            ImGui::TextUnformatted("未保存の編集を置き換えます。必要ならキャンセルして先に保存してください。");
            if (ImGui::Button("置き換えて読み込む")) {
                LoadScenario(scenarioNameBuf_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    const char* trackTypes[] = { "移動", "VFX", "カメラ", "身体・動作", "音", "Signal（上級）" };
    ImGui::Combo("編集する種類", &selectedTrackKind_, trackTypes, IM_ARRAYSIZE(trackTypes));

    if (selectedTrackKind_ == 0) {
        if (studioEnabled_) {
            if (ImGui::Button("演出対象の移動を追加")) AddTransformTrack(FindStudioObject(0));
            if (ImGui::Button("基準オブジェクトの移動を追加")) AddTransformTrack(FindStudioObject(1));
            if (ImGui::Button("演出カメラの移動を追加")) AddTransformTrack(FindStudioObject(2));
        } else if (ImGui::Button(ICON_FA_PLUS_CIRCLE " 選択Objectをトラックへ追加", ImVec2(-1.0f, 0.0f))) {
            AddSelectedObjectsAsTracks();
        }
        DrawTransformTrackEditor();
    }

    if (selectedTrackKind_ == 1) {
        DrawVFXTrackEditor();
    }

    if (selectedTrackKind_ == 2) {
        DrawCameraShotEditor();
    }

    if (selectedTrackKind_ == 3) {
        DrawAnimationClipEditor();
    }

    if (selectedTrackKind_ == 4) {
        DrawAudioClipEditor();
    }

    if (selectedTrackKind_ == 5) {
        DrawSignalEditor();
    }

#endif
}

void GhostDirector::DrawAuthoringGuide() {
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("制作の流れ・接続チェック", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("1. シーン上の対象を各トラックへ割り当てる → 2. 開始時刻・長さを編集 → 3. 大きいタイムラインで再生 → 4. 停止・姿勢復元して保存");
        ImGui::TextWrapped("VFXシーケンサーは演出素材の組み合わせ、ここはキャラクター・カメラ・音を合わせる全体演出です。");
        ImGui::Text("移動 %d / カメラ %d / 動作 %d / VFX %d / 音 %d",
            static_cast<int>(sequence_.transformTracks.size()), static_cast<int>(sequence_.cameraShots.size()),
            static_cast<int>(sequence_.animationClips.size()), static_cast<int>(sequence_.vfxTracks.size()),
            static_cast<int>(sequence_.audioClips.size()));
        ImGui::TextWrapped("相対Transformは対象自身の開始姿勢基準、共通ゲート基準はゲートの位置・水平角度基準です。テンプレートはゲームの入口処理には適用されません。");
        if (ImGui::TreeNode("設定チェック（再生結果の保証ではありません）")) {
            int warnings = 0;
            auto warn = [&](const std::string& text) {
                ++warnings;
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "!");
                ImGui::SameLine();
                ImGui::TextWrapped("%s", text.c_str());
            };
            auto checkTarget = [&](const std::string& name, const CinematicObjectBinding& binding, bool camera) {
                Object3d* target = ResolveTrackTarget(binding);
                if (!target) warn(name + ": 対象が見つかりません。対象リストから割り当ててください。");
                else if (camera && !target->IsCameraObject()) warn(name + ": 対象がカメラではありません。");
            };
            for (const auto& track : sequence_.transformTracks) {
                if (!track.enabled || track.muted) continue;
                checkTarget(track.name, track.binding, false);
                if (track.anchorRelative) {
                    auto* origin = ResolveTrackTarget(sequence_.anchor);
                    auto* target = ResolveTrackTarget(track.binding);
                    if (!origin || origin->GetParent()) warn(track.name + ": 共通ゲートが未設定、または親付きObjectです。");
                    if (target && target->GetParent()) warn(track.name + ": ゲート基準の対象は親を持たないObjectにしてください。");
                    if (origin && FindTransformTrack(origin) >= 0) warn(track.name + ": 共通ゲート自身に移動トラックがあります。基準は再生開始時に固定されます。");
                }
                if (track.keys.empty() && track.legacyPathFile.empty()) warn(track.name + ": 移動キーがありません。");
            }
            for (const auto& shot : sequence_.cameraShots) {
                if (shot.enabled && !shot.muted) checkTarget(shot.name, shot.binding, true);
            }
            for (const auto& clip : sequence_.animationClips) {
                if (!clip.enabled || clip.muted) continue;
                checkTarget(clip.name, clip.binding, false);
                if (clip.clipName.empty()) warn(clip.name + ": Animation / State が未設定です。");
                if (clip.playbackSpeed <= 0.0f) warn(clip.name + ": 再生速度が0です。動きません。");
                if (!clip.driver.empty() && clip.driver != "Animator" && clip.driver != "Model" && clip.driver != "Skeletal" && clip.driver != "Body")
                    warn(clip.name + ": 専用Driverはゲーム側の接続が必要です。このエディターではCallback未接続です。");
                std::error_code error;
                if (clip.driver == "Body") {
                    BodyAnimationClip body;
                    if (!body.Load(BodyAnimationClip::ResolveAssetPath(clip.clipName)))
                        warn(clip.name + ": 身体変形クリップを読み込めません。ファイルとキーを確認してください。");
                }
                if (clip.driver != "Body" && !clip.clipName.empty() && fs::is_regular_file(
                    fs::path("Resources/json/animation_clip") / (clip.clipName + ".json"), error))
                    warn(clip.name + ": 身体変形クリップと同名です。通常のAnimator Stateとは別物で、名前指定だけでは再生できません。");
            }
            for (const auto& track : sequence_.vfxTracks) {
                if (!track.enabled || track.muted) continue;
                if (!track.binding.targetName.empty() || track.binding.targetEventId >= 0)
                    checkTarget(track.name, track.binding, false);
                std::error_code error;
                if (track.sequenceName.empty() || !fs::is_regular_file(
                    fs::path(kVFXDirectory) / (track.sequenceName + ".json"), error))
                    warn(track.name + ": VFXシーケンスが見つかりません。");
            }
            for (const auto& clip : sequence_.audioClips) {
                if (clip.enabled && !clip.muted && clip.audioId.empty()) warn(clip.name + ": 音声IDが未設定です。");
            }
            if (!sequence_.signals.empty()) warn("Signal: このエディターにはゲーム固有のSignal処理を接続していません。発火時刻の編集用です。");
            if (warnings == 0) ImGui::TextWrapped("対象・必須項目の簡易チェックで警告はありません。実際の動き・カメラ・音は再生確認してください。");
            ImGui::TreePop();
        }
    }
#endif
}

void GhostDirector::DrawAnchorTemplateEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("ゲート基準・入口テンプレート（編集用）")) return;
    ImGui::TextWrapped("編集モードで使用してください。共通ゲートを差し替えると、基準付き移動・カメラの配置をまとめて変更できます。既存のゲーム入口処理・シーン設定は変更しません。");
    bool changed = DrawObjectBindingEditor("共通ゲート / 原点", sequence_.anchor, false, false);
    changed |= ImGui::SliderAngle("進行方向の補正", &sequence_.anchorYawOffset, -180.0f, 180.0f);
    if (changed) RefreshPlayer(true);
    DrawObjectBindingEditor("登場する対象", templateActor_, false, false);
    DrawObjectBindingEditor("プレビュー用Camera Object", templateCamera_, true, false);
    ImGui::DragFloat("演出の長さ", &templateDuration_, 0.05f, 0.5f, 30.0f, "%.2f sec", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("ゲートから進む距離", &templateDistance_, 0.1f, 0.1f, 50.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("跳び出す高さ", &templateHeight_, 0.1f, 0.0f, 20.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
    auto* anchor = ResolveTrackTarget(sequence_.anchor);
    auto* actor = ResolveTrackTarget(templateActor_);
    auto* camera = ResolveTrackTarget(templateCamera_);
    std::string problem;
    if (!anchor || !actor || !camera) problem = "ゲート・登場対象・カメラを選択してください。";
    else if (anchor == actor || anchor == camera || actor == camera) problem = "3つの対象は別々に指定してください。";
    else if (anchor->GetParent() || actor->GetParent() || camera->GetParent()) problem = "このテンプレートは親を持たないObject用です。";
    else if (!camera->IsCameraObject()) problem = "カメラObjectを選択してください。";
    else if (FindTransformTrack(actor) >= 0 || FindTransformTrack(camera) >= 0 || FindTransformTrack(anchor) >= 0)
        problem = "対象に既存の移動トラックがあります。上書きを避けるため、別の対象または新規シナリオを使用してください。";
    if (problem.empty()) {
        const auto& settings = camera->GetSceneCameraSettings();
        if (!settings.enabled || settings.eyeSource != SceneCameraEyeSource::kCameraObject ||
            settings.targetMode != SceneCameraTargetMode::kCameraForward)
            problem = "カメラは有効・Eye=Camera Object・Target=Camera Forwardの専用Objectを選んでください。既存設定は自動変更しません。";
        const auto nonzero = [](const Vector3& v) { return std::abs(v.x) + std::abs(v.y) + std::abs(v.z) > 0.0001f; };
        if (nonzero(settings.eyeOffset) || nonzero(settings.targetOffset) ||
            settings.eyeFollowMode != SceneCameraFollowMode::kSnap || settings.targetFollowMode != SceneCameraFollowMode::kSnap)
            problem = "再現性のため、専用カメラのEye/Target Offsetは0、追従は完全追従にしてください。設定は自動変更しません。";
        for (const auto& shot : sequence_.cameraShots) {
            if (shot.enabled && !shot.muted) problem = "既存のカメラカットがあります。新規シナリオで作成してください。";
        }
    }
    if (!problem.empty()) ImGui::TextWrapped("%s", problem.c_str());
    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button("入口の移動＋カメラ雛形を追加", ImVec2(-1.0f, 0.0f))) {
        // プレビュー中の姿勢を雛形のスケールとして取り込まない。
        player_.Stop(true);
        CinematicTransformTrack movement, cameraMove;
        movement.name = "入口・登場対象";
        movement.binding = templateActor_;
        movement.anchorRelative = true;
        cameraMove.name = "入口・カメラ";
        cameraMove.binding = templateCamera_;
        cameraMove.anchorRelative = true;
        for (int index = 0; index < 4; ++index) {
            const float rates[] = { 0.0f, 0.35f, 0.70f, 1.0f };
            const float progress[] = { 0.0f, 0.45f, 1.0f, 1.0f };
            CinematicTransformKey key;
            key.time = templateDuration_ * rates[index];
            key.position = { 0.0f, index == 1 ? templateHeight_ : 0.0f, templateDistance_ * progress[index] };
            key.scale = actor->GetScale();
            movement.keys.push_back(key);
            CinematicTransformKey view;
            view.time = key.time;
            view.position = { -6.0f + rates[index] * 2.0f, 4.0f, -10.0f + rates[index] * 3.0f };
            const Vector3 focus = key.position + Vector3{ 0.0f, 1.0f, 0.0f };
            const Vector3 direction = focus - view.position;
            view.rotation = { -std::atan2(direction.y, std::sqrt(direction.x * direction.x + direction.z * direction.z)),
                std::atan2(direction.x, direction.z), 0.0f };
            view.scale = camera->GetScale();
            cameraMove.keys.push_back(view);
        }
        sequence_.transformTracks.push_back(movement);
        sequence_.transformTracks.push_back(cameraMove);
        CinematicCameraShot shot;
        shot.name = "入口・プレビューカメラ";
        shot.binding = templateCamera_;
        shot.duration = templateDuration_;
        shot.blendInDuration = 0.0f;
        shot.blendOutDuration = 0.0f;
        sequence_.cameraShots.push_back(shot);
        sequence_.Sort();
        selectedTrackKind_ = 0;
        selectedTrackIndex_ = static_cast<int>(sequence_.transformTracks.size()) - 2;
        selectedKeyIndex_ = 0;
        currentScrubTime_ = 0.0f;
        RefreshPlayer(false);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("これは入口演出を制作するための雛形です。ゲームの既存演出の完全複製ではありません。追加後は移動キー・身体クリップ・VFX・音をタイムラインで調整し、停止で姿勢を戻してシナリオ保存してください。基準は再生開始時に固定し、ゲートのスケールや傾きは継承しません。");
#endif
}

void GhostDirector::PlayScenario(bool isLoop, bool) {
    sequence_.Sort();
    player_.SetSequence(&sequence_);
    isLooping_ = isLoop;
    player_.Play(isLooping_);
    currentScrubTime_ = 0.0f;
    AddDebugLog("Cinematic Director: Play [" + sequence_.name + "]");
}

void GhostDirector::StopScenario() {
    player_.Stop(true);
    currentScrubTime_ = 0.0f;
    AddDebugLog("Cinematic Director: Stop");
}

void GhostDirector::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size) {
#ifdef USE_IMGUI
    if (!showSelectedTrackPreview_ || !sceneManager_ || !sceneManager_->GetCurrentScene() ||
        selectedTrackKind_ != 0 || selectedTrackIndex_ < 0 ||
        selectedTrackIndex_ >= static_cast<int>(sequence_.transformTracks.size())) {
        return;
    }

    const CinematicTransformTrack& track = sequence_.transformTracks[selectedTrackIndex_];
    Object3d* target = ResolveTrackTarget(track.binding);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 clipMin(offset.x, offset.y);
    const ImVec2 clipMax(offset.x + size.x, offset.y + size.y);

    auto DrawHeader = [&](const char* detail, ImU32 accentColor) {
        const float panelWidth = std::min(std::max(size.x - 20.0f, 180.0f), 520.0f);
        const ImVec2 panelMin(offset.x + 10.0f, offset.y + 10.0f);
        const ImVec2 panelMax(panelMin.x + panelWidth, panelMin.y + 48.0f);
        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(18, 22, 30, 215), 5.0f);
        drawList->AddRectFilled(panelMin, ImVec2(panelMin.x + 4.0f, panelMax.y), accentColor, 5.0f);
        std::string title = "Selected Track: " + track.name;
        if (track.muted) {
            title += " [MUTE]";
        } else if (!track.enabled) {
            title += " [DISABLED]";
        }
        drawList->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 7.0f), IM_COL32(235, 242, 255, 255), title.c_str());
        drawList->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 27.0f), IM_COL32(175, 188, 210, 255), detail);
    };

    if (!target) {
        drawList->PushClipRect(clipMin, clipMax, true);
        DrawHeader("Target Objectが現在シーンに見つかりません", IM_COL32(235, 80, 80, 255));
        drawList->PopClipRect();
        return;
    }

    // Inline Keyが無い場合だけ、選択中のLegacy PathをGhost Recorderの表示へ渡します。
    if (track.keys.empty()) {
        if (!track.legacyPathFile.empty() && target->recorder_) {
            target->recorder_->SetSceneManager(sceneManager_);
            if (target->recorder_->GetTarget() != target) {
                target->recorder_->SetTarget(target);
            }
            target->recorder_->DrawPreview(viewProjection, offset, size, true);
            drawList->PushClipRect(clipMin, clipMax, true);
            const std::string detail = "Legacy Ghost Path: " + track.legacyPathFile;
            DrawHeader(detail.c_str(), IM_COL32(80, 190, 235, 255));
            drawList->PopClipRect();
        } else {
            drawList->PushClipRect(clipMin, clipMax, true);
            DrawHeader("移動キーがありません。「現在姿勢をキー登録」で追加できます", IM_COL32(235, 165, 70, 255));
            drawList->PopClipRect();
        }
        return;
    }

    Vector3 basePosition = target->GetTranslate();
    Vector3 baseRotation = target->GetRotation();
    Vector3 baseScale = target->GetScale();
    player_.GetBasePose(target, basePosition, baseRotation, baseScale);

    auto ResolveLocalPose = [&](const CinematicTransformKey& key) {
        CinematicTransformKey pose = key;
        if (track.anchorRelative) {
            Vector3 origin;
            float yaw = 0.0f;
            if (player_.GetAnchorPose(origin, yaw)) {
                pose.position = CinematicAnchorPosition(key.position, origin, yaw);
                pose.rotation.y += yaw;
            }
        } else if (track.relative) {
            pose.position = AddVector3(basePosition, key.position);
            pose.rotation = AddVector3(baseRotation, key.rotation);
            pose.scale = MultiplyScale(baseScale, key.scale);
        }
        return pose;
    };

    auto MakePoseWorldMatrix = [&](const CinematicTransformKey& localPose) {
        Matrix4x4 world = Math::MakeAffineMatrix(localPose.scale, localPose.rotation, localPose.position);
        if (target->GetParent()) {
            world = Math::Multiply(world, target->GetParent()->GetWorldMatrix());
        }
        return world;
    };

    auto LocalPositionToWorld = [&](const Vector3& localPosition) {
        if (!target->GetParent()) {
            return localPosition;
        }
        return Math::Transform(localPosition, target->GetParent()->GetWorldMatrix());
    };

    auto WorldToScreen = [&](const Vector3& worldPosition, ImVec2& screenPosition) {
        const Vector3 ndc = Math::Transform(worldPosition, viewProjection);
        if (ndc.z < 0.0f || ndc.z > 1.0f) {
            return false;
        }
        screenPosition.x = offset.x + (ndc.x + 1.0f) * 0.5f * size.x;
        screenPosition.y = offset.y + (1.0f - ndc.y) * 0.5f * size.y;
        return true;
    };

    auto DrawArrowHead = [&](const ImVec2& from, const ImVec2& to, ImU32 color) {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1.0f) {
            return;
        }
        const float dirX = dx / length;
        const float dirY = dy / length;
        const float sideX = -dirY;
        const float sideY = dirX;
        const ImVec2 back(to.x - dirX * 10.0f, to.y - dirY * 10.0f);
        drawList->AddTriangleFilled(
            to,
            ImVec2(back.x + sideX * 4.5f, back.y + sideY * 4.5f),
            ImVec2(back.x - sideX * 4.5f, back.y - sideY * 4.5f),
            color);
    };

    auto DrawWirePose = [&](const CinematicTransformKey& localPose, ImU32 color, float thickness) {
        const Matrix4x4 world = MakePoseWorldMatrix(localPose);
        const Vector3 corners[8] = {
            {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
            {-0.5f,  0.5f, -0.5f}, {0.5f,  0.5f, -0.5f},
            {-0.5f, -0.5f,  0.5f}, {0.5f, -0.5f,  0.5f},
            {-0.5f,  0.5f,  0.5f}, {0.5f,  0.5f,  0.5f},
        };
        const int edges[12][2] = {
            {0, 1}, {1, 3}, {3, 2}, {2, 0},
            {4, 5}, {5, 7}, {7, 6}, {6, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        ImVec2 screenCorners[8]{};
        bool visible[8]{};
        for (int index = 0; index < 8; ++index) {
            visible[index] = WorldToScreen(Math::Transform(corners[index], world), screenCorners[index]);
        }
        for (const auto& edge : edges) {
            if (visible[edge[0]] && visible[edge[1]]) {
                drawList->AddLine(screenCorners[edge[0]], screenCorners[edge[1]], color, thickness);
            }
        }
    };

    auto DrawForwardGuide = [&](const CinematicTransformKey& localPose, ImU32 color) {
        const Matrix4x4 world = MakePoseWorldMatrix(localPose);
        const Vector3 origin = Math::Transform({0.0f, 0.0f, 0.0f}, world);
        Vector3 forward = Math::TransformNormal({0.0f, 0.0f, 1.0f}, world);
        const float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
        if (length <= 0.0001f) {
            return;
        }
        forward = {forward.x / length, forward.y / length, forward.z / length};
        const Vector3 tip = {
            origin.x + forward.x * 1.8f,
            origin.y + forward.y * 1.8f,
            origin.z + forward.z * 1.8f,
        };
        ImVec2 originScreen{};
        ImVec2 tipScreen{};
        if (WorldToScreen(origin, originScreen) && WorldToScreen(tip, tipScreen)) {
            drawList->AddLine(originScreen, tipScreen, color, 2.0f);
            DrawArrowHead(originScreen, tipScreen, color);
        }
    };

    struct PreviewSample {
        Vector3 worldPosition{};
        ImVec2 screenPosition{};
        bool visible = false;
    };

    const float firstTime = track.keys.front().time;
    const float lastTime = track.keys.back().time;
    const float pathDuration = std::max(lastTime - firstTime, 0.0f);
    const int sampleCount = pathDuration > 0.0f
        ? std::clamp(static_cast<int>((track.keys.size() - 1) * 28), 28, 224)
        : 1;
    std::vector<PreviewSample> samples;
    samples.reserve(sampleCount + 1);
    for (int index = 0; index <= sampleCount; ++index) {
        const float rate = sampleCount > 0 ? static_cast<float>(index) / static_cast<float>(sampleCount) : 0.0f;
        const float localTime = firstTime + pathDuration * rate;
        const CinematicTransformKey pose = ResolveLocalPose(EvaluateInlineTransform(track, localTime));
        PreviewSample sample;
        sample.worldPosition = LocalPositionToWorld(pose.position);
        sample.visible = WorldToScreen(sample.worldPosition, sample.screenPosition);
        samples.push_back(sample);
    }

    drawList->PushClipRect(clipMin, clipMax, true);
    const ImU32 pathColor = track.muted || !track.enabled
        ? IM_COL32(150, 155, 168, 110)
        : IM_COL32(235, 242, 255, 175);
    const ImU32 arrowColor = track.muted || !track.enabled
        ? IM_COL32(150, 155, 168, 150)
        : IM_COL32(70, 220, 245, 230);

    const int arrowInterval = std::max(1, sampleCount / 8);
    for (int index = 1; index < static_cast<int>(samples.size()); ++index) {
        if (!samples[index - 1].visible || !samples[index].visible) {
            continue;
        }
        drawList->AddLine(samples[index - 1].screenPosition, samples[index].screenPosition, pathColor, 2.2f);
        if (index % arrowInterval == 0) {
            DrawArrowHead(samples[index - 1].screenPosition, samples[index].screenPosition, arrowColor);
        }
    }

    // 軌道上の光点で、開始から終了へ進む方向を視覚的に示します。
    if (pathDuration > 0.0f && track.enabled && !track.muted) {
        for (int index = 0; index < 4; ++index) {
            const float rate = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.18f + index * 0.25f, 1.0f);
            const CinematicTransformKey pose =
                ResolveLocalPose(EvaluateInlineTransform(track, firstTime + pathDuration * rate));
            ImVec2 screen{};
            if (WorldToScreen(LocalPositionToWorld(pose.position), screen)) {
                drawList->AddCircleFilled(screen, 3.5f, IM_COL32(255, 235, 90, 230));
            }
        }
    }

    for (int index = 0; index < static_cast<int>(track.keys.size()); ++index) {
        const CinematicTransformKey localPose = ResolveLocalPose(track.keys[index]);
        ImVec2 screen{};
        if (!WorldToScreen(LocalPositionToWorld(localPose.position), screen)) {
            continue;
        }

        const bool isFirst = index == 0;
        const bool isLast = index == static_cast<int>(track.keys.size()) - 1;
        const bool isSelected = index == selectedKeyIndex_;
        const ImU32 markerColor = isFirst
            ? IM_COL32(70, 235, 120, 255)
            : (isLast ? IM_COL32(90, 145, 255, 255) : IM_COL32(255, 170, 65, 255));
        const ImU32 boxColor = isFirst
            ? IM_COL32(70, 235, 120, isSelected ? 210 : 115)
            : (isLast
                ? IM_COL32(90, 145, 255, isSelected ? 210 : 115)
                : IM_COL32(255, 170, 65, isSelected ? 210 : 115));

        if (showPreviewOrientation_) {
            DrawWirePose(localPose, boxColor, isSelected ? 2.2f : 1.4f);
            DrawForwardGuide(localPose, IM_COL32(70, 220, 245, isSelected ? 255 : 180));
        }
        if (isSelected) {
            drawList->AddCircle(screen, 11.0f, IM_COL32(255, 255, 255, 255), 0, 2.5f);
        }
        drawList->AddCircleFilled(screen, isSelected ? 7.0f : 5.5f, markerColor);

        char label[96]{};
        if (isFirst) {
            sprintf_s(label, "START  %.2fs", track.startTime + track.keys[index].time);
        } else if (isLast) {
            sprintf_s(label, "END  %.2fs", track.startTime + track.keys[index].time);
        } else {
            sprintf_s(label, "K%d  %.2fs", index + 1, track.startTime + track.keys[index].time);
        }
        const ImVec2 labelPosition(screen.x + 9.0f, screen.y - 18.0f);
        drawList->AddText(ImVec2(labelPosition.x + 1.0f, labelPosition.y + 1.0f), IM_COL32(0, 0, 0, 230), label);
        drawList->AddText(labelPosition, markerColor, label);
    }

    // マスター時間が選択トラック内にある時だけ、現在姿勢を黄色で表示します。
    const float localCurrentTime = currentScrubTime_ - track.startTime;
    if (localCurrentTime >= firstTime && localCurrentTime <= lastTime) {
        const CinematicTransformKey currentPose =
            ResolveLocalPose(EvaluateInlineTransform(track, localCurrentTime));
        ImVec2 currentScreen{};
        if (WorldToScreen(LocalPositionToWorld(currentPose.position), currentScreen)) {
            drawList->AddCircleFilled(currentScreen, 6.0f, IM_COL32(255, 235, 55, 255));
            drawList->AddCircle(currentScreen, 12.0f, IM_COL32(255, 255, 255, 235), 0, 2.0f);
            drawList->AddText(
                ImVec2(currentScreen.x + 10.0f, currentScreen.y + 6.0f),
                IM_COL32(255, 240, 100, 255),
                "CURRENT");
        }
    }

    const std::string detail =
        std::string(track.relative ? "Relative" : "Absolute") +
        " | Green: Start  Orange: Key  Blue: End  Yellow: Current";
    DrawHeader(detail.c_str(), IM_COL32(70, 210, 240, 255));
    drawList->PopClipRect();
#else
    (void)viewProjection;
    (void)offset;
    (void)size;
#endif
}

void GhostDirector::SaveScenario(const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }
    sequence_.name = fileName;
    sequence_.Sort();
    const std::string path = std::string(kScenarioDirectory) + fileName + ".json";
    if (sequence_.Save(path)) {
        fileOperationStatus_ = "保存完了: " + fileName;
        AddDebugLog("Cinematic Director: Saved " + path);
    } else {
        fileOperationStatus_ = "保存失敗: " + fileName;
        AddDebugLog("Cinematic Director: Save failed " + path);
    }
}

void GhostDirector::LoadScenario(const std::string& fileName) {
    const std::string path = std::string(kScenarioDirectory) + fileName + ".json";
    CinematicSequence loaded;
    if (!loaded.Load(path)) {
        fileOperationStatus_ = "読込失敗（現在の編集は保持）: " + fileName;
        AddDebugLog("Cinematic Director: Load failed " + path);
        return;
    }
    player_.Stop(true);
    const bool reopenStudio = studioEnabled_;
    if (reopenStudio) EndStudio();
    sequence_ = std::move(loaded);
    fileOperationStatus_ = "読込完了: " + fileName;
    strncpy_s(scenarioNameBuf_, sizeof(scenarioNameBuf_), fileName.c_str(), _TRUNCATE);
    player_.SetSequence(&sequence_);
    currentScrubTime_ = 0.0f;
    selectedTrackKind_ = 0;
    selectedTrackIndex_ = sequence_.transformTracks.empty() ? -1 : 0;
    selectedKeyIndex_ = -1;
    AddDebugLog("Cinematic Director: Loaded " + path);
    if (reopenStudio) BeginStudio();
}

bool GhostDirector::IsFinished() const {
    return player_.IsFinished();
}

int GhostDirector::GetActiveEventID() const {
    return player_.GetLastEventId();
}

ActiveEvent GhostDirector::GetActiveEvent() const {
    ActiveEvent result;
    result.id = player_.GetLastEventId();
    result.targetObject = player_.GetLastEventTarget();
    return result;
}

void GhostDirector::AdvanceTime(float deltaTime) {
    Update(deltaTime);
}

void GhostDirector::RecordTransformKey(Object3d* object, bool force) {
    if (!object || (!force && !autoKeyEnabled_)) {
        return;
    }
    if (!force && EditorManager::GetInstance()->GetSelectedObject() != this) {
        return;
    }

    int trackIndex = FindTransformTrack(object);
    bool addedTrack = false;
    if (trackIndex < 0) {
        trackIndex = AddTransformTrack(object);
        addedTrack = trackIndex >= 0;
    }
    if (trackIndex < 0 || trackIndex >= static_cast<int>(sequence_.transformTracks.size())) {
        return;
    }

    auto& track = sequence_.transformTracks[trackIndex];
    if (track.locked) {
        return;
    }
    CinematicTransformKey key;
    key.time = std::max(0.0f, currentScrubTime_ - track.startTime);
    key.position = object->GetTranslate();
    key.rotation = object->GetRotation();
    key.scale = object->GetScale();
    if (track.anchorRelative) {
        Vector3 origin;
        float yaw = 0.0f;
        if (object->GetParent() || !player_.GetAnchorPose(origin, yaw)) return;
        key.position = CinematicAnchorLocalPosition(key.position, origin, yaw);
        key.rotation.y -= yaw;
    } else if (track.relative) {
        Vector3 basePosition = object->GetTranslate();
        Vector3 baseRotation = object->GetRotation();
        Vector3 baseScale = object->GetScale();
        player_.GetBasePose(object, basePosition, baseRotation, baseScale);
        key.position = SubtractVector3(key.position, basePosition);
        key.rotation = SubtractVector3(key.rotation, baseRotation);
        key.scale = DivideScale(key.scale, baseScale);
    }
    key.easingToNext = 4;
    UpsertTransformKey(track, key);
    sequence_.Sort();
    selectedTrackKind_ = 0;
    selectedTrackIndex_ = trackIndex;
    for (int index = 0; index < static_cast<int>(track.keys.size()); ++index) {
        if (std::abs(track.keys[index].time - key.time) <= kKeyTimeEpsilon) {
            selectedKeyIndex_ = index;
            break;
        }
    }

    if (addedTrack) {
        RefreshPlayer(true);
    }
}

int GhostDirector::FindTransformTrack(Object3d* object) const {
    if (!object) {
        return -1;
    }
    for (int index = 0; index < static_cast<int>(sequence_.transformTracks.size()); ++index) {
        const auto& binding = sequence_.transformTracks[index].binding;
        if (GetRoleIndex(binding.targetName) >= 0 && ResolveTrackTarget(binding) == object) return index;
        if (binding.targetEventId >= 0 && binding.targetEventId == object->GetEventID()) {
            return index;
        }
        if (binding.targetName == object->GetName()) {
            return index;
        }
    }
    return -1;
}

int GhostDirector::AddTransformTrack(Object3d* object) {
    if (!object) {
        return -1;
    }
    const int existing = FindTransformTrack(object);
    if (existing >= 0) {
        return existing;
    }

    CinematicTransformTrack track;
    track.name = BuildTrackName(object);
    track.binding.targetName = object->GetName();
    track.binding.targetEventId = object->GetEventID();
    if (studioEnabled_) {
        const char* roles[] = { "$Actor", "$Gate", "$Camera" };
        for (int index = 0; index < 3; ++index)
            if (FindStudioObject(index) == object) track.binding = { roles[index], -1 };
    }
    track.startTime = std::max(0.0f, currentScrubTime_);
    CinematicTransformKey key;
    key.time = 0.0f;
    key.position = object->GetTranslate();
    key.rotation = object->GetRotation();
    key.scale = object->GetScale();
    track.keys.push_back(key);
    if (studioEnabled_) {
        sequence_.anchor = { "$Gate", -1 };
        Vector3 origin;
        float yaw = 0;
        if (player_.GetAnchorPose(origin, yaw)) {
            track.anchorRelative = true;
            track.keys[0].position = CinematicAnchorLocalPosition(key.position, origin, yaw);
            track.keys[0].rotation.y -= yaw;
        }
    }
    sequence_.transformTracks.push_back(track);
    const int index = static_cast<int>(sequence_.transformTracks.size()) - 1;
    selectedTrackKind_ = 0;
    selectedTrackIndex_ = index;
    selectedKeyIndex_ = 0;
    return index;
}

void GhostDirector::AddSelectedObjectsAsTracks() {
    if (!editor_) {
        return;
    }
    const auto selectedObjects = editor_->GetSelectedObjects();
    bool changed = false;
    for (Object3d* object : selectedObjects) {
        if (FindTransformTrack(object) < 0) {
            AddTransformTrack(object);
            changed = true;
        }
    }
    if (!changed) {
        if (Object3d* object = editor_->GetSelectedObject()) {
            if (FindTransformTrack(object) < 0) {
                AddTransformTrack(object);
                changed = true;
            }
        }
    }
    if (changed) {
        sequence_.Sort();
        RefreshPlayer(true);
    }
}

void GhostDirector::RemoveSelectedTrack() {
    if (selectedTrackKind_ == 0 && selectedTrackIndex_ >= 0 &&
        selectedTrackIndex_ < static_cast<int>(sequence_.transformTracks.size())) {
        sequence_.transformTracks.erase(sequence_.transformTracks.begin() + selectedTrackIndex_);
    } else if (selectedTrackKind_ == 1 && selectedTrackIndex_ >= 0 &&
        selectedTrackIndex_ < static_cast<int>(sequence_.vfxTracks.size())) {
        sequence_.vfxTracks.erase(sequence_.vfxTracks.begin() + selectedTrackIndex_);
    } else if (selectedTrackKind_ == 2 && selectedTrackIndex_ >= 0 &&
        selectedTrackIndex_ < static_cast<int>(sequence_.cameraShots.size())) {
        sequence_.cameraShots.erase(sequence_.cameraShots.begin() + selectedTrackIndex_);
    } else if (selectedTrackKind_ == 3 && selectedTrackIndex_ >= 0 &&
        selectedTrackIndex_ < static_cast<int>(sequence_.animationClips.size())) {
        sequence_.animationClips.erase(sequence_.animationClips.begin() + selectedTrackIndex_);
    } else if (selectedTrackKind_ == 4 && selectedTrackIndex_ >= 0 &&
        selectedTrackIndex_ < static_cast<int>(sequence_.audioClips.size())) {
        sequence_.audioClips.erase(sequence_.audioClips.begin() + selectedTrackIndex_);
    } else if (selectedTrackKind_ == 5 && selectedTrackIndex_ >= 0 &&
        selectedTrackIndex_ < static_cast<int>(sequence_.signals.size())) {
        sequence_.signals.erase(sequence_.signals.begin() + selectedTrackIndex_);
    } else {
        return;
    }
    selectedTrackIndex_ = -1;
    selectedKeyIndex_ = -1;
    RefreshPlayer(true);
}

void GhostDirector::RefreshPlayer(bool preservePreview) {
    const bool previewing = preservePreview && (player_.IsPreviewing() || player_.IsPlaying());
    const float time = currentScrubTime_;
    if (player_.IsPlaying()) {
        player_.Stop(true);
    } else if (player_.IsPreviewing()) {
        player_.EndPreview(true);
    }
    player_.SetSequence(&sequence_);
    if (previewing) {
        player_.BeginPreview();
        player_.SetTime(time, false);
    }
}

void GhostDirector::EvaluatePreviewAtCurrentTime() {
    if (player_.IsPlaying()) {
        return;
    }
    player_.BeginPreview();
    player_.SetTime(currentScrubTime_, false);
}

void GhostDirector::SelectTrackTarget(int trackIndex) {
    if (studioEnabled_) return;
    if (!editor_ || !sceneManager_ || !sceneManager_->GetCurrentScene() || trackIndex < 0 ||
        trackIndex >= static_cast<int>(sequence_.transformTracks.size())) {
        return;
    }
    if (Object3d* target = ResolveTrackTarget(sequence_.transformTracks[trackIndex].binding)) {
        editor_->SetSelectedObject(target);
    }
}

Object3d* GhostDirector::ResolveTrackTarget(const CinematicObjectBinding& binding) const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return nullptr;
    }
    const int role = GetRoleIndex(binding.targetName);
    if (studioEnabled_) return role >= 0 ? FindStudioObject(role) : nullptr;
    const auto& resolved = role >= 0 ? sequence_.roleBindings[role] : binding;
    Object3d* nameMatch = nullptr;
    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (!object) {
            continue;
        }
        if (resolved.targetEventId >= 0 && object->GetEventID() == resolved.targetEventId) {
            return object.get();
        }
        if (!resolved.targetName.empty() && object->GetName() == resolved.targetName) {
            nameMatch = object.get();
        }
    }
    return nameMatch;
}

bool GhostDirector::DrawObjectBindingEditor(
    const char* label,
    CinematicObjectBinding& binding,
    bool cameraOnly,
    bool allowWorld) {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        ImGui::TextDisabled("%s: シーンがありません", label);
        return false;
    }

    bool changed = false;
    const char* currentLabel = binding.targetName.empty() ? (allowWorld ? "World" : "(未設定)") : binding.targetName.c_str();
    const char* roleLabels[] = { "演出対象（役割）", "基準オブジェクト（役割）", "演出カメラ（役割）" };
    if (const int role = GetRoleIndex(binding.targetName); role >= 0) currentLabel = roleLabels[role];
    if (ImGui::BeginCombo(label, currentLabel)) {
        const char* roles[] = { "$Actor", "$Gate", "$Camera" };
        const char* labels[] = { "演出対象（役割）", "基準オブジェクト（役割）", "演出カメラ（役割）" };
        for (int index = 0; index < 3; ++index) {
            if (cameraOnly && index != 2) continue;
            if (ImGui::Selectable(labels[index], binding.targetName == roles[index])) {
                binding = { roles[index], -1 };
                changed = true;
            }
        }
        if (allowWorld && ImGui::Selectable("World", binding.targetName.empty())) {
            binding = {};
            changed = true;
        }
        for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (studioEnabled_) continue;
            if (!object || object->IsEditorInternal() || (cameraOnly && !object->IsCameraObject())) {
                continue;
            }
            const bool selected = object->GetName() == binding.targetName;
            if (ImGui::Selectable(object->GetName().c_str(), selected)) {
                binding.targetName = object->GetName();
                binding.targetEventId = object->GetEventID();
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
#else
    (void)label;
    (void)binding;
    (void)cameraOnly;
    (void)allowWorld;
    return false;
#endif
}

void GhostDirector::UpsertTransformKey(CinematicTransformTrack& track, const CinematicTransformKey& key) {
    for (auto& existing : track.keys) {
        if (std::abs(existing.time - key.time) <= kKeyTimeEpsilon) {
            const int easing = existing.easingToNext;
            existing = key;
            existing.easingToNext = easing;
            return;
        }
    }
    track.keys.push_back(key);
}

float GhostDirector::GetTransformTrackDuration(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(sequence_.transformTracks.size())) {
        return 0.0f;
    }
    const auto& track = sequence_.transformTracks[trackIndex];
    if (!track.keys.empty()) {
        return std::max(0.0f, track.keys.back().time);
    }
    if (track.legacyPathFile.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return 0.0f;
    }
    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == track.binding.targetName && object->recorder_) {
            return object->recorder_->GetDurationSeconds();
        }
    }
    return 0.0f;
}

float GhostDirector::GetScenarioDuration() const {
    return std::max(sequence_.GetAuthoredDuration(), player_.GetDuration());
}

void GhostDirector::DrawTransportControls() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_STREAM " 再生 / マスター時間", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::Checkbox("ループ", &isLooping_);
    ImGui::SameLine();
    ImGui::Checkbox("Auto Key", &autoKeyEnabled_);
    if (autoKeyEnabled_) ImGui::TextWrapped("自動記録 ON: 対象Objectの移動・回転・拡縮を現在時刻のキーへ記録します。");

    if (!player_.IsPlaying()) {
        if (ImGui::Button(ICON_FA_PLAY " 再生")) {
            if (player_.IsPreviewing()) {
                player_.Resume(isLooping_);
            } else {
                PlayScenario(isLooping_, false);
            }
        }
    } else if (ImGui::Button(ICON_FA_PAUSE " 一時停止")) {
        currentScrubTime_ = player_.GetCurrentTime();
        player_.Pause();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " 停止・姿勢復元")) {
        StopScenario();
    }

    const float duration = GetScenarioDuration();
    if (ImGui::SliderFloat("現在時刻", &currentScrubTime_, 0.0f, duration, "%.3f sec")) {
        EvaluatePreviewAtCurrentTime();
    }
    ImGui::Text("全体: %.3f sec / Transform %zu / Camera %zu / Animation %zu / VFX %zu / Audio %zu / Signal %zu",
        duration,
        sequence_.transformTracks.size(),
        sequence_.cameraShots.size(),
        sequence_.animationClips.size(),
        sequence_.vfxTracks.size(),
        sequence_.audioClips.size(),
        sequence_.signals.size());
#endif
}

void GhostDirector::DrawTransformTrackEditor() {
#ifdef USE_IMGUI
    ImGui::Checkbox(ICON_FA_ROUTE " 選択トラックの軌跡をGame Viewに表示", &showSelectedTrackPreview_);
    if (showSelectedTrackPreview_) {
        ImGui::SameLine();
        ImGui::Checkbox("姿勢ガイド", &showPreviewOrientation_);
    }
    ImGui::TextDisabled("表示対象は選択中のObject / Cameraトラック1本だけです。");
    ImGui::Separator();

    for (int index = 0; index < static_cast<int>(sequence_.transformTracks.size()); ++index) {
        auto& track = sequence_.transformTracks[index];
        ImGui::PushID(index);
        const bool selected = selectedTrackKind_ == 0 && selectedTrackIndex_ == index;
        std::string label = track.muted ? "[M] " + track.name : track.name;
        if (ImGui::Selectable(label.c_str(), selected)) {
            selectedTrackKind_ = 0;
            selectedTrackIndex_ = index;
            selectedKeyIndex_ = -1;
            SelectTrackTarget(index);
        }
        ImGui::PopID();
    }

    if (selectedTrackKind_ != 0 || selectedTrackIndex_ < 0 ||
        selectedTrackIndex_ >= static_cast<int>(sequence_.transformTracks.size())) {
        return;
    }

    auto& track = sequence_.transformTracks[selectedTrackIndex_];
    ImGui::SeparatorText("選択トラック");
    char trackName[128]{};
    strncpy_s(trackName, sizeof(trackName), track.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("トラック名", trackName, sizeof(trackName))) {
        track.name = trackName;
    }

    bool bindingChanged = DrawObjectBindingEditor("対象", track.binding, false, false);

    ImGui::Checkbox("有効", &track.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Mute", &track.muted);
    ImGui::SameLine();
    ImGui::Checkbox("Lock", &track.locked);
    const bool wasRelative = track.relative;
    ImGui::BeginDisabled(track.anchorRelative);
    if (ImGui::Checkbox("相対Transform", &track.relative) && wasRelative != track.relative) {
        Object3d* target = ResolveTrackTarget(track.binding);
        if (target && FindTransformTrack(target) == selectedTrackIndex_) {
            Vector3 basePosition = target->GetTranslate();
            Vector3 baseRotation = target->GetRotation();
            Vector3 baseScale = target->GetScale();
            player_.GetBasePose(target, basePosition, baseRotation, baseScale);
            for (auto& key : track.keys) {
                if (track.relative) {
                    key.position = SubtractVector3(key.position, basePosition);
                    key.rotation = SubtractVector3(key.rotation, baseRotation);
                    key.scale = DivideScale(key.scale, baseScale);
                } else {
                    key.position = AddVector3(basePosition, key.position);
                    key.rotation = AddVector3(baseRotation, key.rotation);
                    key.scale = MultiplyScale(key.scale, baseScale);
                }
            }
        }
        EvaluatePreviewAtCurrentTime();
    }
    ImGui::EndDisabled();
    if (track.anchorRelative) ImGui::TextWrapped("共通ゲート基準: Positionはゲートからの距離、Rotation Yはゲートからの角度（rad）です。");
    ImGui::SameLine();
    ImGui::Checkbox("終了姿勢を保持", &track.holdLast);
    if (ImGui::DragFloat("開始時刻", &track.startTime, 0.01f, 0.0f, 600.0f, "%.3f sec")) {
        currentScrubTime_ = std::max(currentScrubTime_, track.startTime);
        EvaluatePreviewAtCurrentTime();
    }

    const auto pathFiles = CollectJsonStems(kAnimationDirectory);
    const std::string pathLabel = track.legacyPathFile.empty() ? "Inline Keys" : track.legacyPathFile;
    if (ImGui::BeginCombo("Legacy Ghost Path", pathLabel.c_str())) {
        if (ImGui::Selectable("Inline Keys", track.legacyPathFile.empty())) {
            track.legacyPathFile.clear();
            bindingChanged = true;
        }
        for (const auto& fileName : pathFiles) {
            if (ImGui::Selectable(fileName.c_str(), track.legacyPathFile == fileName)) {
                track.legacyPathFile = fileName;
                bindingChanged = true;
            }
        }
        ImGui::EndCombo();
    }
    if (!track.legacyPathFile.empty() && !track.keys.empty()) {
        ImGui::TextDisabled("Inline KeyがあるためLegacy PathよりInline Keyを優先します。");
    }

    if (ImGui::Button(ICON_FA_KEY " 現在姿勢をキー登録", ImVec2(-1.0f, 0.0f))) {
        if (auto* target = ResolveTrackTarget(track.binding)) RecordTransformKey(target, true);
    }

    if (!track.keys.empty()) {
        if (selectedKeyIndex_ < 0 || selectedKeyIndex_ >= static_cast<int>(track.keys.size())) {
            selectedKeyIndex_ = 0;
        }
        std::string keyLabel = "Key " + std::to_string(selectedKeyIndex_ + 1) + " / " + std::to_string(track.keys.size());
        if (ImGui::BeginCombo("キー", keyLabel.c_str())) {
            for (int keyIndex = 0; keyIndex < static_cast<int>(track.keys.size()); ++keyIndex) {
                char label[96];
                sprintf_s(label, "Key %d  %.3f sec", keyIndex + 1, track.keys[keyIndex].time);
                if (ImGui::Selectable(label, selectedKeyIndex_ == keyIndex)) {
                    selectedKeyIndex_ = keyIndex;
                    currentScrubTime_ = track.startTime + track.keys[keyIndex].time;
                    EvaluatePreviewAtCurrentTime();
                }
            }
            ImGui::EndCombo();
        }

        auto& key = track.keys[selectedKeyIndex_];
        bool keyChanged = false;
        keyChanged |= ImGui::DragFloat("Key Time", &key.time, 0.01f, 0.0f, 600.0f, "%.3f sec");
        keyChanged |= ImGui::DragFloat3("Position", &key.position.x, 0.05f);
        keyChanged |= ImGui::DragFloat3("Rotation", &key.rotation.x, 0.01f);
        keyChanged |= ImGui::DragFloat3("Scale", &key.scale.x, 0.01f, 0.001f, 100.0f);
        const char* easingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" };
        keyChanged |= ImGui::Combo("次のキーへの補間", &key.easingToNext, easingNames, IM_ARRAYSIZE(easingNames));
        if (keyChanged) {
            const float editedTime = key.time;
            sequence_.Sort();
            selectedKeyIndex_ = 0;
            float closestDistance = std::abs(track.keys.front().time - editedTime);
            for (int keyIndex = 1; keyIndex < static_cast<int>(track.keys.size()); ++keyIndex) {
                const float distance = std::abs(track.keys[keyIndex].time - editedTime);
                if (distance < closestDistance) {
                    closestDistance = distance;
                    selectedKeyIndex_ = keyIndex;
                }
            }
            currentScrubTime_ = track.startTime + track.keys[selectedKeyIndex_].time;
            EvaluatePreviewAtCurrentTime();
        }
        if (ImGui::Button(ICON_FA_TRASH_ALT " 選択キー削除")) {
            track.keys.erase(track.keys.begin() + selectedKeyIndex_);
            selectedKeyIndex_ = track.keys.empty() ? -1 : std::min(selectedKeyIndex_, static_cast<int>(track.keys.size()) - 1);
            EvaluatePreviewAtCurrentTime();
        }
    }

    if (bindingChanged) {
        RefreshPlayer(true);
    }
    if (ImGui::Button(ICON_FA_TRASH " トラック削除", ImVec2(-1.0f, 0.0f))) {
        RemoveSelectedTrack();
    }
#endif
}

void GhostDirector::DrawVFXTrackEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " VFXトラック追加", ImVec2(-1.0f, 0.0f))) {
        CinematicVFXTrackData track;
        track.name = "VFX Track";
        track.startTime = currentScrubTime_;
        if (studioEnabled_) track.binding = { "$Actor", -1 };
        sequence_.vfxTracks.push_back(track);
        selectedTrackKind_ = 1;
        selectedTrackIndex_ = static_cast<int>(sequence_.vfxTracks.size()) - 1;
        selectedKeyIndex_ = -1;
        RefreshPlayer(true);
    }

    for (int index = 0; index < static_cast<int>(sequence_.vfxTracks.size()); ++index) {
        auto& track = sequence_.vfxTracks[index];
        ImGui::PushID(10000 + index);
        const bool selected = selectedTrackKind_ == 1 && selectedTrackIndex_ == index;
        if (ImGui::Selectable(track.name.c_str(), selected)) {
            selectedTrackKind_ = 1;
            selectedTrackIndex_ = index;
            selectedKeyIndex_ = -1;
        }
        ImGui::PopID();
    }

    if (selectedTrackKind_ != 1 || selectedTrackIndex_ < 0 ||
        selectedTrackIndex_ >= static_cast<int>(sequence_.vfxTracks.size())) {
        return;
    }

    auto& track = sequence_.vfxTracks[selectedTrackIndex_];
    bool changed = false;
    char name[128]{};
    strncpy_s(name, sizeof(name), track.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("VFXトラック名", name, sizeof(name))) {
        track.name = name;
    }
    changed |= ImGui::Checkbox("VFX有効", &track.enabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("VFX Mute", &track.muted);
    changed |= ImGui::DragFloat("VFX開始時刻", &track.startTime, 0.01f, 0.0f, 600.0f, "%.3f sec");

    const auto vfxFiles = CollectJsonStems(kVFXDirectory);
    const std::string sequenceLabel = track.sequenceName.empty() ? "(未設定)" : track.sequenceName;
    if (ImGui::BeginCombo("VFX Sequence", sequenceLabel.c_str())) {
        for (const auto& fileName : vfxFiles) {
            if (ImGui::Selectable(fileName.c_str(), track.sequenceName == fileName)) {
                track.sequenceName = fileName;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::BeginDisabled(track.sequenceName.empty() || !editor_ || !editor_->GetVFXSequencerEditor());
    if (ImGui::Button("このVFXシーケンスを編集", ImVec2(-1.0f, 0.0f))) {
        // 演出の状態を復元してから素材編集へ移り、読込先で置換を確認します。
        player_.Stop(true);
        editor_->GetVFXSequencerEditor()->RequestOpenSequence(track.sequenceName);
        EditorCommandRegistry::GetInstance()->Execute("tool.vfx");
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("素材編集では共有VFXを変更します。保存後にこの演出を先頭から再生して確認してください。");

    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        const std::string targetLabel = track.binding.targetName.empty() ? "World" : track.binding.targetName;
        if (ImGui::BeginCombo("VFX Target", targetLabel.c_str())) {
            if (ImGui::Selectable("World", track.binding.targetName.empty())) {
                track.binding = {};
                changed = true;
            }
            for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
                if (object && !object->IsEditorInternal() && ImGui::Selectable(object->GetName().c_str(), object->GetName() == track.binding.targetName)) {
                    track.binding.targetName = object->GetName();
                    track.binding.targetEventId = object->GetEventID();
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    if (changed) {
        RefreshPlayer(true);
    }
    if (ImGui::Button(ICON_FA_TRASH " VFXトラック削除", ImVec2(-1.0f, 0.0f))) {
        RemoveSelectedTrack();
    }
#endif
}

void GhostDirector::DrawCameraShotEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " Camera Shot追加", ImVec2(-1.0f, 0.0f))) {
        CinematicCameraShot shot;
        shot.name = "Camera Shot";
        shot.startTime = currentScrubTime_;
        if (editor_) {
            Object3d* selected = editor_->GetSelectedObject();
            if (selected && selected->IsCameraObject()) {
                shot.name = selected->GetName();
                shot.binding.targetName = selected->GetName();
                shot.binding.targetEventId = selected->GetEventID();
                const SceneCameraSettings& settings = selected->GetSceneCameraSettings();
                shot.blendInDuration = settings.blendInDuration;
                shot.blendOutDuration = settings.blendOutDuration;
                shot.easing = static_cast<int>(settings.easing);
            }
        }
        if (studioEnabled_) {
            shot.binding = { "$Camera", -1 };
            shot.blendInDuration = shot.blendOutDuration = 0.0f;
        }
        sequence_.cameraShots.push_back(shot);
        selectedTrackKind_ = 2;
        selectedTrackIndex_ = static_cast<int>(sequence_.cameraShots.size()) - 1;
        selectedKeyIndex_ = -1;
        RefreshPlayer(true);
    }

    for (int index = 0; index < static_cast<int>(sequence_.cameraShots.size()); ++index) {
        const auto& shot = sequence_.cameraShots[index];
        ImGui::PushID(20000 + index);
        if (ImGui::Selectable(shot.name.c_str(), selectedTrackKind_ == 2 && selectedTrackIndex_ == index)) {
            selectedTrackKind_ = 2;
            selectedTrackIndex_ = index;
            selectedKeyIndex_ = -1;
            if (editor_) {
                if (Object3d* target = ResolveTrackTarget(shot.binding)) {
                    editor_->SetSelectedObject(target);
                }
            }
        }
        ImGui::PopID();
    }

    if (selectedTrackKind_ != 2 || selectedTrackIndex_ < 0 ||
        selectedTrackIndex_ >= static_cast<int>(sequence_.cameraShots.size())) {
        return;
    }

    auto& shot = sequence_.cameraShots[selectedTrackIndex_];
    char name[128]{};
    strncpy_s(name, sizeof(name), shot.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("Shot名", name, sizeof(name))) {
        shot.name = name;
    }
    bool refresh = DrawObjectBindingEditor("Camera Object", shot.binding, true, false);
    ImGui::Checkbox("有効##CameraShot", &shot.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Mute##CameraShot", &shot.muted);
    ImGui::DragFloat("開始時刻##CameraShot", &shot.startTime, 0.01f, 0.0f, 600.0f, "%.3f sec");
    ImGui::DragFloat("長さ##CameraShot", &shot.duration, 0.01f, 0.05f, 600.0f, "%.3f sec");
    ImGui::DragFloat("Blend In##CameraShot", &shot.blendInDuration, 0.01f, 0.0f, 10.0f, "%.3f sec");
    ImGui::DragFloat("Blend Out##CameraShot", &shot.blendOutDuration, 0.01f, 0.0f, 10.0f, "%.3f sec");
    const char* easingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" };
    ImGui::Combo("Blend補間##CameraShot", &shot.easing, easingNames, IM_ARRAYSIZE(easingNames));
    if (refresh) {
        RefreshPlayer(true);
    }
    if (ImGui::Button(ICON_FA_TRASH " Camera Shot削除", ImVec2(-1.0f, 0.0f))) {
        RemoveSelectedTrack();
    }
#endif
}

void GhostDirector::DrawAnimationClipEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " Animation Clip追加", ImVec2(-1.0f, 0.0f))) {
        CinematicAnimationClipData clip;
        clip.name = "Animation Clip";
        clip.startTime = currentScrubTime_;
        if (editor_) {
            if (Object3d* selected = editor_->GetSelectedObject()) {
                clip.binding.targetName = selected->GetName();
                clip.binding.targetEventId = selected->GetEventID();
            }
        }
        if (studioEnabled_) {
            clip.binding = { "$Actor", -1 };
            clip.driver = "Body";
        }
        sequence_.animationClips.push_back(clip);
        selectedTrackKind_ = 3;
        selectedTrackIndex_ = static_cast<int>(sequence_.animationClips.size()) - 1;
        selectedKeyIndex_ = -1;
        RefreshPlayer(true);
    }

    for (int index = 0; index < static_cast<int>(sequence_.animationClips.size()); ++index) {
        const auto& clip = sequence_.animationClips[index];
        ImGui::PushID(30000 + index);
        if (ImGui::Selectable(clip.name.c_str(), selectedTrackKind_ == 3 && selectedTrackIndex_ == index)) {
            selectedTrackKind_ = 3;
            selectedTrackIndex_ = index;
            selectedKeyIndex_ = -1;
        }
        ImGui::PopID();
    }
    if (selectedTrackKind_ != 3 || selectedTrackIndex_ < 0 ||
        selectedTrackIndex_ >= static_cast<int>(sequence_.animationClips.size())) {
        return;
    }

    auto& clip = sequence_.animationClips[selectedTrackIndex_];
    char name[128]{};
    char driver[128]{};
    char clipName[128]{};
    strncpy_s(name, sizeof(name), clip.name.c_str(), _TRUNCATE);
    strncpy_s(driver, sizeof(driver), clip.driver.c_str(), _TRUNCATE);
    strncpy_s(clipName, sizeof(clipName), clip.clipName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Clip名##Animation", name, sizeof(name))) clip.name = name;
    bool refresh = DrawObjectBindingEditor("Animation対象", clip.binding, false, false);
    int playbackKind = clip.driver == "Body" ? 1 : 0;
    const char* playbackKinds[] = { "Animator / 専用Driver", "身体変形（Animation Workbench）" };
    if (ImGui::Combo("再生方式", &playbackKind, playbackKinds, IM_ARRAYSIZE(playbackKinds))) {
        clip.driver = playbackKind == 1 ? "Body" : "Animator";
        refresh = true;
    }
    if (clip.driver == "Body") {
        if (ImGui::BeginCombo("身体クリップ", clip.clipName.empty() ? "クリップを選択" : clip.clipName.c_str())) {
            std::error_code error;
            std::vector<std::string> assets;
            fs::directory_iterator it("Resources/json/animation_clip", error), end;
            while (!error && it != end) {
                if (it->path().extension() == ".json") assets.push_back(it->path().stem().string());
                it.increment(error);
            }
            std::sort(assets.begin(), assets.end());
            for (const auto& asset : assets) {
                if (ImGui::Selectable(asset.c_str(), clip.clipName == asset)) {
                    clip.clipName = asset;
                    BodyAnimationClip body;
                    if (body.Load(BodyAnimationClip::ResolveAssetPath(asset))) {
                        clip.duration = body.GetDuration() / std::max(0.01f, clip.playbackSpeed);
                        clip.loop = body.IsLoopDefault();
                    }
                    refresh = true;
                }
            }
            if (assets.empty()) ImGui::TextDisabled("保存済みクリップがありません");
            ImGui::EndCombo();
        }
        if (ImGui::Button("再読込・元の長さに合わせる")) {
            BodyAnimationClip body;
            if (body.Load(BodyAnimationClip::ResolveAssetPath(clip.clipName))) {
                clip.duration = body.GetDuration() / std::max(0.01f, clip.playbackSpeed);
            }
            refresh = true;
        }
        ImGui::TextWrapped("移動・当たり判定は変更せず、見た目の伸縮・回転・ずれ・せん断を再生します。ループ周期は元クリップの長さです。同じ対象で重なる区間はリストの後を優先します。");
    } else {
    if (ImGui::InputText("Driver", driver, sizeof(driver))) { clip.driver = driver; refresh = true; }
    if (ImGui::InputText("Animation / State", clipName, sizeof(clipName))) { clip.clipName = clipName; refresh = true; }
    }
    refresh |= ImGui::Checkbox("有効##Animation", &clip.enabled);
    ImGui::SameLine();
    refresh |= ImGui::Checkbox("Mute##Animation", &clip.muted);
    ImGui::SameLine();
    refresh |= ImGui::Checkbox("Loop##Animation", &clip.loop);
    refresh |= ImGui::DragFloat("開始時刻##Animation", &clip.startTime, 0.01f, 0.0f, 600.0f, "%.3f sec", ImGuiSliderFlags_AlwaysClamp);
    refresh |= ImGui::DragFloat("長さ##Animation", &clip.duration, 0.01f, 0.05f, 600.0f, "%.3f sec", ImGuiSliderFlags_AlwaysClamp);
    refresh |= ImGui::DragFloat("再生速度##Animation", &clip.playbackSpeed, 0.01f, 0.0f, 8.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    refresh |= ImGui::DragFloat("Blend In##Animation", &clip.blendInDuration, 0.01f, 0.0f, 5.0f, "%.3f sec", ImGuiSliderFlags_AlwaysClamp);
    const char* easingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" };
    refresh |= ImGui::Combo("Blend補間##Animation", &clip.easing, easingNames, IM_ARRAYSIZE(easingNames));
    refresh |= ImGui::Checkbox("停止時に元のStateへ戻す##Animation", &clip.restoreOnStop);
    ImGui::TextWrapped("Animator方式はState名を指定します。身体変形は上の一覧から選択します。それ以外の専用Driverにはゲーム側Callbackが必要です。");
    if (refresh) {
        RefreshPlayer(true);
    }
    if (ImGui::Button(ICON_FA_TRASH " Animation Clip削除", ImVec2(-1.0f, 0.0f))) {
        RemoveSelectedTrack();
    }
#endif
}

void GhostDirector::DrawAudioClipEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " Audio Clip追加", ImVec2(-1.0f, 0.0f))) {
        CinematicAudioClipData clip;
        clip.name = "Audio Clip";
        clip.startTime = currentScrubTime_;
        sequence_.audioClips.push_back(clip);
        selectedTrackKind_ = 4;
        selectedTrackIndex_ = static_cast<int>(sequence_.audioClips.size()) - 1;
        selectedKeyIndex_ = -1;
        RefreshPlayer(true);
    }
    for (int index = 0; index < static_cast<int>(sequence_.audioClips.size()); ++index) {
        const auto& clip = sequence_.audioClips[index];
        ImGui::PushID(40000 + index);
        if (ImGui::Selectable(clip.name.c_str(), selectedTrackKind_ == 4 && selectedTrackIndex_ == index)) {
            selectedTrackKind_ = 4;
            selectedTrackIndex_ = index;
            selectedKeyIndex_ = -1;
        }
        ImGui::PopID();
    }
    if (selectedTrackKind_ != 4 || selectedTrackIndex_ < 0 ||
        selectedTrackIndex_ >= static_cast<int>(sequence_.audioClips.size())) {
        return;
    }

    auto& clip = sequence_.audioClips[selectedTrackIndex_];
    char name[128]{};
    strncpy_s(name, sizeof(name), clip.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("Clip名##Audio", name, sizeof(name))) clip.name = name;
    const char* audioLabel = clip.audioId.empty() ? "(未設定)" : clip.audioId.c_str();
    if (ImGui::BeginCombo("Audio ID", audioLabel)) {
        for (const auto& entry : GameAudioSettings::GetInstance()->GetEntries()) {
            if (ImGui::Selectable(entry.id.c_str(), clip.audioId == entry.id)) {
                clip.audioId = entry.id;
                if (clip.name == "Audio Clip") {
                    clip.name = entry.displayName.empty() ? entry.id : entry.displayName;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("有効##Audio", &clip.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Mute##Audio", &clip.muted);
    ImGui::SameLine();
    ImGui::Checkbox("Loop##Audio", &clip.loop);
    ImGui::DragFloat("開始時刻##Audio", &clip.startTime, 0.01f, 0.0f, 600.0f, "%.3f sec");
    ImGui::DragFloat("長さ##Audio", &clip.duration, 0.01f, 0.05f, 600.0f, "%.3f sec");
    ImGui::SliderFloat("音量##Audio", &clip.volume, 0.0f, 2.0f, "%.2f");
    ImGui::TextDisabled("スクラブ中は発音せず、再生ヘッドが開始時刻を通過した時だけ鳴ります。");
    if (ImGui::Button(ICON_FA_TRASH " Audio Clip削除", ImVec2(-1.0f, 0.0f))) {
        RemoveSelectedTrack();
    }
#endif
}

void GhostDirector::DrawSignalEditor() {
#ifdef USE_IMGUI
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " Signal追加", ImVec2(-1.0f, 0.0f))) {
        CinematicSignalMarker signal;
        signal.name = "Signal";
        signal.time = currentScrubTime_;
        sequence_.signals.push_back(signal);
        selectedTrackKind_ = 5;
        selectedTrackIndex_ = static_cast<int>(sequence_.signals.size()) - 1;
        selectedKeyIndex_ = -1;
        RefreshPlayer(true);
    }
    for (int index = 0; index < static_cast<int>(sequence_.signals.size()); ++index) {
        const auto& signal = sequence_.signals[index];
        ImGui::PushID(50000 + index);
        if (ImGui::Selectable(signal.name.c_str(), selectedTrackKind_ == 5 && selectedTrackIndex_ == index)) {
            selectedTrackKind_ = 5;
            selectedTrackIndex_ = index;
            selectedKeyIndex_ = -1;
            currentScrubTime_ = signal.time;
        }
        ImGui::PopID();
    }
    if (selectedTrackKind_ != 5 || selectedTrackIndex_ < 0 ||
        selectedTrackIndex_ >= static_cast<int>(sequence_.signals.size())) {
        return;
    }

    auto& signal = sequence_.signals[selectedTrackIndex_];
    char name[128]{};
    char signalId[128]{};
    char payload[256]{};
    strncpy_s(name, sizeof(name), signal.name.c_str(), _TRUNCATE);
    strncpy_s(signalId, sizeof(signalId), signal.signal.c_str(), _TRUNCATE);
    strncpy_s(payload, sizeof(payload), signal.payload.c_str(), _TRUNCATE);
    if (ImGui::InputText("Marker名", name, sizeof(name))) signal.name = name;
    if (ImGui::InputText("Signal ID", signalId, sizeof(signalId))) signal.signal = signalId;
    if (ImGui::InputText("Payload", payload, sizeof(payload))) signal.payload = payload;
    DrawObjectBindingEditor("Signal対象", signal.binding, false, true);
    ImGui::Checkbox("有効##Signal", &signal.enabled);
    if (ImGui::DragFloat("時刻##Signal", &signal.time, 0.01f, 0.0f, 600.0f, "%.3f sec")) {
        currentScrubTime_ = signal.time;
    }
    ImGui::TextDisabled("Signalは通常再生で時刻を通過した時だけ発火します。スクラブでは発火しません。");
    if (ImGui::Button(ICON_FA_TRASH " Signal削除", ImVec2(-1.0f, 0.0f))) {
        RemoveSelectedTrack();
    }
#endif
}

void GhostDirector::DrawTimelineCanvas() {
#ifdef USE_IMGUI
    constexpr float labelWidth = 230.0f;
    constexpr float headerHeight = 28.0f;
    constexpr float rowHeight = 36.0f;
    const int transformCount = static_cast<int>(sequence_.transformTracks.size());
    const int cameraCount = static_cast<int>(sequence_.cameraShots.size());
    const int animationCount = static_cast<int>(sequence_.animationClips.size());
    const int vfxCount = static_cast<int>(sequence_.vfxTracks.size());
    const int audioCount = static_cast<int>(sequence_.audioClips.size());
    const int signalCount = static_cast<int>(sequence_.signals.size());
    const int authoredRowCount = transformCount + cameraCount + animationCount + vfxCount + audioCount + signalCount;
    const int rowCount = std::max(1, authoredRowCount);
    const float duration = std::max(1.0f, GetScenarioDuration());
    const float contentWidth = std::max(ImGui::GetContentRegionAvail().x, labelWidth + duration * timelinePixelsPerSecond_ + 120.0f);
    const float contentHeight = headerHeight + rowHeight * rowCount + 16.0f;

    ImGui::BeginChild("##CinematicTimelineScroll", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##CinematicTimelineCanvas", ImVec2(contentWidth, contentHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + contentWidth, origin.y + contentHeight);
    drawList->AddRectFilled(origin, end, IM_COL32(24, 27, 34, 255));
    drawList->AddRectFilled(origin, ImVec2(origin.x + labelWidth, end.y), IM_COL32(35, 39, 48, 255));
    drawList->AddLine(ImVec2(origin.x + labelWidth, origin.y), ImVec2(origin.x + labelWidth, end.y), IM_COL32(105, 115, 135, 255), 1.0f);

    const int secondCount = static_cast<int>(std::ceil(duration));
    for (int second = 0; second <= secondCount; ++second) {
        const float x = origin.x + labelWidth + second * timelinePixelsPerSecond_;
        drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), IM_COL32(70, 76, 90, 180), 1.0f);
        char label[32];
        sprintf_s(label, "%ds", second);
        drawList->AddText(ImVec2(x + 4.0f, origin.y + 5.0f), IM_COL32(180, 188, 205, 255), label);
    }

    for (int row = 0; row < rowCount; ++row) {
        const float y0 = origin.y + headerHeight + row * rowHeight;
        const float y1 = y0 + rowHeight;
        if ((row % 2) == 0) {
            drawList->AddRectFilled(ImVec2(origin.x, y0), ImVec2(end.x, y1), IM_COL32(255, 255, 255, 7));
        }
        drawList->AddLine(ImVec2(origin.x, y1), ImVec2(end.x, y1), IM_COL32(60, 65, 78, 180), 1.0f);

        if (row < transformCount) {
            const auto& track = sequence_.transformTracks[row];
            const bool selected = selectedTrackKind_ == 0 && selectedTrackIndex_ == row;
            const ImU32 textColor = selected ? IM_COL32(255, 230, 110, 255) : IM_COL32(215, 220, 230, 255);
            drawList->AddText(ImVec2(origin.x + 9.0f, y0 + 9.0f), textColor, track.name.c_str());
            const float startX = origin.x + labelWidth + track.startTime * timelinePixelsPerSecond_;
            const float trackDuration = std::max(0.05f, GetTransformTrackDuration(row));
            const float finishX = startX + trackDuration * timelinePixelsPerSecond_;
            const ImU32 clipColor = track.muted ? IM_COL32(75, 80, 92, 220) : IM_COL32(62, 145, 220, 220);
            drawList->AddRectFilled(ImVec2(startX, y0 + 6.0f), ImVec2(finishX, y1 - 6.0f), clipColor, 4.0f);
            for (int keyIndex = 0; keyIndex < static_cast<int>(track.keys.size()); ++keyIndex) {
                const float keyX = startX + track.keys[keyIndex].time * timelinePixelsPerSecond_;
                const bool keySelected = selected && selectedKeyIndex_ == keyIndex;
                drawList->AddCircleFilled(ImVec2(keyX, (y0 + y1) * 0.5f), keySelected ? 6.0f : 4.5f,
                    keySelected ? IM_COL32(255, 230, 90, 255) : IM_COL32(235, 245, 255, 255));
            }
        } else if (row - transformCount < cameraCount) {
            const int cameraIndex = row - transformCount;
            const auto& shot = sequence_.cameraShots[cameraIndex];
            const bool selected = selectedTrackKind_ == 2 && selectedTrackIndex_ == cameraIndex;
            drawList->AddText(ImVec2(origin.x + 9.0f, y0 + 9.0f),
                selected ? IM_COL32(120, 235, 255, 255) : IM_COL32(215, 220, 230, 255), shot.name.c_str());
            const float startX = origin.x + labelWidth + shot.startTime * timelinePixelsPerSecond_;
            const float finishX = startX + std::max(0.05f, shot.duration) * timelinePixelsPerSecond_;
            const ImU32 color = shot.muted ? IM_COL32(75, 80, 92, 220) : IM_COL32(45, 175, 205, 230);
            drawList->AddRectFilled(ImVec2(startX, y0 + 6.0f), ImVec2(finishX, y1 - 6.0f), color, 4.0f);
        } else if (row - transformCount - cameraCount < animationCount) {
            const int animationIndex = row - transformCount - cameraCount;
            const auto& clip = sequence_.animationClips[animationIndex];
            const bool selected = selectedTrackKind_ == 3 && selectedTrackIndex_ == animationIndex;
            drawList->AddText(ImVec2(origin.x + 9.0f, y0 + 9.0f),
                selected ? IM_COL32(255, 205, 105, 255) : IM_COL32(215, 220, 230, 255), clip.name.c_str());
            const float startX = origin.x + labelWidth + clip.startTime * timelinePixelsPerSecond_;
            const float finishX = startX + std::max(0.05f, clip.duration) * timelinePixelsPerSecond_;
            const ImU32 color = clip.muted ? IM_COL32(75, 80, 92, 220) : IM_COL32(220, 135, 45, 230);
            drawList->AddRectFilled(ImVec2(startX, y0 + 6.0f), ImVec2(finishX, y1 - 6.0f), color, 4.0f);
        } else if (row - transformCount - cameraCount - animationCount < vfxCount) {
            const int vfxIndex = row - transformCount - cameraCount - animationCount;
            const auto& track = sequence_.vfxTracks[vfxIndex];
            const bool selected = selectedTrackKind_ == 1 && selectedTrackIndex_ == vfxIndex;
            drawList->AddText(ImVec2(origin.x + 9.0f, y0 + 9.0f),
                selected ? IM_COL32(255, 210, 125, 255) : IM_COL32(215, 220, 230, 255), track.name.c_str());
            const float startX = origin.x + labelWidth + track.startTime * timelinePixelsPerSecond_;
            const float finishX = startX + std::max(0.1f, track.duration) * timelinePixelsPerSecond_;
            drawList->AddRectFilled(ImVec2(startX, y0 + 6.0f), ImVec2(finishX, y1 - 6.0f), IM_COL32(165, 85, 215, 220), 4.0f);
        } else if (row - transformCount - cameraCount - animationCount - vfxCount < audioCount) {
            const int audioIndex = row - transformCount - cameraCount - animationCount - vfxCount;
            const auto& clip = sequence_.audioClips[audioIndex];
            const bool selected = selectedTrackKind_ == 4 && selectedTrackIndex_ == audioIndex;
            drawList->AddText(ImVec2(origin.x + 9.0f, y0 + 9.0f),
                selected ? IM_COL32(135, 255, 155, 255) : IM_COL32(215, 220, 230, 255), clip.name.c_str());
            const float startX = origin.x + labelWidth + clip.startTime * timelinePixelsPerSecond_;
            const float finishX = startX + std::max(0.05f, clip.duration) * timelinePixelsPerSecond_;
            const ImU32 color = clip.muted ? IM_COL32(75, 80, 92, 220) : IM_COL32(55, 175, 95, 230);
            drawList->AddRectFilled(ImVec2(startX, y0 + 6.0f), ImVec2(finishX, y1 - 6.0f), color, 4.0f);
        } else if (row < authoredRowCount) {
            const int signalIndex = row - transformCount - cameraCount - animationCount - vfxCount - audioCount;
            const auto& signal = sequence_.signals[signalIndex];
            const bool selected = selectedTrackKind_ == 5 && selectedTrackIndex_ == signalIndex;
            drawList->AddText(ImVec2(origin.x + 9.0f, y0 + 9.0f),
                selected ? IM_COL32(255, 135, 145, 255) : IM_COL32(215, 220, 230, 255), signal.name.c_str());
            const float markerX = origin.x + labelWidth + signal.time * timelinePixelsPerSecond_;
            drawList->AddRectFilled(
                ImVec2(markerX - 3.0f, y0 + 5.0f), ImVec2(markerX + 3.0f, y1 - 5.0f),
                signal.enabled ? IM_COL32(235, 75, 85, 240) : IM_COL32(90, 90, 95, 220), 2.0f);
        }
    }

    const float playheadX = origin.x + labelWidth + currentScrubTime_ * timelinePixelsPerSecond_;
    drawList->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, end.y), IM_COL32(255, 82, 82, 255), 2.0f);
    drawList->AddTriangleFilled(ImVec2(playheadX - 6.0f, origin.y), ImVec2(playheadX + 6.0f, origin.y),
        ImVec2(playheadX, origin.y + 9.0f), IM_COL32(255, 82, 82, 255));

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int row = static_cast<int>((mouse.y - origin.y - headerHeight) / rowHeight);
        if (row >= 0 && row < authoredRowCount) {
            if (row < transformCount) {
                selectedTrackKind_ = 0;
                selectedTrackIndex_ = row;
                selectedKeyIndex_ = -1;
                SelectTrackTarget(row);
            } else if (row - transformCount < cameraCount) {
                selectedTrackKind_ = 2;
                selectedTrackIndex_ = row - transformCount;
                selectedKeyIndex_ = -1;
                if (editor_) {
                    if (Object3d* target = ResolveTrackTarget(sequence_.cameraShots[selectedTrackIndex_].binding)) {
                        editor_->SetSelectedObject(target);
                    }
                }
            } else if (row - transformCount - cameraCount < animationCount) {
                selectedTrackKind_ = 3;
                selectedTrackIndex_ = row - transformCount - cameraCount;
                selectedKeyIndex_ = -1;
            } else if (row - transformCount - cameraCount - animationCount < vfxCount) {
                selectedTrackKind_ = 1;
                selectedTrackIndex_ = row - transformCount - cameraCount - animationCount;
                selectedKeyIndex_ = -1;
            } else if (row - transformCount - cameraCount - animationCount - vfxCount < audioCount) {
                selectedTrackKind_ = 4;
                selectedTrackIndex_ = row - transformCount - cameraCount - animationCount - vfxCount;
                selectedKeyIndex_ = -1;
            } else {
                selectedTrackKind_ = 5;
                selectedTrackIndex_ = row - transformCount - cameraCount - animationCount - vfxCount - audioCount;
                selectedKeyIndex_ = -1;
            }
        }
        if (mouse.x >= origin.x + labelWidth) {
            currentScrubTime_ = std::clamp((mouse.x - origin.x - labelWidth) / timelinePixelsPerSecond_, 0.0f, duration);
            if (selectedTrackKind_ == 0 && selectedTrackIndex_ >= 0 && selectedTrackIndex_ < transformCount) {
                const auto& track = sequence_.transformTracks[selectedTrackIndex_];
                const float localTime = currentScrubTime_ - track.startTime;
                float bestDistance = 8.0f / timelinePixelsPerSecond_;
                for (int keyIndex = 0; keyIndex < static_cast<int>(track.keys.size()); ++keyIndex) {
                    const float distance = std::abs(track.keys[keyIndex].time - localTime);
                    if (distance <= bestDistance) {
                        bestDistance = distance;
                        selectedKeyIndex_ = keyIndex;
                    }
                }
            }
            EvaluatePreviewAtCurrentTime();
        }
    }
    ImGui::EndChild();
#endif
}
