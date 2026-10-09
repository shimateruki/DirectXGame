#include "EnemyAttackPreviewWindow.h"
#include "SceneManager.h"
#include "ProjectWindow.h"
#include "BaseEnemy.h"
#include "Player.h"
#include "MeshEffectManager.h"
#include "BulletManager.h"
#include "CameraManager.h"
#include "Camera.h"
#include "ModelManager.h"
#include "BaseScene.h"
#include "MeshRenderer.h"
#include "GPUParticleManager.h"
#include "EditorManager.h"
#include "game/actor/combat/AttackSequence.h"
#include "game/actor/combat/AttackPlacementMath.h"
#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>
#ifdef USE_IMGUI
#include "imgui.h"
#include "ImGuizmo.h"

namespace {
bool EditText(const char* label, std::string& text) {
    char buffer[1024]{};
    text.copy(buffer, sizeof(buffer) - 1);
    if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
    text = buffer;
    return true;
}
bool SelectJson(const char* label, const char* directory, std::string& path, ProjectWindow* assets = nullptr) {
    bool changed = false;
    const std::string selectedName = path.empty() ? "選択してください" : std::filesystem::path(path).stem().string();
    if (ImGui::BeginCombo(label, selectedName.c_str())) {
        static char search[128]{};
        ImGui::InputText("素材名で検索", search, sizeof(search));
        std::error_code error;
        std::vector<std::string> paths;
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
            if (it->path().extension() == ".json") paths.push_back(it->path().generic_string());
        std::sort(paths.begin(), paths.end());
        for (const auto& candidate : paths) {
            if (search[0] && candidate.find(search) == std::string::npos) continue;
            ImGui::PushID(candidate.c_str());
            if (assets) {
                const uint64_t texture = ImGui::IsRectVisible(ImVec2(64, 64)) ? assets->GetEffectThumbnailGpuPtr(candidate) : 0;
                if (texture) ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(64, 64));
                else ImGui::Dummy(ImVec2(64, 64));
                ImGui::SameLine();
            }
            const std::string name = std::filesystem::path(candidate).stem().string();
            if (ImGui::Selectable(name.c_str(), candidate == path, 0, ImVec2(0, assets ? 64.0f : 0.0f))) { path = candidate; changed = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", candidate.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (assets && !path.empty()) {
        if (const auto texture = assets->GetEffectThumbnailGpuPtr(path)) ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(128, 128));
        else ImGui::TextDisabled("素材の画像を生成中");
        if (ImGui::SmallButton("素材を撮り直す")) assets->InvalidatePresetThumbnail("@effect:" + path);
    }
    return changed;
}
}
#endif

void EnemyAttackPreviewWindow::StopPlacementPreview() {
    if (placementGizmoUsing_) {
        authoringGestureOpen_ = false;
        RememberSequenceEdit(placementGestureBefore_);
        placementGizmoUsing_ = false;
        authoringGestureOpen_ = false;
    }
    GPUParticleManager::GetInstance()->ClearAuthoringPreview();
    if (sceneManager_ && sceneManager_->GetCurrentScene() == previewScene_ && previewScene_) {
        if (placementObject_) {
            previewScene_->RequestRemoveObject(placementObject_);
            std::erase(spawnedPreviewObjects_, placementObject_);
        }
        if (previewEnemy_) previewEnemy_->SetIsVisible(true);
        if (previewPlayer_) previewPlayer_->SetIsVisible(true);
        if (previewTarget_) previewTarget_->SetIsVisible(showTarget_);
    }
    placementObject_ = nullptr;
    placementGpuSignature_.clear();
    placementMessage_.clear();
    if (placementScope_) MeshEffectManager::GetInstance()->StopEffectScope(placementScope_);
    placementScope_ = 0;
    placementPreset_.clear();
    placementPreview_ = false;
}

void EnemyAttackPreviewWindow::BeginPlacementPreview() {
    if (!IsCurrentSceneReady() || sceneManager_->IsPlaying()) {
        profileStatus_ = "編集モードのSceneを開いてから配置を確認してください。";
        return;
    }
    authoringActorSelected_ = false;
    draftRefreshPending_ = false;
    ClearPreviewTransientState();
    RemovePreviewObjects();
    enabled_ = true;
    CreatePreviewObjects();
    playing_ = false;
    placementPreview_ = true;
    placementTime_ = selectedTrack_ <= -2 ? 0.15f : 0.0f;
    if (const auto* attack = GetSelectedAttackDefinition(); attack && selectedTrack_ >= 0 &&
        selectedTrack_ < static_cast<int>(attack->sequence.tracks.size())) {
        const auto& track = attack->sequence.tracks[selectedTrack_];
        placementTime_ = (std::max)(0.01f, track.end - track.begin) * 0.4f;
    }
    UpdatePlacementPreview();
}

void EnemyAttackPreviewWindow::UpdatePlacementPreview() {
    const auto* attack = GetSelectedAttackDefinition();
    if (!attack) {
        StopPlacementPreview();
        return;
    }
    Object3d* actor = playerCopyMode_ ? static_cast<Object3d*>(previewPlayer_) : static_cast<Object3d*>(previewEnemy_);
    if (actor) actor->SetIsVisible(!hidePlacementActor_);
    if (previewTarget_) previewTarget_->SetIsVisible(!hidePlacementActor_ && showTarget_);
    Vector3 position, rotation, scale;
    if (!GetPlacementTransform(position, rotation, scale)) { StopPlacementPreview(); return; }
    if (selectedTrack_ <= -2) {
        const auto& event = attack->sequence.events[-2 - selectedTrack_];
        const auto part = ResolveProjectileComponent(*attack, event);
        if (event.operation == "projectile") {
            if (!placementObject_) {
                auto object = std::make_unique<Object3d>();
                object->Initialize(previewScene_->GetObject3dCommon());
                object->SetName("AttackPlacementObject");
                object->SetClassName("EditorOnly"); object->SetEditorInternal(true);
                object->SetCollisionAttribute(0); object->SetCollisionMask(0);
                object->SetIsLocked(true);
                placementObject_ = object.get();
                spawnedPreviewObjects_.push_back(object.get());
                previewScene_->AddObject(std::move(object));
            }
            if (placementPreset_ != part.model) { placementObject_->SetModel(part.model); placementPreset_ = part.model; }
            placementObject_->SetIsVisible(projectileSection_ < 3);
            placementObject_->SetTranslate(position); placementObject_->SetRotation(rotation); placementObject_->SetScale(scale);
            placementObject_->SetMaterialType(part.style == "fire" ? 11 : 0);
            const auto& a = part.appearance;
            placementObject_->SetColor({a.red,a.green,a.blue,a.alpha}); placementObject_->SetEmissive(a.emissive);
            if (auto* renderer = placementObject_->GetMeshRenderer()) if (auto* water = renderer->GetWaterParamData()) {
                water->effectType = 1; water->effectScale = a.effectScale; water->effectSoftness = a.softness;
                water->effectIntensity = a.intensity; water->billboardScale = a.billboardScale;
            }
            placementObject_->UpdateWorldMatrix(true);
        }
        const std::string primary = event.operation == "gpu_vfx" ? event.preset :
            projectileSection_ == 3 ? part.trail : projectileSection_ == 4 ? part.impact : "";
        const std::string secondary = event.operation == "gpu_vfx" ? "" :
            projectileSection_ == 3 ? part.secondaryTrail : projectileSection_ == 4 ? part.secondaryImpact : "";
        AttackSequenceContext context;
        context.position = actor ? actor->GetTranslate() : Vector3{};
        const float baseYaw = rotation.y - event.yawDegrees * 0.01745329252f;
        context.forward = {std::sin(baseYaw),0,std::cos(baseYaw)};
        context.ownerSize = previewBaseScale_; context.hasTarget = previewTarget_ != nullptr;
        if (previewTarget_) context.targetPosition = previewTarget_->GetTranslate();
        Vector3 direction = ResolveAttackSequenceEmission(event,context,0,0).direction;
        const bool directed = event.operation == "gpu_vfx" ? event.directed : projectileSection_ == 3 && part.style == "fire";
        const float speed = event.operation == "gpu_vfx" ? event.particleSpeedScale : part.appearance.trailSpeedScale;
        if (event.operation == "projectile" && projectileSection_ == 3 && part.style == "fire") direction = direction * -1;
        const auto signature = primary + "|" + secondary + "|" + std::to_string(position.x) + "|" +
            std::to_string(position.y) + "|" + std::to_string(position.z) + "|" + std::to_string(rotation.y) + "|" +
            std::to_string(placementTime_) + "|" + std::to_string(directed) + "|" + std::to_string(speed) + "|" +
            std::to_string(direction.x) + "|" + std::to_string(direction.y) + "|" + std::to_string(direction.z);
        if (signature != placementGpuSignature_) {
            placementGpuSignature_ = signature;
            auto* gpu = GPUParticleManager::GetInstance(); gpu->ClearAuthoringPreview();
            placementMessage_ = primary.empty() ?
                (event.operation == "gpu_vfx" || projectileSection_ >= 3 ? "GPU素材は未指定です。" : "生成Objectの配置確認です。衝突は発生しません。") :
                "選択した素材の単独表示です。寿命を過ぎると粒子は見えません。";
            for (const auto& preset : {primary, secondary}) if (!preset.empty()) {
                if (!gpu->ShowAuthoringPreview(preset, position, direction, directed,
                    speed, placementTime_)) placementMessage_ = "GPU素材を読み込めません（未登録、または単独表示非対応）: " + preset;
            }
        }
        return;
    }
    if (selectedTrack_ < 0 || selectedTrack_ >= static_cast<int>(attack->sequence.tracks.size())) return;
    const auto& track = attack->sequence.tracks[selectedTrack_];
    if (track.type != "mesh_vfx") return; // 判定はViewportに実寸の輪を表示します。
    auto* effects = MeshEffectManager::GetInstance();
    if (placementPreset_ != track.preset || !placementScope_) {
        if (placementScope_) effects->StopEffectScope(placementScope_);
        placementScope_ = effects->CreateEffectScope();
        placementPreset_ = track.preset;
        // 読込失敗でもスコープとパスを保持し、毎フレーム再試行しません。
        if (!effects->StartTimelineEffect(placementScope_, track.preset)) {
            profileStatus_ = "素材を表示できません: " + track.preset;
            return;
        }
        profileStatus_ = "配置確認中: 位置・回転・大きさを変更できます。ファイルはまだ保存していません。";
    }
    Object3d* owner = playerCopyMode_ ? static_cast<Object3d*>(previewPlayer_) : static_cast<Object3d*>(previewEnemy_);
    if (!owner) return;
    const float duration = (std::max)(0.01f, track.end - track.begin);
    placementTime_ = std::clamp(placementTime_, 0.0f, duration);
    std::string error;
    if (!track.Validate(attack->sequence.duration, &error)) {
        profileStatus_ = "配置確認を更新できません: " + error;
        return;
    }
    auto key = track.Evaluate(placementTime_);
    if (track.motion != "keyframed") key.position = track.keys.front().position;
    Vector3 forward{0, 0, 1};
    if (previewTarget_) {
        Vector3 offset = previewTarget_->GetTranslate() - owner->GetTranslate();
        offset.y = 0;
        if (Math::Length(offset) > 0.0001f) forward = Math::Normalize(offset);
    }
    const Vector3 right{forward.z, 0, -forward.x};
    const Vector3 worldPosition = owner->GetTranslate() + right * key.position[0] + Vector3{0, key.position[1], 0} + forward * key.position[2];
    effects->EvaluateTimelineEffect(placementScope_, placementTime_, duration, worldPosition,
        {key.rotation[0], key.rotation[1] + std::atan2(forward.x, forward.z), key.rotation[2]},
        {key.scale[0], key.scale[1], key.scale[2]});
}

void EnemyAttackPreviewWindow::ReloadAuthoringProfile() {
    const auto* selected = GetSelectedAttackDefinition();
    const std::string id = selected ? selected->id : "";
    EnemyAttackProfile loaded;
    std::string error;
    const auto path = EnemyAttackProfile::GetDefaultPath(GetSelectedEnemyType());
    // 失敗時に編集内容を失わないよう、一時プロファイルで読込が完了してから置換します。
    if (!loaded.LoadFromFile(path, &error) || loaded.attacks.empty()) {
        profileStatus_ = "再読込できません。編集中の内容は保持しました: " + error;
        return;
    }
    StopPreview();
    editableProfile_ = std::move(loaded);
    profileAttackIndex_ = 0;
    for (int i = 0; i < static_cast<int>(editableProfile_.attacks.size()); ++i)
        if (playerCopyMode_ == (editableProfile_.attacks[i].sequence.binding == "player_special")) { profileAttackIndex_ = i; break; }
    for (int i = 0; i < static_cast<int>(editableProfile_.attacks.size()); ++i)
        if (editableProfile_.attacks[i].id == id) { profileAttackIndex_ = i; break; }
    selectedTrack_ = -1;
    authoringUndo_.clear(); authoringRedo_.clear(); authoringGestureOpen_ = false;
    profileDirty_ = false;
    EnemyAttackProfile::InvalidateCache(GetSelectedEnemyType());
    ApplyRecommendedSettings();
    profileStatus_ = "保存済みの攻撃を開き直しました: " + path;
}

void EnemyAttackPreviewWindow::RememberSequenceEdit(const AttackSequenceDefinition& before) {
    if (!authoringGestureOpen_) {
        if (authoringUndo_.size() == 64) authoringUndo_.erase(authoringUndo_.begin());
        authoringUndo_.push_back(before);
        authoringRedo_.clear();
    }
    authoringGestureOpen_ = true;
    profileDirty_ = true;
    RequestDraftRefresh();
}

void EnemyAttackPreviewWindow::RequestDraftRefresh() {
    if (!autoApplyDraft_ || !useDraftProfile_ || !enabled_ || placementPreview_) return;
    if (!draftRefreshPending_) {
        draftRefreshTime_ = elapsedTime_;
        draftRefreshPlaying_ = playing_;
    }
    playing_ = false;
    draftRefreshPending_ = true;
    draftRefreshDelay_ = 0;
}

void EnemyAttackPreviewWindow::PreviewDraft(bool keepTime) {
    const auto* attack = GetSelectedAttackDefinition();
    if (!attack || !attack->sequence.enabled || !IsCurrentSceneReady() || sceneManager_->IsPlaying()) {
        draftRefreshPending_ = false;
        profileStatus_ = "保存前の動作確認は共通攻撃を選び、編集モードで実行してください。";
        return;
    }
    if (!attack->sequence.Validate(&profileStatus_)) { draftRefreshPending_ = false; return; }
    const float time = keepTime ? (draftRefreshPending_ ? draftRefreshTime_ : elapsedTime_) : 0;
    const bool resume = keepTime ? (draftRefreshPending_ ? draftRefreshPlaying_ : playing_) : true;
    draftRefreshPending_ = false;
    draftProfile_ = editableProfile_;
    useDraftProfile_ = true;
    if (!enabled_) { enabled_ = true; CreatePreviewObjects(); }
    if (playerCopyMode_) copyActionIndex_ = 4;
    SeekPreview(time);
    playing_ = resume;
    profileStatus_ = "編集中の設定で動作確認中。JSONは保存していません。";
}

bool EnemyAttackPreviewWindow::DrawAttackAuthoringHierarchy() {
#ifdef USE_IMGUI
    if (!sceneManager_ || sceneManager_->IsPlaying()) return false;
    auto* attack = GetSelectedAttackDefinition();
    const std::string type = GetSelectedEnemyType();
    if (crownMovieMode_ || !attack || (type != "FireSlime" && type != "PrismSlime")) return false;
    ImGui::Begin("攻撃の構成###Hierarchy");
    ImGui::TextWrapped("1 攻撃を開く → 2 部品を配置 → 3 保存して動作確認");
    ImGui::TextColored(profileDirty_ ? ImVec4(1, 0.75f, 0.3f, 1) : ImVec4(0.4f, 0.9f, 0.65f, 1),
        "%s", profileDirty_ ? "未保存の変更があります" : "保存済み");
    if (ImGui::Button("保存", ImVec2(-1, 0))) SaveSelectedProfile(false);
    if (attack->sequence.enabled) {
        if (ImGui::Button("保存せずに攻撃を試す", ImVec2(-1, 0))) PreviewDraft(false);
        if (ImGui::Button("変更を同じ再生位置へ反映", ImVec2(-1, 0))) PreviewDraft(true);
        ImGui::Checkbox("調整後に自動反映（操作を離した時）", &autoApplyDraft_);
        ImGui::Checkbox("画面に弾の判定範囲を表示", &showAttackRadii_);
        if (useDraftProfile_) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "編集中データを再生中 / 未保存でも確認可能");
    }
    if (ImGui::Button("保存して攻撃を再生", ImVec2(-1, 0))) {
        profileDirty_ = true;
        SaveSelectedProfile();
        if (!profileDirty_) {
            playing_ = true;
            if (!enabled_) { enabled_ = true; CreatePreviewObjects(); }
        }
    }
    ImGui::TextWrapped("%s", profileStatus_.c_str());
    ImGui::SeparatorText("1. 既存の攻撃を開いて再編集");
    if (!playerCopyMode_ && ImGui::TreeNode("本編での攻撃の選び方")) {
        const char* modes[] = {"優先度順", "均等ランダム", "重み付きランダム"};
        int mode = editableProfile_.selectionMode == "weighted" ? 2 : editableProfile_.selectionMode == "uniform" ? 1 : 0;
        if (ImGui::Combo("共通攻撃候補の選択", &mode, modes, 3)) {
            editableProfile_.selectionMode = mode == 2 ? "weighted" : mode == 1 ? "uniform" : "priority";
            profileDirty_ = true;
        }
        ImGui::TextWrapped("本編で射程内の共通攻撃を選ぶ方式です。プレビューは選択した攻撃を固定再生します。プリズムの外側の行動サイクルは既存AIです。");
        ImGui::TreePop();
    }
    if (ImGui::BeginCombo("対象の攻撃", attack->displayName.c_str())) {
        for (int i = 0; i < static_cast<int>(editableProfile_.attacks.size()); ++i) {
            const auto& candidate = editableProfile_.attacks[i];
            if (playerCopyMode_ != (candidate.sequence.binding == "player_special")) continue;
            const char* mode = !candidate.sequence.enabled ? " [数値調整のみ]" :
                candidate.sequence.tracks.empty() ? " [弾・放出の構成編集]" : " [部品編集]";
            const std::string label = candidate.displayName + mode + "###" + candidate.id;
            if (ImGui::Selectable(label.c_str(), profileAttackIndex_ == i)) {
                StopPreview();
                profileAttackIndex_ = i;
                selectedTrack_ = -1;
                if (playerCopyMode_) copyActionIndex_ = 4;
                ApplyRecommendedSettings();
            }
        }
        ImGui::EndCombo();
    }
    attack = GetSelectedAttackDefinition();
    if (EditText("攻撃名", attack->displayName)) profileDirty_ = true;
    ImGui::TextWrapped("選択した攻撃を直接編集します。別案を作る場合は複製してください。");
    if (ImGui::Button("保存済みへ戻す")) {
        if (profileDirty_) ImGui::OpenPopup("未保存の変更を破棄しますか");
        else ReloadAuthoringProfile();
    }
    if (ImGui::BeginPopupModal("未保存の変更を破棄しますか", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("この敵の未保存の編集を破棄し、保存済みJSONを開き直します。");
        if (ImGui::Button("破棄して開き直す")) { ReloadAuthoringProfile(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("編集を続ける")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // 再読込によってvectorが置換されるため、以前の要素ポインタは使いません。
    attack = GetSelectedAttackDefinition();
    if (!attack->sequence.assetPath.empty())
        ImGui::TextWrapped("共有アセットを参照中。この攻撃の部品を変更したら、別の攻撃へ切り替える前に保存してください。個別に編集する場合は複製を使います。");
    if (attack->sequence.enabled && ImGui::Button("この攻撃を複製して編集", ImVec2(-1, 0))) {
        auto copy = *attack;
        int suffix = 1;
        do { copy.id = "custom_attack_" + std::to_string(suffix++); } while (editableProfile_.FindAttack(copy.id));
        copy.displayName += " (複製)";
        copy.sequence.assetPath.clear(); // 複製後の保存が元の共有素材を書き換えないよう独立させます。
        StopPreview();
        editableProfile_.attacks.push_back(std::move(copy));
        profileAttackIndex_ = static_cast<int>(editableProfile_.attacks.size()) - 1;
        selectedTrack_ = -1;
        profileDirty_ = true;
        profileStatus_ = "独立した攻撃として複製しました。攻撃名と部品を変更してください。";
        ImGui::End();
        return true;
    }
    // 制作機能を知らなくても、既存のC++攻撃から安全に別の攻撃を作れる入口を残します。
    auto createAttack = [&](const char* asset, const char* name) {
        EnemyAttackDefinition created;
        created.sequence.enabled = true;
        created.sequence.binding = playerCopyMode_ ? "player_special" : "enemy";
        created.sequence.duration = 1.5f;
        if (asset && !EnemyAttackProfile::LoadSequenceAsset(asset, created.sequence, &profileStatus_)) return false;
        created.sequence.assetPath.clear(); // サンプルの編集で、共通の見本ファイルを書き換えない。
        int suffix = 1;
        do { created.id = "custom_attack_" + std::to_string(suffix++); } while (editableProfile_.FindAttack(created.id));
        created.displayName = std::string(name) + " " + std::to_string(suffix - 1);
        created.minRange = 0; created.maxRange = 3;
        created.radius = 0.3f; created.maxSpeed = 14; created.lifetime = 2;
        created.recommendedTargetDistance = 1.5f;
        created.previewDuration = 3;
        created.windupDuration = 0;
        created.activeDuration = created.sequence.duration;
        created.cooldown = 2;
        for (const auto& existing : editableProfile_.attacks)
            if (existing.sequence.binding == created.sequence.binding)
                created.sequence.priority = (std::max)(created.sequence.priority, (std::min)(existing.sequence.priority, 999) + 1);
        StopPreview();
        editableProfile_.attacks.push_back(std::move(created));
        profileAttackIndex_ = static_cast<int>(editableProfile_.attacks.size()) - 1;
        selectedTrack_ = asset ? 0 : -1;
        authoringIdentity_ = editableProfile_.enemyType + "/" + editableProfile_.attacks.back().id;
        authoringUndo_.clear(); authoringRedo_.clear(); authoringGestureOpen_ = false;
        profileDirty_ = true;
        prismChantOnly_ = false; // 新しい攻撃の確認では、以前の魔法陣だけの観測設定を引き継がない。
        if (playerCopyMode_) copyActionIndex_ = 4;
        ApplyRecommendedSettings();
        profileStatus_ = "新しい攻撃を作成しました。表示部品を選ぶと中央で配置を確認できます。";
        if (selectedTrack_ >= 0 && editableProfile_.attacks.back().sequence.tracks[selectedTrack_].type == "mesh_vfx") BeginPlacementPreview();
        return true;
    };
    bool created = false;
    if (ImGui::TreeNode("新しい攻撃を作る・見本から始める")) {
    if (ImGui::Button("空の攻撃を新規作成", ImVec2(-1, 0))) created = createAttack(nullptr, "新しい攻撃");
    if (ImGui::Button("表示＋判定の例から作成", ImVec2(-1, 0)))
        created = createAttack("Resources/json/attack_assets/authoring_sweep.json", "表示と判定の攻撃");
    if (ImGui::Button("二段攻撃の例から作成", ImVec2(-1, 0)))
        created = createAttack("Resources/json/attack_assets/authoring_double_sweep.json", "二段の攻撃");
    if (ImGui::Button("本体の動き＋接触分岐の例から作成", ImVec2(-1, 0)))
        created = createAttack("Resources/json/attack_assets/authoring_contact_combo.json", "接触で追撃する攻撃");
    if (ImGui::Button("照準・発射・追尾の例から作成", ImVec2(-1, 0)))
        created = createAttack("Resources/json/attack_assets/authoring_homing.json", "追尾する攻撃Object");
    ImGui::TreePop();
    }
    if (created) { ImGui::End(); return true; }
    attack = GetSelectedAttackDefinition();
    if (attack->id.rfind("custom_attack_", 0) == 0 && ImGui::Button("この作成した攻撃を削除")) {
        StopPreview();
        editableProfile_.attacks.erase(editableProfile_.attacks.begin() + profileAttackIndex_);
        profileAttackIndex_ = 0;
        for (int i = 0; i < static_cast<int>(editableProfile_.attacks.size()); ++i) {
            if (playerCopyMode_ == (editableProfile_.attacks[i].sequence.binding == "player_special")) {
                profileAttackIndex_ = i;
                break;
            }
        }
        selectedTrack_ = -1;
        authoringIdentity_.clear();
        profileDirty_ = true;
        profileStatus_ = "作成した攻撃を削除しました。保存するまでファイルには反映されません。";
        ImGui::End();
        return true;
    }
    const std::string identity = editableProfile_.enemyType + "/" + attack->id;
    if (authoringIdentity_ != identity) {
        authoringIdentity_ = identity;
        selectedTrack_ = -1;
        authoringUndo_.clear(); authoringRedo_.clear(); authoringGestureOpen_ = false;
    }
    if (!ImGui::IsAnyItemActive()) authoringGestureOpen_ = false;
    ImGui::TextWrapped("%s", attack->displayName.c_str());
    if (!attack->sequence.enabled) {
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "現在は観測・数値調整の対象です");
        ImGui::TextWrapped("この既存攻撃は部品編集へ未移行です。画面の槍や魔法陣を名前で選び、配置や表示時間を再編集する一覧はまだありません。右側で変更できるのは攻撃全体の数値です。");
        ImGui::TextWrapped("例えば、表示を0～1秒、判定を0.3～0.5秒にする攻撃を、別の攻撃として作れます。");
        if (ImGui::Button("この攻撃の設定を保存して観測", ImVec2(-1, 0))) {
            profileDirty_ = true;
            SaveSelectedProfile();
            if (!profileDirty_) {
                playing_ = true;
                if (!enabled_) { enabled_ = true; CreatePreviewObjects(); }
            }
        }
        ImGui::TextWrapped("%s", profileStatus_.c_str());
        ImGui::End();
        return true;
    }
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.65f, 1), "%s", attack->sequence.tracks.empty() ?
        "弾の構成編集: 本体・判定・軌跡・着弾" : "部品編集: 表示物・判定・本体の時間を編集できます");
    ImGui::SeparatorText("2. 部品を選んで配置・再編集");
    if (ImGui::Selectable("攻撃全体の設定", selectedTrack_ == -1 && !authoringActorSelected_)) { selectedTrack_ = -1; authoringActorSelected_ = false; StopPlacementPreview(); }
    if (ImGui::Selectable((std::string("術者（参照）: ") + GetSelectedEnemyType()).c_str(), authoringActorSelected_)) {
        selectedTrack_ = -1; StopPlacementPreview(); authoringActorSelected_ = true;
    }
    ImGui::TextWrapped("術者と生成Objectは別です。下の部品を選ぶと単独表示と配置ギズモを開きます。");
    ImGui::TextWrapped("表示部品を選択 → 中央に表示 → 右のInspectorで素材・位置・大きさ・時間を変更。下のタイムラインで表示区間を調整できます。");
    ImGui::TextDisabled("表示物と判定は独立した部品です。");
    auto& sequence = attack->sequence;
    if (!sequence.events.empty()) {
        ImGui::SeparatorText("攻撃の構成（選択して再編集）");
        for (int i = 0; i < static_cast<int>(sequence.events.size()); ++i) {
            const auto& event = sequence.events[i];
            const std::string name = (event.enabled ? "" : "[無効] ") + std::to_string(i + 1) + ": " +
                (event.name.empty() ? (event.operation == "projectile" ? "火球 / 弾" : "GPU放出: " + event.preset) : event.name);
            // -1は全体設定、-2以下は命令番号を表します。部品番号(0以上)と区別します。
            if (ImGui::Selectable(name.c_str(), selectedTrack_ == -2 - i)) {
                selectedTrack_ = -2 - i;
                projectileSection_ = 0;
                BeginPlacementPreview();
            }
            if (event.operation == "projectile") {
                ImGui::PushID(i);
                ImGui::Indent(14);
                const char* children[] = {"発射・移動", "弾Objectの見た目", "当たり判定", "飛行中の軌跡", "着弾エフェクト"};
                for (int section = 1; section < 5; ++section) {
                    if (ImGui::Selectable(children[section], selectedTrack_ == -2 - i && projectileSection_ == section)) {
                        selectedTrack_ = -2 - i;
                        projectileSection_ = section;
                        BeginPlacementPreview();
                    }
                }
                ImGui::Unindent(14);
                ImGui::PopID();
            }
        }
        ImGui::TextWrapped("生成Objectの下の項目を選び、右側で見た目・判定・軌跡を編集します。各項目は同じ弾の構成設定です。");
    }
    if (!playerCopyMode_ && editableProfile_.selectionMode == "weighted") {
        if (ImGui::DragFloat("この攻撃の抽選重み", &attack->selectionWeight, 0.1f, 0, 100000)) profileDirty_ = true;
        ImGui::TextWrapped("例: 同じ射程の候補の重みが3と1なら75%%と25%%。0は本編の抽選から除外します。");
    }
    const auto before = sequence;
    ImGui::Text("生成・放出 %d / 時間区間を持つ部品 %d", static_cast<int>(sequence.events.size()), static_cast<int>(sequence.tracks.size()));
    if (ImGui::Button("＋ 部品を追加（素材から選択）", ImVec2(-1,0))) ImGui::OpenPopup("攻撃部品を追加");
    if (ImGui::BeginPopup("攻撃部品を追加")) {
        static int kind = 0;
        const char* kinds[] = {"生成Object（モデル）", "GPU素材の放出", "表示素材（MeshEffect）", "当たり判定", "術者アニメーション"};
        ImGui::Combo("部品の種類", &kind, kinds, 5);
        std::string picked;
        bool add = false;
        if (kind == 0 && ImGui::BeginCombo("モデルを選択", "素材を選ぶと追加します")) {
            for (const auto& model : ModelManager::GetInstance()->GetAvailableModelNames())
                if (ImGui::Selectable(model.c_str())) { picked = model; add = true; }
            ImGui::EndCombo();
        } else if (kind == 1) add = SelectJson("GPU素材", "Resources/json/gpu_particles", picked);
        else if (kind == 2) add = SelectJson("表示素材", "Resources/json/effect", picked, authoringAssets_);
        else if (kind == 3) add = ImGui::Button("球の判定を追加");
        else if (kind == 4) add = SelectJson("術者の動き", "Resources/json/animation_clip", picked);
        if (add) {
            if ((kind < 2 && sequence.events.size() >= 128) || (kind >= 2 && sequence.tracks.size() >= 64))
                profileStatus_ = "部品数の上限に達しています。";
            else {
                if (kind < 2) {
                    AttackSequenceEvent event;
                    event.name = std::filesystem::path(picked).stem().string();
                    event.up = 1; event.forward = 1;
                    if (kind == 0) {
                        event.projectile = ResolveProjectileComponent(*attack,event);
                        event.projectile.model = picked; event.projectile.style = "plain";
                        event.projectile.lifetime = (std::max)(0.1f,event.projectile.lifetime);
                        event.projectile.appearance.red = event.projectile.appearance.green = event.projectile.appearance.blue = 1;
                        event.projectile.appearance.alpha = event.projectile.appearance.emissive = 1;
                        event.projectile.trail.clear(); event.projectile.secondaryTrail.clear();
                        event.projectile.impact.clear(); event.projectile.secondaryImpact.clear();
                    } else { event.operation = "gpu_vfx"; event.preset = event.name; }
                    sequence.events.push_back(event);
                    selectedTrack_ = -1 - static_cast<int>(sequence.events.size()); projectileSection_ = kind == 0 ? 1 : 0;
                } else {
                    AttackSequenceTrack track;
                    track.type = kind == 2 ? "mesh_vfx" : kind == 3 ? "hitbox" : "body_animation";
                    track.name = kind == 3 ? "攻撃判定" : std::filesystem::path(picked).stem().string();
                    track.preset = picked; track.end = (std::max)(0.01f,sequence.duration); sequence.duration = track.end;
                    track.keys.front().position = {0,1,1}; sequence.tracks.push_back(track);
                    selectedTrack_ = static_cast<int>(sequence.tracks.size()) - 1;
                }
                RememberSequenceEdit(before);
                authoringActorSelected_ = false;
                if (kind != 4) BeginPlacementPreview(); else StopPlacementPreview();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::TextWrapped("選んだ素材を追加して配置確認を開きます。生成Objectの見た目と術者モデルは独立しています。");
        ImGui::EndPopup();
    }
    auto hasParent = [&](int i) {
        const auto& track = sequence.tracks[i];
        return track.type == "hitbox" && track.parentTrack >= 0 && track.parentTrack < i &&
            sequence.tracks[track.parentTrack].type == "mesh_vfx";
    };
    auto drawPart = [&](int i, bool child) {
        ImGui::PushID(i);
        const auto& track = sequence.tracks[i];
        if (child) ImGui::Indent(16);
        std::string label = (track.parentTrack != -1 && !hasParent(i) ? "[親参照を修正] " : "") +
            std::string(track.enabled ? "" : "[無効] ") + track.name +
            (track.type == "hitbox" ? " [判定]" : track.type == "body_animation" ? " [本体]" : " [表示]");
        if (ImGui::Selectable(label.c_str(), selectedTrack_ == i)) {
            selectedTrack_ = i;
            if (track.type != "body_animation") BeginPlacementPreview();
            else { authoringActorSelected_ = false; StopPlacementPreview(); }
        }
        if (child) ImGui::Unindent(16);
        ImGui::PopID();
    };
    for (int i = 0; i < static_cast<int>(sequence.tracks.size()); ++i) {
        if (hasParent(i)) continue;
        drawPart(i, false);
        for (int j = i + 1; j < static_cast<int>(sequence.tracks.size()); ++j)
            if (hasParent(j) && sequence.tracks[j].parentTrack == i) drawPart(j, true);
    }
    if (ImGui::Button("元に戻す") && !authoringUndo_.empty()) {
        StopPlacementPreview();
        selectedTrack_ = -1;
        authoringRedo_.push_back(sequence); sequence = authoringUndo_.back(); authoringUndo_.pop_back(); profileDirty_ = true;
        RequestDraftRefresh();
    }
    ImGui::SameLine();
    if (ImGui::Button("やり直す") && !authoringRedo_.empty()) {
        StopPlacementPreview();
        selectedTrack_ = -1;
        authoringUndo_.push_back(sequence); sequence = authoringRedo_.back(); authoringRedo_.pop_back(); profileDirty_ = true;
        RequestDraftRefresh();
    }
    ImGui::Separator();
    ImGui::SeparatorText("3. 保存して動作確認");
    ImGui::TextWrapped("配置確認は未保存の表示部品だけを映します。追尾・接触・分岐は上の「保存して攻撃を再生」で確認します。共有アセットの保存は他の利用先にも反映されます。");
    ImGui::TextWrapped("保存した攻撃は本編の候補にも追加され、射程と設定した選択方式で選ばれます。");
    if (ImGui::Button("保存して再生", ImVec2(-1, 0))) {
        profileDirty_ = true;
        SaveSelectedProfile();
        if (!profileDirty_) {
            playing_ = true;
            if (!enabled_) { enabled_ = true; CreatePreviewObjects(); }
        }
    }
    ImGui::TextWrapped("%s", profileStatus_.c_str());
    ImGui::End();
    return true;
#else
    return false;
#endif
}

void EnemyAttackPreviewWindow::DrawTrackInspector() {
#ifdef USE_IMGUI
    DrawPlacementControls();
    auto* attack = GetSelectedAttackDefinition();
    if (!attack) return;
    auto& sequence = attack->sequence;
    const auto before = sequence;
    bool changed = false;
    if (ImGui::TreeNode("詳細: 共有攻撃アセット")) {
        std::string path = sequence.assetPath;
        if (SelectJson("既存アセットを割り当て", "Resources/json/attack_assets", path)) {
            if (EnemyAttackProfile::LoadSequenceAsset(path, sequence, &profileStatus_)) changed = true;
        }
        EditText("新規アセット名 (英数字・_・-)", authoringAssetName_);
        if (ImGui::Button("現在の内容を新規アセットとして保存")) {
            const bool valid = !authoringAssetName_.empty() && std::all_of(authoringAssetName_.begin(), authoringAssetName_.end(),
                [](unsigned char c) { return c < 128 && (std::isalnum(c) || c == '_' || c == '-'); });
            path = "Resources/json/attack_assets/" + authoringAssetName_ + ".json";
            std::error_code ec;
            if (!valid) profileStatus_ = "アセット名は英数字・_・-にしてください。";
            else if (std::filesystem::exists(path, ec)) profileStatus_ = "同名のアセットがあります。別名を指定してください。";
            else if (EnemyAttackProfile::SaveSequenceAsset(path, sequence, &profileStatus_)) {
                sequence.assetPath = path; changed = true;
                profileStatus_ = "アセットを作成しました。プロファイルを保存すると割り当ても保存されます。";
            }
        }
        if (!sequence.assetPath.empty()) {
            ImGui::TextWrapped("共有先: %s", sequence.assetPath.c_str());
            if (ImGui::Button("共有を解除して埋め込みにする")) { sequence.assetPath.clear(); changed = true; }
        }
        ImGui::TreePop();
    }
    if (selectedTrack_ >= 0 && selectedTrack_ < static_cast<int>(sequence.tracks.size())) {
        auto& track = sequence.tracks[selectedTrack_];
        ImGui::SeparatorText("選択した攻撃部品");
        changed |= EditText("部品名", track.name);
        for (int i = 0; track.type != "body_animation" && i < static_cast<int>(track.keys.size()); ++i) {
            ImGui::PushID(i);
            auto& key = track.keys[i];
            ImGui::Separator();
            changed |= ImGui::DragFloat("キー時刻 (部品開始から)", &key.time, 0.01f, 0, (std::max)(0.0f, track.end - track.begin));
            changed |= ImGui::DragFloat3("位置 (右・上・前)", key.position.data(), 0.01f, -1000, 1000);
            if (track.type == "mesh_vfx") {
                changed |= ImGui::DragFloat3("回転 (ラジアン)", key.rotation.data(), 0.01f, -1000, 1000);
                changed |= ImGui::DragFloat3("スケール", key.scale.data(), 0.01f, 0.001f, 100);
            }
            const char* easing[] = {"保持", "線形補間", "Smoothstep"};
            changed |= ImGui::Combo("次のキーへの補間", &key.interpolation, easing, 3);
            if (track.keys.size() > 1 && ImGui::Button("キーを削除")) {
                track.keys.erase(track.keys.begin() + i); changed = true; ImGui::PopID(); break;
            }
            ImGui::PopID();
        }
        if (track.type != "body_animation" && track.keys.size() < 128 && ImGui::Button("キーを追加")) {
            auto key = track.keys.back();
            key.time = (std::min)(track.end - track.begin, key.time + 0.1f);
            if (key.time > track.keys.back().time) { track.keys.push_back(key); changed = true; }
        }

        changed |= ImGui::Checkbox("有効", &track.enabled);
        if (track.type != "body_animation") changed |= ImGui::Checkbox("術者に追従", &track.followOwner);
        changed |= ImGui::DragFloat("表示・判定開始", &track.begin, 0.01f, 0, sequence.duration, "%.3f 秒");
        changed |= ImGui::DragFloat("表示・判定終了", &track.end, 0.01f, 0, sequence.duration, "%.3f 秒");
        const char* ids[] = {"always", "grounded", "airborne", "target_in_range", "contact", "no_contact", "track_completed"};
        const char* labels[] = {"常時", "接地中", "空中", "標的が射程内", "指定判定の終了後: 接触あり", "指定判定の終了後: 接触なし", "指定判定の終了後: 結果を問わない"};
        int condition = 0;
        for (int i = 0; i < 7; ++i) if (track.condition == ids[i]) condition = i;
        if (ImGui::Combo("開始条件", &condition, labels, 7)) { track.condition = ids[condition]; changed = true; }
        if (condition >= 4) {
            const char* sourceName = track.conditionTrack >= 0 && track.conditionTrack < static_cast<int>(sequence.tracks.size())
                ? sequence.tracks[track.conditionTrack].name.c_str() : "選択してください";
            if (ImGui::BeginCombo("分岐元の判定", sourceName)) {
                for (int i = 0; i < static_cast<int>(sequence.tracks.size()); ++i) {
                    const auto& source = sequence.tracks[i];
                    if (i == selectedTrack_ || source.type != "hitbox") continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(source.name.c_str(), track.conditionTrack == i)) { track.conditionTrack = i; changed = true; }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("分岐元の終了以降に開始してください。接触はHP減少の確定ではありません。元が条件不成立で省略された場合、後続も省略します。");
        }
        if (track.type == "mesh_vfx") {
            if (SelectJson("表示プリセット", "Resources/json/effect", track.preset, authoringAssets_)) {
                changed = true;
                if (!placementPreview_) BeginPlacementPreview();
            }
            const char* modes[] = {"キーで動かす", "照準して直進", "照準して追尾"};
            int motion = track.motion == "homing" ? 2 : track.motion == "aim_launch" ? 1 : 0;
            if (ImGui::Combo("共通動作", &motion, modes, 3)) {
                track.motion = motion == 2 ? "homing" : motion == 1 ? "aim_launch" : "keyframed";
                track.launchDelay = (std::min)(track.launchDelay, (track.end - track.begin) * 0.5f);
                changed = true;
            }
            if (motion != 0) {
                changed |= ImGui::DragFloat("発射まで (部品開始から秒)", &track.launchDelay, 0.01f, 0, 30);
                changed |= ImGui::DragFloat("移動速度 (m/s)", &track.moveSpeed, 0.1f, 0, 1000);
                changed |= ImGui::DragFloat("狙う高さ (標的原点からm)", &track.targetHeight, 0.01f, 0, 1000);
                if (motion == 2) {
                    changed |= ImGui::DragFloat("旋回速度 (度/秒)", &track.turnDegreesPerSecond, 1, 0, 1000);
                    changed |= ImGui::DragFloat("発射後の追尾時間 (秒)", &track.homingDuration, 0.01f, 0, 30);
                }
                ImGui::TextWrapped("先頭キーの位置に生成し、発射までは狙い続けます。発射後はキーの位置を使わず共通動作で移動します。回転は+Zを前として合わせます。");
            }
            if (sequence.tracks.size() < 64 && ImGui::Button("このObjectに判定を追加")) {
                AttackSequenceTrack child;
                child.name = track.name + " の判定"; child.type = "hitbox"; child.parentTrack = selectedTrack_;
                child.begin = track.begin + (motion != 0 ? track.launchDelay : 0); child.end = track.end;
                sequence.tracks.push_back(child);
                selectedTrack_ = static_cast<int>(sequence.tracks.size()) - 1;
                RememberSequenceEdit(before);
                return; // push_back後のtrack参照を使わない。
            }
        }
        else if (track.type == "body_animation") {
            changed |= SelectJson("本体クリップ", "Resources/json/animation_clip", track.preset);
            changed |= ImGui::DragFloat("クリップ再生速度", &track.playbackSpeed, 0.01f, 0.01f, 10);
            changed |= ImGui::DragFloat("クリップ開始位置 (秒)", &track.clipOffset, 0.01f, 0, 300);
            changed |= ImGui::Checkbox("クリップをループ", &track.loop);
            changed |= ImGui::DragFloat("入りのブレンド (秒)", &track.blendIn, 0.01f, 0, 30);
            changed |= ImGui::DragFloat("終わりのブレンド (秒)", &track.blendOut, 0.01f, 0, 30);
            ImGui::TextWrapped("Animation Workbenchで保存した本体変形を描画だけに重ねます。移動・判定や子オブジェクトは動かしません。キーの編集はWorkbenchで行います。");
        } else {
            const char* parentName = track.parentTrack >= 0 && track.parentTrack < static_cast<int>(sequence.tracks.size())
                ? sequence.tracks[track.parentTrack].name.c_str() : "親なし / 要確認";
            if (ImGui::BeginCombo("追従するObject", parentName)) {
                if (ImGui::Selectable("親なし", track.parentTrack == -1)) { track.parentTrack = -1; changed = true; }
                for (int i = 0; i < selectedTrack_; ++i) {
                    if (sequence.tracks[i].type != "mesh_vfx") continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(sequence.tracks[i].name.c_str(), track.parentTrack == i)) { track.parentTrack = i; changed = true; }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("親を指定すると表示Objectの位置へ追従します。判定キーの位置は親からのオフセットです。親の表示区間内に判定区間を収めてください。");
            changed |= ImGui::DragFloat("球の判定半径", &track.radius, 0.01f, 0.01f, 100);
            changed |= ImGui::DragFloat("ダメージ倍率", &track.damageScale, 0.01f, 0, 100);
            ImGui::TextWrapped("1回の攻撃で各対象に1回命中します。表示スケールは判定半径へ影響しません。");
        }
        if (ImGui::Button("部品を複製") && sequence.tracks.size() < 64) {
            auto copy = track; copy.name += " コピー"; sequence.tracks.push_back(copy); changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("部品を削除")) {
            StopPlacementPreview();
            sequence.tracks.erase(sequence.tracks.begin() + selectedTrack_);
            // 配列の詰め直しで別の判定を参照しない。削除された参照は未指定に戻して保存時に検出します。
            for (auto& other : sequence.tracks) {
                if (other.parentTrack == selectedTrack_) other.parentTrack = -2;
                else if (other.parentTrack > selectedTrack_) --other.parentTrack;
                if (other.conditionTrack == selectedTrack_) other.conditionTrack = -1;
                else if (other.conditionTrack > selectedTrack_) --other.conditionTrack;
            }
            selectedTrack_ = -1; changed = true;
        }
    } else ImGui::TextDisabled("左側の攻撃の構成から表示物・判定を追加または選択してください。");
    if (changed) RememberSequenceEdit(before);
#endif
}

void EnemyAttackPreviewWindow::DrawProjectileInspector() {
#ifdef USE_IMGUI
    DrawPlacementControls();
    auto* attack = GetSelectedAttackDefinition();
    const int index = -2 - selectedTrack_;
    if (!attack || index < 0 || index >= static_cast<int>(attack->sequence.events.size())) return;
    auto& sequence = attack->sequence;
    const auto before = sequence;
    auto& event = sequence.events[index];
    auto part = ResolveProjectileComponent(*attack, event);
    bool changed = false;
    ImGui::TextWrapped("%s / %s", attack->displayName.c_str(), event.name.empty() ? "名称未設定" : event.name.c_str());
    if (ImGui::Button("保存せず動作確認")) PreviewDraft(false);
    ImGui::SameLine();
    if (ImGui::Button("同じ位置で見直す")) PreviewDraft(true);
    ImGui::TextWrapped("自動反映ONでは入力を離した後に再生位置を再構築します。保存するまで本編のJSONは変わりません。");
    changed |= EditText("構成名", event.name);
    changed |= ImGui::Checkbox("この弾 / 放出を有効にする", &event.enabled);
    changed |= ImGui::DragFloat("攻撃データの長さ", &sequence.duration, 0.01f, 0, 30, "%.3f 秒");
    changed |= ImGui::DragFloat("発生時刻", &event.time, 0.01f, 0, sequence.duration, "%.3f 秒");
    auto gpuPreset = [&](const char* label, std::string& name) {
        ImGui::PushID(label);
        std::string path = name.empty() ? "" : "Resources/json/gpu_particles/" + name + ".json";
        bool edited = SelectJson(label, "Resources/json/gpu_particles", path);
        if (edited) name = std::filesystem::path(path).stem().string();
        if (ImGui::SmallButton("なしにする")) { name.clear(); edited = true; }
        ImGui::PopID();
        return edited;
    };
    if (event.operation == "gpu_vfx") {
        changed |= gpuPreset("GPUエフェクト", event.preset);
        changed |= ImGui::Checkbox("発射方向へ放出", &event.directed);
        changed |= ImGui::DragFloat("粒子速度倍率", &event.particleSpeedScale, 0.01f, 0, 100);
    } else {
        const char* sections[] = {"発射・移動", "弾Objectの見た目", "当たり判定", "飛行中の軌跡", "着弾エフェクト"};
        ImGui::Combo("編集する構成", &projectileSection_, sections, 5);
        ImGui::SeparatorText(sections[projectileSection_]);
        if (projectileSection_ == 0) {
            changed |= ImGui::SliderInt("一度に出す弾の数", &event.count, 1, 32);
            changed |= ImGui::DragFloat("扇状に広がる角度", &event.spreadDegrees, 0.5f, 0, 360, "%.1f 度");
            changed |= ImGui::DragFloat("最低速度", &part.minSpeed, 0.1f, 0, 1000, "%.2f m/s");
            changed |= ImGui::DragFloat("最高速度", &part.maxSpeed, 0.1f, 0, 1000, "%.2f m/s");
            changed |= ImGui::DragFloat("弾が残る時間", &part.lifetime, 0.01f, 0.01f, 1000, "%.2f 秒");
            changed |= ImGui::DragFloat("距離に掛ける速度倍率 (0=最高速度)", &event.distanceSpeedScale, 0.01f, 0, 1000);
            changed |= ImGui::DragFloat("速度への加算", &event.distanceSpeedBias, 0.05f, -1000, 1000);
            changed |= ImGui::DragFloat("上向き速度の追加", &event.verticalSpeed, 0.05f, -1000, 1000);
        } else if (projectileSection_ == 1) {
            if (ImGui::BeginCombo("モデル", part.model.c_str())) {
                for (const auto& model : ModelManager::GetInstance()->GetAvailableModelNames()) {
                    if (ImGui::Selectable(model.c_str(), model == part.model)) {
                        part.model = model;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            int style = part.style == "plain" ? 1 : 0;
            const char* styles[] = {"炎の描画", "通常のモデル"};
            if (ImGui::Combo("見た目", &style, styles, 2)) { part.style = style ? "plain" : "fire"; changed = true; }
            changed |= ImGui::DragFloat("表示スケール (判定は変わりません)", &part.visualScale, 0.01f, 0.01f, 20);
            auto& a = part.appearance;
            float color[] = {a.red, a.green, a.blue, a.alpha};
            if (ImGui::ColorEdit4("色・透明度", color)) { a.red = color[0]; a.green = color[1]; a.blue = color[2]; a.alpha = color[3]; changed = true; }
            changed |= ImGui::DragFloat("発光", &a.emissive, 0.01f, 0, 100);
            changed |= ImGui::DragFloat("炎の広がり", &a.effectScale, 0.01f, 0, 100);
            changed |= ImGui::DragFloat("炎の境界の柔らかさ", &a.softness, 0.01f, 0, 1);
            changed |= ImGui::DragFloat("炎の強さ", &a.intensity, 0.01f, 0, 100);
            changed |= ImGui::DragFloat("炎の板の広がり", &a.billboardScale, 0.01f, 0, 100);
        } else if (projectileSection_ == 2) {
            changed |= ImGui::Checkbox("接触判定を有効にする", &part.collisionEnabled);
            changed |= ImGui::DragFloat("判定球の半径 (ワールド)", &part.radius, 0.01f, 0.01f, 1000, "%.3f m");
            changed |= ImGui::DragFloat("この弾のダメージ", &part.damage, 0.05f, 0, 1000);
            ImGui::TextWrapped("判定OFFでは地形・対象に衝突せず寿命まで飛びます。状態異常は攻撃全体の設定を使います。画面の輪は実際の判定半径です。");
        } else if (projectileSection_ == 3) {
            changed |= gpuPreset("軌跡の主エフェクト", part.trail);
            changed |= gpuPreset("軌跡の補助エフェクト", part.secondaryTrail);
            changed |= ImGui::DragFloat("軌跡の発生間隔", &part.appearance.trailInterval, 0.001f, 0.01f, 100, "%.3f 秒");
            changed |= ImGui::DragFloat("軌跡の速度倍率", &part.appearance.trailSpeedScale, 0.01f, 0, 100);
        } else {
            changed |= gpuPreset("着弾の主エフェクト", part.impact);
            changed |= gpuPreset("着弾の補助エフェクト", part.secondaryImpact);
            ImGui::TextWrapped("衝突・着弾時に放出します。判定OFFや寿命による自然消滅では放出しません。");
        }
    }
    if (event.operation == "gpu_vfx" || projectileSection_ == 0) {
        float offset[] = {event.right, event.up, event.forward};
        if (ImGui::DragFloat3("発生位置 (右・上・前)", offset, 0.01f, -1000, 1000)) { event.right = offset[0]; event.up = offset[1]; event.forward = offset[2]; changed = true; }
        float size[] = {event.sizeRight, event.sizeUp, event.sizeForward};
        if (ImGui::DragFloat3("術者サイズに比例する位置", size, 0.01f, -1000, 1000)) { event.sizeRight = size[0]; event.sizeUp = size[1]; event.sizeForward = size[2]; changed = true; }
        changed |= ImGui::Checkbox("標的の方へ向ける", &event.aimAtTarget);
        changed |= ImGui::DragFloat("狙う高さ", &event.targetHeight, 0.01f, -1000, 1000);
        changed |= ImGui::DragFloat("水平回転", &event.yawDegrees, 0.5f, -360, 360, "%.1f 度");
        const char* ids[] = {"always", "grounded", "airborne", "target_in_range"};
        const char* labels[] = {"常時", "接地中", "空中", "標的が射程内"};
        int condition = 0;
        for (int i = 0; i < 4; ++i) if (event.condition == ids[i]) condition = i;
        if (ImGui::Combo("発生条件", &condition, labels, 4)) { event.condition = ids[condition]; changed = true; }
        changed |= ImGui::Checkbox("条件不成立なら後続も中止", &event.stopOnFalse);
    }
    if (changed && event.operation == "projectile") event.projectile = part;
    if (ImGui::Button("この構成を複製") && sequence.events.size() < 128) {
        auto copy = event; copy.name += " (複製)";
        sequence.events.push_back(std::move(copy));
        selectedTrack_ = -1 - static_cast<int>(sequence.events.size());
        RememberSequenceEdit(before);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("この構成を削除")) {
        StopPlacementPreview();
        sequence.events.erase(sequence.events.begin() + index);
        selectedTrack_ = -1;
        RememberSequenceEdit(before);
        return;
    }
    if (changed) RememberSequenceEdit(before);
    std::string error;
    if (!sequence.Validate(&error)) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%s", error.c_str());
    ImGui::TextWrapped("%s", profileStatus_.c_str());
#endif
}

void EnemyAttackPreviewWindow::DrawTrackTimeline() {
#ifdef USE_IMGUI
    auto* attack = GetSelectedAttackDefinition();
    if (!attack) return;
    if (!attack->sequence.enabled) {
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "観測モード: この下は既存攻撃の計測グラフです");
        ImGui::TextWrapped("新しい表示物や判定の制作は、炎スライム／プリズムの左パネルから別の攻撃を作成して行います。");
        return;
    }
    auto& sequence = attack->sequence;
    ImGui::SeparatorText("攻撃内容の時間軸 (共通ランタイム開始 = 0秒)");
    ImGui::TextDisabled("棒の中央で移動、左右端で区間を変更。移動キーはInspectorで編集します。");
    const float width = (std::max)(100.0f, ImGui::GetContentRegionAvail().x - 160);
    const float duration = (std::max)(0.01f, sequence.duration);
    if (sequence.actorControl.enabled) {
        const auto before = sequence;
        bool changed = false;
        ImGui::TextUnformatted("術者の予備動作・照準");
        changed |= ImGui::SliderFloat("照準固定##timeline", &sequence.actorControl.aimLockTime, 0, duration, "%.3f 秒");
        changed |= ImGui::SliderFloat("予備動作終了##timeline", &sequence.actorControl.telegraphEnd, 0, duration, "%.3f 秒");
        ImGui::Text("後隙終了 %.3f 秒 / この後から再使用待ち %.3f 秒", sequence.duration, attack->cooldown);
        if (changed) RememberSequenceEdit(before);
    }
    if (!sequence.events.empty() && ImGui::TreeNodeEx("弾・GPU放出の発生時刻", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < static_cast<int>(sequence.events.size()); ++i) {
            ImGui::PushID(i);
            auto& event = sequence.events[i];
            const std::string label = event.name.empty() ? (event.operation == "projectile" ? "弾の発射" : "GPU放出") : event.name;
            if (ImGui::Selectable(label.c_str(), selectedTrack_ == -2 - i, 0, ImVec2(160, 0))) {
                selectedTrack_ = -2 - i; projectileSection_ = 0; BeginPlacementPreview();
            }
            ImGui::SameLine();
            const auto before = sequence;
            ImGui::SetNextItemWidth(width);
            if (ImGui::SliderFloat("##発生時刻", &event.time, 0, duration, "%.3f 秒")) RememberSequenceEdit(before);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    for (int i = 0; i < static_cast<int>(sequence.tracks.size()); ++i) {
        ImGui::PushID(i);
        auto& track = sequence.tracks[i];
        if (!track.Validate(duration, nullptr)) {
            ImGui::Text("%s: 区間またはキーをInspectorで修正してください。", track.name.c_str());
            ImGui::PopID();
            continue;
        }
        ImGui::TextUnformatted(track.name.c_str()); ImGui::SameLine(160);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float x0 = origin.x + track.begin / duration * width, x1 = origin.x + track.end / duration * width;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + width, origin.y + 23}, IM_COL32(30, 35, 42, 255));
        draw->AddRectFilled({x0, origin.y + 2}, {x1, origin.y + 21}, track.type == "hitbox" ? IM_COL32(210, 80, 60, 255) :
            track.type == "body_animation" ? IM_COL32(155, 100, 210, 255) : IM_COL32(60, 150, 210, 255));
        ImGui::InvisibleButton("track", {width, 23});
        if (ImGui::IsItemClicked()) {
            selectedTrack_ = i;
            const float mouse = ImGui::GetIO().MousePos.x;
            authoringDragMode_ = std::abs(mouse - x0) < 8 ? 1 : (std::abs(mouse - x1) < 8 ? 2 : 0);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            const auto before = sequence;
            const float delta = ImGui::GetIO().MouseDelta.x / width * duration;
            const float oldLength = track.end - track.begin;
            if (authoringDragMode_ == 1) track.begin = std::clamp(track.begin + delta, 0.0f, (std::max)(0.0f, track.end - 0.001f));
            else if (authoringDragMode_ == 2) track.end = std::clamp(track.end + delta, track.begin + 0.001f, duration);
            else {
                const float length = track.end - track.begin;
                track.begin = std::clamp(track.begin + delta, 0.0f, (std::max)(0.0f, duration - length));
                track.end = track.begin + length;
            }
            if (authoringDragMode_ != 0)
                for (auto& key : track.keys) key.time *= (track.end - track.begin) / oldLength;
            RememberSequenceEdit(before);
        }
        ImGui::PopID();
    }
#endif
}

void EnemyAttackPreviewWindow::DrawAttackOverlay(float x, float y, float width, float height) {
#ifdef USE_IMGUI
    if (IsAuthoringViewportActive()) DrawPlacementGizmo(x, y, width, height);
    if (!enabled_ || !showAttackRadii_ || !sceneManager_ || sceneManager_->IsPlaying() || crownMovieMode_) return;
    const auto* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera || width <= 0 || height <= 0) return;
    const auto& matrix = camera->GetViewProjectionMatrix();
    auto project = [&](Vector3 p, ImVec2& screen) {
        const auto& m = matrix.m;
        const float w = p.x*m[0][3] + p.y*m[1][3] + p.z*m[2][3] + m[3][3];
        if (w <= 0.001f) return false;
        const float z = (p.x*m[0][2] + p.y*m[1][2] + p.z*m[2][2] + m[3][2]) / w;
        if (z < 0 || z > 1) return false;
        screen = {x + (1 + (p.x*m[0][0] + p.y*m[1][0] + p.z*m[2][0] + m[3][0])/w)*width*0.5f,
            y + (1 - (p.x*m[0][1] + p.y*m[1][1] + p.z*m[2][1] + m[3][1])/w)*height*0.5f};
        return true;
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({x, y}, {x + width, y + height}, true);
    for (const auto& bullet : BulletManager::GetInstance()->GetBullets()) {
        if (!bullet || bullet->IsDead()) continue;
        const auto position = bullet->GetTranslate();
        const float radius = bullet->GetCollisionRadius();
        const ImU32 color = bullet->GetCollisionMask() ? IM_COL32(255, 215, 40, 255) : IM_COL32(155, 155, 155, 200);
        // 実Colliderの半径を3つの大円で表示。編集用の飾り半径とは別に計算しません。
        for (int plane = 0; plane < 3; ++plane) {
            for (int i = 0; i < 48; ++i) {
                auto point = [&](int segment) {
                    const float angle = segment * 6.28318530718f / 48;
                    const float a = radius * std::cos(angle), b = radius * std::sin(angle);
                    return position + (plane == 0 ? Vector3{a,b,0} : plane == 1 ? Vector3{a,0,b} : Vector3{0,a,b});
                };
                ImVec2 a, b;
                if (project(point(i), a) && project(point(i+1), b)) draw->AddLine(a, b, color, 1.5f);
            }
        }
        ImVec2 screen;
        if (project(position + Vector3{0, radius, 0}, screen)) {
            const std::string label = bullet->GetName() + (bullet->GetCollisionMask() ? " [判定ON]" : " [判定OFF]");
            draw->AddText(screen, color, label.c_str());
        }
    }
    draw->PopClipRect();
#endif
}

EnemyAttackPreviewWindow::AuthoringSelectionKind EnemyAttackPreviewWindow::GetAuthoringSelectionKind() const {
    if (authoringActorSelected_) return AuthoringSelectionKind::Actor;
    return selectedTrack_ == -1 ? AuthoringSelectionKind::Attack :
        selectedTrack_ <= -2 ? AuthoringSelectionKind::Event : AuthoringSelectionKind::Track;
}

bool EnemyAttackPreviewWindow::IsAuthoringViewportActive() const {
#ifdef USE_IMGUI
    return enabled_ && !crownMovieMode_ && EditorManager::GetInstance()->GetSelectedObject() == this &&
        sceneManager_ && !sceneManager_->IsPlaying();
#else
    return false;
#endif
}

bool EnemyAttackPreviewWindow::GetPlacementTransform(Vector3& position, Vector3& rotation, Vector3& scale) {
    const auto* attack = GetSelectedAttackDefinition();
    Object3d* actor = playerCopyMode_ ? static_cast<Object3d*>(previewPlayer_) : static_cast<Object3d*>(previewEnemy_);
    if (!attack || !actor || GetAuthoringSelectionKind() == AuthoringSelectionKind::Actor || selectedTrack_ == -1) return false;
    Vector3 forward{0,0,1};
    if (previewTarget_) { auto delta = previewTarget_->GetTranslate() - actor->GetTranslate(); delta.y = 0;
        if (Math::Length(delta) > 0.001f) forward = Math::Normalize(delta); }
    Vector3 origin = actor->GetTranslate();
    rotation = {}; scale = {1,1,1};
    if (selectedTrack_ <= -2) {
        const int index = -2 - selectedTrack_;
        if (index >= static_cast<int>(attack->sequence.events.size())) return false;
        const auto& event = attack->sequence.events[index];
        AttackSequenceContext context;
        context.position = origin; context.forward = forward; context.ownerSize = previewBaseScale_;
        context.hasTarget = previewTarget_ != nullptr;
        if (previewTarget_) context.targetPosition = previewTarget_->GetTranslate();
        position = ResolveAttackSequenceEmission(event, context, 0, 0).position;
        rotation.y = std::atan2(forward.x,forward.z) + event.yawDegrees * 0.01745329252f;
        const float size = ResolveProjectileComponent(*attack, event).visualScale;
        if (event.operation == "projectile") scale = {size,size,size};
        return true;
    }
    if (selectedTrack_ >= static_cast<int>(attack->sequence.tracks.size())) return false;
    const auto& track = attack->sequence.tracks[selectedTrack_];
    if (track.type == "body_animation") return false;
    if (track.parentTrack >= 0 && track.parentTrack < static_cast<int>(attack->sequence.tracks.size())) {
        const auto& parent = attack->sequence.tracks[track.parentTrack];
        const auto parentKey = parent.Evaluate(track.begin + placementTime_ - parent.begin);
        const Vector3 right{forward.z,0,-forward.x};
        origin = origin + right * parentKey.position[0] + Vector3{0,parentKey.position[1],0} + forward * parentKey.position[2];
        const float yaw = std::atan2(forward.x,forward.z) + parentKey.rotation[1];
        forward = {std::sin(yaw),0,std::cos(yaw)};
    }
    const auto key = track.Evaluate(track.motion == "keyframed" ? placementTime_ : 0);
    position = origin + Vector3{forward.z,0,-forward.x} * key.position[0] + Vector3{0,key.position[1],0} + forward * key.position[2];
    rotation = {key.rotation[0],key.rotation[1] + std::atan2(forward.x,forward.z),key.rotation[2]};
    scale = {key.scale[0],key.scale[1],key.scale[2]};
    return true;
}

void EnemyAttackPreviewWindow::ApplyPlacementTransform(const Vector3& position, const Vector3& rotation, const Vector3& scale) {
    auto* attack = GetSelectedAttackDefinition();
    Vector3 previous, oldRotation, oldScale;
    if (!attack || !GetPlacementTransform(previous, oldRotation, oldScale)) return;
    if (selectedTrack_ <= -2) {
        auto& event = attack->sequence.events[-2 - selectedTrack_];
        const Vector3 delta = position - previous;
        if (placementOperation_ == 0) {
            const auto local = AttackPlacementMath::ToLocalDelta(delta,oldRotation.y);
            event.right += local.x; event.up += local.y; event.forward += local.z;
        } else if (placementOperation_ == 1) AttackPlacementMath::RotateEvent(event,previewBaseScale_,rotation.y-oldRotation.y);
        else if (event.operation == "projectile") {
            auto part = ResolveProjectileComponent(*attack,event);
            // 弾の保存形式は均等倍率です。縮小した軸も拾い、未操作の軸で打ち消さないようにします。
            float value = scale.x;
            if (std::abs(scale.y-oldScale.y) > std::abs(value-oldScale.x)) value = scale.y;
            if (std::abs(scale.z-oldScale.z) > std::abs(value-oldScale.x)) value = scale.z;
            part.visualScale = std::clamp(value,0.01f,20.0f);
            event.projectile = part;
        }
    } else {
        auto& track = attack->sequence.tracks[selectedTrack_];
        const float time = track.motion == "keyframed" ? std::clamp(placementTime_,0.0f,track.end-track.begin) : 0;
        auto key = track.Evaluate(time); key.time = time;
        const Vector3 delta = position - previous;
        const float yaw = oldRotation.y - key.rotation[1];
        if (placementOperation_ == 0) {
            const auto local = AttackPlacementMath::ToLocalDelta(delta,yaw);
            key.position[0] += local.x; key.position[1] += local.y; key.position[2] += local.z;
        } else if (placementOperation_ == 1) key.rotation = {rotation.x,rotation.y-yaw,rotation.z};
        else key.scale = {(std::max)(0.001f,scale.x),(std::max)(0.001f,scale.y),(std::max)(0.001f,scale.z)};
        if (!AttackPlacementMath::PutKey(track,key)) placementMessage_ = "キー数の上限です。既存キーの時刻を選んで編集してください。";
    }
    profileDirty_ = true;
    UpdatePlacementPreview();
}

void EnemyAttackPreviewWindow::DrawPlacementControls() {
#ifdef USE_IMGUI
    const auto* attack = GetSelectedAttackDefinition();
    if (!attack || selectedTrack_ == -1) return;
    if (selectedTrack_ <= -2 && -2-selectedTrack_ >= static_cast<int>(attack->sequence.events.size())) return;
    if (selectedTrack_ >= 0) {
        if (selectedTrack_ >= static_cast<int>(attack->sequence.tracks.size())) return;
        if (attack->sequence.tracks[selectedTrack_].type == "body_animation") {
            ImGui::TextWrapped("術者のアニメーションは「保存せず動作確認」で再生します。配置ギズモは生成Object・GPU・判定が対象です。");
            return;
        }
    }
    ImGui::SeparatorText("選択部品の配置");
    if (ImGui::Button("選択した部品だけを表示")) BeginPlacementPreview();
    if (!placementPreview_) { ImGui::TextWrapped("単独表示ではAI・ダメージを止めて配置します。攻撃全体は「保存せず動作確認」で再生します。"); return; }
    ImGui::Checkbox("術者と標的を一時的に隠す", &hidePlacementActor_);
    if (selectedTrack_ >= 0) {
        if (selectedTrack_ >= static_cast<int>(attack->sequence.tracks.size())) return;
        const auto& track = attack->sequence.tracks[selectedTrack_];
        ImGui::SliderFloat("編集時刻 (部品開始から)",&placementTime_,0,(std::max)(0.01f,track.end-track.begin),"%.3f 秒");
        ImGui::TextWrapped("ギズモ操作はこの時刻のキーを編集し、なければ追加します。移動モード付き表示物は発射前の初期位置を編集します。");
    } else {
        ImGui::SliderFloat("素材の経過時間 (配置確認のみ)",&placementTime_,0,3,"%.2f 秒");
        ImGui::TextWrapped("GPU素材は単独の放出結果を表示します。攻撃の発生時刻・粒子の寿命は変更しません。");
    }
    ImGui::RadioButton("移動",&placementOperation_,0); ImGui::SameLine();
    const bool event = selectedTrack_ <= -2;
    const bool gpu = event && attack->sequence.events[-2-selectedTrack_].operation == "gpu_vfx";
    const bool hitbox = !event && attack->sequence.tracks[selectedTrack_].type == "hitbox";
    if (!hitbox) { ImGui::RadioButton(event ? "水平回転" : "回転",&placementOperation_,1); ImGui::SameLine(); }
    if (!gpu && !hitbox) ImGui::RadioButton(event ? "弾の表示倍率" : "拡縮",&placementOperation_,2);
    if ((gpu && placementOperation_ == 2) || hitbox) placementOperation_ = 0;
    if (ImGui::Button("選択位置へカメラを寄せる")) {
        Vector3 p,r,s;
        if (GetPlacementTransform(p,r,s)) if (auto* camera = CameraManager::GetInstance()->GetActiveCamera()) {
            const auto delta = camera->GetEye() - camera->GetTargetPoint();
            camera->SetFollowTarget(nullptr); camera->SetTarget(p); camera->SetEye(p + delta); camera->Update();
        }
    }
    if (!placementMessage_.empty()) ImGui::TextWrapped("%s",placementMessage_.c_str());
    UpdatePlacementPreview();
#endif
}

void EnemyAttackPreviewWindow::DrawPlacementGizmo(float x,float y,float width,float height) {
#ifdef USE_IMGUI
    if (!placementPreview_) return;
    Vector3 position, rotation, scale;
    if (!GetPlacementTransform(position,rotation,scale)) return;
    auto* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return;
    auto* attack = GetSelectedAttackDefinition();
    const auto& vp = camera->GetViewProjectionMatrix();
    const float w = position.x*vp.m[0][3]+position.y*vp.m[1][3]+position.z*vp.m[2][3]+vp.m[3][3];
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({x,y},{x+width,y+height},true);
    float radius = 0;
    if (selectedTrack_ >= 0 && attack->sequence.tracks[selectedTrack_].type == "hitbox")
        radius = attack->sequence.tracks[selectedTrack_].radius;
    if (selectedTrack_ <= -2 && projectileSection_ == 2)
        radius = ResolveProjectileComponent(*attack,attack->sequence.events[-2-selectedTrack_]).radius;
    if (radius > 0) {
        auto project = [&](Vector3 p, ImVec2& screen) {
            const float cw=p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3];
            if(cw<=0.001f) return false;
            auto q=Math::Transform(p,vp); screen={x+(q.x+1)*width*0.5f,y+(1-q.y)*height*0.5f}; return true;
        };
        for(int plane=0;plane<3;++plane) for(int i=0;i<48;++i) {
            auto point=[&](int n) {float a=n*6.28318530718f/48;float c=radius*std::cos(a),s=radius*std::sin(a);
                return position+(plane==0?Vector3{c,s,0}:plane==1?Vector3{c,0,s}:Vector3{0,c,s});};
            ImVec2 a,b; if(project(point(i),a)&&project(point(i+1),b)) draw->AddLine(a,b,IM_COL32(255,180,50,255),2);
        }
    }
    if (w > 0.001f) {
        const auto p = Math::Transform(position,vp);
        ImVec2 screen{x+(p.x+1)*width*0.5f,y+(1-p.y)*height*0.5f};
        draw->AddCircle(screen,12,IM_COL32(255,210,60,255),24,2);
        const std::string label = selectedTrack_ <= -2 ? attack->sequence.events[-2-selectedTrack_].name : attack->sequence.tracks[selectedTrack_].name;
        draw->AddText({screen.x+16,screen.y},IM_COL32(255,230,110,255),label.c_str());
    }
    draw->PopClipRect();
    Math math;
    Matrix4x4 world = math.Multiply(math.Multiply(math.MakeScaleMatrix(scale),math.MakeRotateQuaternionMatrix(math.EulerToQuaternion(rotation))),math.MakeTranslateMatrix(position));
    auto view = camera->GetViewMatrix(); auto projection = camera->GetProjectionMatrix();
    ImGuizmo::SetDrawlist(draw); ImGuizmo::SetRect(x,y,width,height); ImGuizmo::SetOrthographic(false);
    ImGuizmo::PushID(9021);
    auto op = placementOperation_ == 0 ? ImGuizmo::TRANSLATE : placementOperation_ == 1 ?
        (selectedTrack_ <= -2 ? ImGuizmo::ROTATE_Y : ImGuizmo::ROTATE) : ImGuizmo::SCALE;
    const auto before = attack->sequence;
    const bool changed = ImGuizmo::Manipulate(&view.m[0][0],&projection.m[0][0],op,ImGuizmo::LOCAL,&world.m[0][0]);
    if (ImGuizmo::IsUsing()) {
        if (!placementGizmoUsing_) { placementGestureBefore_ = before; placementGizmoUsing_ = true; }
        if (changed) {
            float p[3],r[3],s[3]; ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0],p,r,s);
            ApplyPlacementTransform({p[0],p[1],p[2]},{r[0]*0.01745329252f,r[1]*0.01745329252f,r[2]*0.01745329252f},{s[0],s[1],s[2]});
        }
    } else if (placementGizmoUsing_) {
        authoringGestureOpen_ = false; RememberSequenceEdit(placementGestureBefore_);
        authoringGestureOpen_ = false; placementGizmoUsing_ = false;
    }
    ImGuizmo::PopID();
#endif
}
