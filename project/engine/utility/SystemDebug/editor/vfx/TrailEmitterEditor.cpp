#include "TrailEmitterEditor.h"
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include "IconsFontAwesome5.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include "EffectPreviewStage.h"
#include "EditorManager.h"
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ============================================================
//  Initialize
// ============================================================
void TrailEmitterEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    RefreshFileLists();
}

void TrailEmitterEditor::ResetDummyPreview(bool clearEffects) {
    previewTime_ = 0.0f;
    dummyFirstFrame_ = true;
    isDummyRunning_ = true;
    if (clearEffects) {
        MeshEffectManager::GetInstance()->ClearActiveEffects();
        GPUParticleManager::GetInstance()->ResetSimulation();
    }
}

void TrailEmitterEditor::StepDummyPreview(float deltaTime, bool allowLoop) {
    if (!isDummyRunning_) {
        return;
    }

    const float duration = (std::max)(previewDuration_, 0.01f);
    previewTime_ += (std::max)(deltaTime, 0.0f);
    if (previewTime_ >= duration) {
        if (allowLoop) {
            previewTime_ = std::fmod(previewTime_, duration);
            dummyFirstFrame_ = true;
        } else {
            previewTime_ = duration;
        }
    }

    Vector3 origin = { 0.0f, 0.0f, 0.0f };
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    if (previewStage && previewStage->IsEnabled() &&
        EditorManager::GetInstance()->GetSelectedObject() == this) {
        origin = previewStage->GetPreviewPosition();
    }

    const float t = previewTime_ / duration;
    dummyPos_ = origin;
    dummyPos_.x += sinf(t * 3.14159f * 2.0f) * previewRange_;

    if (dummyFirstFrame_) {
        lastDummyPos_ = dummyPos_;
        dummyFirstFrame_ = false;
        return;
    }

    Vector3 diff = dummyPos_ - lastDummyPos_;
    const float dist = Math::Length(diff);
    const TrailEmitterConfig& cfg = emitter_.GetConfig();
    if (dist < cfg.emitDistance) {
        return;
    }

    Vector3 dir = Math::Normalize(diff);
    const float rotY = atan2f(dir.x, dir.z);
    Vector3 spawnPos = {
        (lastDummyPos_.x + dummyPos_.x) * 0.5f,
        (lastDummyPos_.y + dummyPos_.y) * 0.5f,
        (lastDummyPos_.z + dummyPos_.z) * 0.5f
    };
    const Vector3 rotation = cfg.autoOrient ? Vector3{ 0.0f, rotY, 0.0f } : Vector3{ 0.0f, 0.0f, 0.0f };

    if (cfg.emitMesh && !cfg.meshEffectPreset.empty()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            "Resources/json/effect/" + cfg.meshEffectPreset + ".json",
            spawnPos,
            rotation,
            cfg.scale);
    }
    if (cfg.emitParticle && !cfg.gpuParticlePreset.empty()) {
        Matrix4x4 transform = Math::MakeIdentity4x4();
        transform.m[3][0] = spawnPos.x;
        transform.m[3][1] = spawnPos.y;
        transform.m[3][2] = spawnPos.z;
        GPUParticleManager::GetInstance()->Emit(cfg.gpuParticlePreset, spawnPos, transform);
    }
    lastDummyPos_ = dummyPos_;
}

// ============================================================
//  Update
// ============================================================
void TrailEmitterEditor::Update(float deltaTime) {
    if (deltaTime <= 0.0001f) deltaTime = 1.0f / 60.0f;

    bool isGamePlaying = SceneManager::GetInstance() && SceneManager::GetInstance()->IsPlaying();
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    const bool isThisEditorSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    const bool usePreviewStage = previewStage && previewStage->IsEnabled() && isThisEditorSelected;
    MeshEffectManager* meshManager = MeshEffectManager::GetInstance();
    GPUParticleManager* gpuManager = GPUParticleManager::GetInstance();
    const float previewScale = usePreviewStage ? previewStage->GetPlaybackSpeed() : 1.0f;
    const float previewDelta = deltaTime * previewScale;
    if (usePreviewStage) {
        gpuManager->SetTimeScale(previewScale);
    }

    // ゲームが止まっているときも更新を代行
    if (!isGamePlaying) {
        meshManager->Update(previewDelta);
        gpuManager->Update(deltaTime);
    }

    // ── Play開始を検知 → ポインタリセット ──
    // エディットモードで選んだObject3dはPlay開始時に再生成されて無効になるため
    if (isGamePlaying && !wasPlaying_) {
        targetObject_    = nullptr;
        isTracking_      = false;
        isDummyRunning_  = false;
        dummyFirstFrame_ = true;
        emitter_.Stop();
    }

    // ── Play停止を検知 → ポインタリセット ──
    // ゲーム中に選んだObject3dはStop時に再生成・破棄されて無効になるため
    if (!isGamePlaying && wasPlaying_) {
        targetObject_    = nullptr;
        isTracking_      = false;
        isDummyRunning_  = false;
        dummyFirstFrame_ = true;
        emitter_.Stop();
    }

    wasPlaying_ = isGamePlaying;

    // ── シーン切り替え時にリセット ──
    BaseScene* currentScene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (lastScene_ != currentScene) {
        lastScene_       = currentScene;
        targetObject_    = nullptr;
        isTracking_      = false;
        isDummyRunning_  = false;
        dummyFirstFrame_ = true;
        emitter_.Stop();
        return;
    }

    auto seekDummyPreview = [&](float seekTime) {
        ResetDummyPreview(true);
        const float targetTime = std::clamp(seekTime, 0.0f, (std::max)(previewDuration_, 0.01f));
        const float previousGpuScale = gpuManager->GetTimeScale();
        gpuManager->SetTimeScale(1.0f);
        while (previewTime_ + (1.0f / 60.0f) < targetTime) {
            StepDummyPreview(1.0f / 60.0f, false);
            meshManager->UpdateEditorPreviewStep(1.0f / 60.0f);
            gpuManager->UpdateEditorPreviewStep(1.0f / 60.0f);
        }
        if (targetTime > previewTime_) {
            const float remainder = targetTime - previewTime_;
            StepDummyPreview(remainder, false);
            meshManager->UpdateEditorPreviewStep(remainder);
            gpuManager->UpdateEditorPreviewStep(remainder);
        }
        gpuManager->SetTimeScale(previousGpuScale);
    };

    if (usePreviewStage && previewStage->GetPlayRequestSerial() != lastStagePlayRequestSerial_) {
        lastStagePlayRequestSerial_ = previewStage->GetPlayRequestSerial();
        seekDummyPreview(0.0f);
    }
    if (usePreviewStage && previewStage->GetStopRequestSerial() != lastStageStopRequestSerial_) {
        lastStageStopRequestSerial_ = previewStage->GetStopRequestSerial();
        isDummyRunning_ = false;
        previewTime_ = 0.0f;
        meshManager->ClearActiveEffects();
        gpuManager->ResetSimulation();
    }
    if (usePreviewStage && previewStage->GetSeekRequestSerial() != lastStageSeekRequestSerial_) {
        lastStageSeekRequestSerial_ = previewStage->GetSeekRequestSerial();
        seekDummyPreview(previewStage->GetSeekTargetTime());
    }

    // ── モード1: 実オブジェクト追従 ──
    if (isTracking_ && targetObject_) {
        // エディットモードのみ: シーンのオブジェクトリストで有効性を検証
        // Play中は GetObjects() にゲームオブジェクトが含まれないケースがあるためスキップ
        if (!isGamePlaying && currentScene) {
            bool stillValid = false;
            for (auto& obj : currentScene->GetObjects()) {
                if (obj.get() == targetObject_) { stillValid = true; break; }
            }
            if (!stillValid) {
                targetObject_ = nullptr;
                isTracking_   = false;
                emitter_.Stop();
                return;
            }
        }
        emitter_.Update(previewDelta);
        return;
    }

    // ── モード2: サイン波ダミープレビュー ──
    if (isDummyRunning_) {
        const bool allowLoop = usePreviewStage ? previewStage->IsLoopEnabled() : true;
        StepDummyPreview(previewDelta, allowLoop);
        if (usePreviewStage && !allowLoop && previewTime_ >= previewDuration_) {
            isDummyRunning_ = false;
        }
    }

    if (usePreviewStage) {
        const float duration = (std::max)(previewDuration_, 0.01f);
        const TrailEmitterConfig& config = emitter_.GetConfig();
        std::vector<EffectPreviewStage::TimelineEvent> events;
        events.push_back({ "Motion path", 0.0f, duration, Vector4{ 0.65f, 0.45f, 1.0f, 1.0f } });
        if (config.emitMesh) {
            events.push_back({ "Mesh emit", 0.0f, duration, Vector4{ 0.25f, 0.8f, 1.0f, 1.0f } });
        }
        if (config.emitParticle) {
            events.push_back({ "Particle emit", 0.0f, duration, Vector4{ 1.0f, 0.55f, 0.2f, 1.0f } });
        }
        previewStage->ReportToolState(
            EffectPreviewStage::ToolKind::Trail,
            "Trail Emitter",
            std::clamp(previewTime_, 0.0f, duration),
            duration,
            previewStage->IsTransportPlaying(),
            static_cast<int>(meshManager->GetActiveEffects().size()) + gpuManager->GetActiveSystemCount(),
            events);
    }
}




// ============================================================
//  DrawImGui
// ============================================================
void TrailEmitterEditor::DrawImGui() {
#ifdef USE_IMGUI
    TrailEmitterConfig& cfg = emitter_.GetConfig();

    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.1f, 1.0f), ICON_FA_FIRE " Trail Emitter Editor");
    ImGui::Separator();

    // ── 保存・読み込み ──
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("##ConfigName", configNameBuf_, sizeof(configNameBuf_));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SAVE " 保存")) Save(configNameBuf_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " 更新")) RefreshFileLists();

    if (!savedConfigList_.empty()) {
        if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " 設定読み込み", configNameBuf_)) {
            for (auto& name : savedConfigList_) {
                bool sel = (std::string(configNameBuf_) == name);
                if (ImGui::Selectable(name.c_str(), sel)) {
                    strcpy_s(configNameBuf_, sizeof(configNameBuf_), name.c_str());
                    Load(name);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();

    // ── オブジェクト選択 (MeshEffectEditorと同じパターン) ──
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.8f, 1.0f), ICON_FA_CROSSHAIRS " 追従対象オブジェクト");
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
        std::string currentName = targetObject_ ? targetObject_->GetName() : "なし (None)";

        if (ImGui::BeginCombo("##TargetObject", currentName.c_str())) {
            // 追従解除
            if (ImGui::Selectable("なし (None)", targetObject_ == nullptr)) {
                targetObject_ = nullptr;
                if (isTracking_) { emitter_.Stop(); isTracking_ = false; }
            }
            // シーン内の全オブジェクト
            for (auto& obj : objects) {
                if (!obj || obj->IsEditorInternal()) continue;
                bool isSel = (targetObject_ == obj.get());
                std::string label = obj->GetName();
                if (label.empty()) label = "(NoName)";
                if (ImGui::Selectable(label.c_str(), isSel)) {
                    targetObject_ = obj.get();
                    // ターゲット変更時は一旦停止
                    if (isTracking_) { emitter_.Stop(); isTracking_ = false; }
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("シーンが読み込まれていません");
    }

    // 選択中オブジェクトの情報表示
    if (targetObject_) {
        Vector3 pos = targetObject_->GetWorldPosition();
        ImGui::TextDisabled("  位置: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
    }
    ImGui::Separator();

    // ── メッシュエフェクト設定 ──
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), ICON_FA_MAGIC " メッシュエフェクト");
    ImGui::Checkbox("##emitMesh", &cfg.emitMesh);
    ImGui::SameLine();
    ImGui::BeginDisabled(!cfg.emitMesh);
    if (!meshEffectList_.empty()) {
        int curIdx = 0;
        for (int i = 0; i < (int)meshEffectList_.size(); ++i)
            if (meshEffectList_[i] == cfg.meshEffectPreset) { curIdx = i; break; }
        if (ImGui::BeginCombo("エフェクトプリセット##mesh", cfg.meshEffectPreset.empty() ? "(なし)" : cfg.meshEffectPreset.c_str())) {
            for (int i = 0; i < (int)meshEffectList_.size(); ++i) {
                bool sel = (curIdx == i);
                if (ImGui::Selectable(meshEffectList_[i].c_str(), sel))
                    cfg.meshEffectPreset = meshEffectList_[i];
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("Resources/json/effect/ にJSONが見当たりません");
    }
    ImGui::EndDisabled();

    // ── GPUパーティクル設定 ──
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), ICON_FA_FIRE " GPUパーティクル");
    ImGui::Checkbox("##emitPart", &cfg.emitParticle);
    ImGui::SameLine();
    ImGui::BeginDisabled(!cfg.emitParticle);
    if (!gpuParticleList_.empty()) {
        if (ImGui::BeginCombo("パーティクルプリセット##gpu", cfg.gpuParticlePreset.empty() ? "(なし)" : cfg.gpuParticlePreset.c_str())) {
            for (auto& name : gpuParticleList_) {
                bool sel = (cfg.gpuParticlePreset == name);
                if (ImGui::Selectable(name.c_str(), sel)) cfg.gpuParticlePreset = name;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("Resources/json/gpu_particles/ にJSONが見当たりません");
    }
    ImGui::EndDisabled();

    // ── 共通設定 ──
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), ICON_FA_SLIDERS_H " 共通設定");
    ImGui::DragFloat("Emit間隔 (m)", &cfg.emitDistance, 0.01f, 0.01f, 5.0f, "%.2f");
    ImGui::DragFloat3("スケール倍率", &cfg.scale.x, 0.01f, 0.01f, 10.0f);
    ImGui::Checkbox("移動方向に自動回転 (AutoOrient)", &cfg.autoOrient);

    // ── 追従プレビュー (実オブジェクト) ──
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.5f, 1.0f, 1.0f), ICON_FA_RUNNING " 実オブジェクト追従");

    if (targetObject_) {
        if (!isTracking_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_PLAY " 追従開始", ImVec2(-1, 30))) {
                isDummyRunning_ = false;
                emitter_.Start(targetObject_);
                isTracking_ = true;
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button(ICON_FA_STOP " 追従停止", ImVec2(-1, 30))) {
                emitter_.Stop();
                isTracking_ = false;
            }
            ImGui::PopStyleColor();

            // ── 診断情報 ──
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                ICON_FA_CIRCLE " 追従中: %s", targetObject_->GetName().c_str());

            Vector3 curPos  = targetObject_->GetWorldPosition();
            Vector3 lastPos = emitter_.GetLastEmitPos();
            Vector3 diff    = { curPos.x - lastPos.x, curPos.y - lastPos.y, curPos.z - lastPos.z };
            float dist = sqrtf(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);

            ImGui::TextDisabled("  現在位置: (%.2f, %.2f, %.2f)", curPos.x, curPos.y, curPos.z);
            ImGui::TextDisabled("  前回Emit: (%.2f, %.2f, %.2f)", lastPos.x, lastPos.y, lastPos.z);
            ImGui::TextDisabled("  移動距離: %.3f / %.3f (しきい値)", dist, emitter_.GetConfig().emitDistance);
            ImGui::TextDisabled("  エミッター有効: %s", emitter_.IsActive() ? "YES" : "NO");
            ImGui::TextDisabled("  発火中エフェクト数: %d",
                (int)MeshEffectManager::GetInstance()->GetActiveEffects().size());
        }
    } else {
        ImGui::BeginDisabled(true);
        ImGui::Button(ICON_FA_MOUSE_POINTER " 先に対象オブジェクトを選択してください", ImVec2(-1, 30));
        ImGui::EndDisabled();
    }

    // ── ダミープレビュー (サイン波) ──
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), ICON_FA_PLAY_CIRCLE " サイン波ダミープレビュー");
    ImGui::DragFloat("往復距離 (m)",    &previewRange_,    0.1f, 0.5f, 30.0f, "%.1f");
    ImGui::DragFloat("サイクル時間 (s)", &previewDuration_, 0.05f, 0.2f, 10.0f, "%.2f");

    if (!isDummyRunning_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.7f, 1.0f));
        if (ImGui::Button(ICON_FA_PLAY " ダミー開始", ImVec2(-1, 28))) {
            if (isTracking_) { emitter_.Stop(); isTracking_ = false; } // 追従は止める
            isDummyRunning_  = true;
            previewTime_     = 0.0f;
            dummyFirstFrame_ = true;
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.5f, 1.0f));
        if (ImGui::Button(ICON_FA_STOP " ダミー停止", ImVec2(-1, 28))) {
            isDummyRunning_ = false;
        }
        ImGui::PopStyleColor();
        ImGui::TextDisabled("ダミー位置: X=%.2f  Y=%.2f  Z=%.2f", dummyPos_.x, dummyPos_.y, dummyPos_.z);
    }

    // ── ゲームコード向け使い方ガイド ──
    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_FA_CODE " ゲームコードでの使い方")) {
        ImGui::TextWrapped(
            "TrailEmitter trail;\n"
            "trail.GetConfig().meshEffectPreset = \"%s\";\n"
            "trail.GetConfig().emitDistance = %.2f;\n"
            "// 攻撃開始時:\n"
            "trail.Start(swordTipObject);\n"
            "// 毎フレーム:\n"
            "trail.Update(deltaTime);\n"
            "// 攻撃終了時:\n"
            "trail.Stop();",
            cfg.meshEffectPreset.c_str(), cfg.emitDistance
        );
    }
#endif
}

// ============================================================
//  RefreshFileLists
// ============================================================
void TrailEmitterEditor::RefreshFileLists() {
    meshEffectList_.clear();
    gpuParticleList_.clear();
    savedConfigList_.clear();

    auto scanDir = [](const std::string& dir, std::vector<std::string>& out) {
        if (!fs::exists(dir)) return;
        for (auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".json")
                out.push_back(entry.path().stem().string());
        }
    };

    scanDir("Resources/json/effect/",       meshEffectList_);
    scanDir("Resources/json/gpu_particles/", gpuParticleList_);
    scanDir("Resources/json/trail_emitter/", savedConfigList_);
}

// ============================================================
//  Save
// ============================================================
void TrailEmitterEditor::Save(const std::string& name) {
    if (name.empty()) return;
    fs::create_directories("Resources/json/trail_emitter/");

    TrailEmitterConfig& cfg = emitter_.GetConfig();
    json j;
    j["meshEffectPreset"]  = cfg.meshEffectPreset;
    j["gpuParticlePreset"] = cfg.gpuParticlePreset;
    j["emitDistance"]      = cfg.emitDistance;
    j["scale"]             = { cfg.scale.x, cfg.scale.y, cfg.scale.z };
    j["emitMesh"]          = cfg.emitMesh;
    j["emitParticle"]      = cfg.emitParticle;
    j["autoOrient"]        = cfg.autoOrient;

    std::ofstream f("Resources/json/trail_emitter/" + name + ".json");
    if (f.is_open()) { f << j.dump(4); f.close(); }
    RefreshFileLists();
}

// ============================================================
//  Load
// ============================================================
void TrailEmitterEditor::Load(const std::string& name) {
    std::ifstream f("Resources/json/trail_emitter/" + name + ".json");
    if (!f.is_open()) return;
    json j; f >> j; f.close();

    TrailEmitterConfig& cfg = emitter_.GetConfig();
    if (j.contains("meshEffectPreset"))  cfg.meshEffectPreset  = j["meshEffectPreset"];
    if (j.contains("gpuParticlePreset")) cfg.gpuParticlePreset = j["gpuParticlePreset"];
    if (j.contains("emitDistance"))      cfg.emitDistance      = j["emitDistance"];
    if (j.contains("scale")) { cfg.scale.x = j["scale"][0]; cfg.scale.y = j["scale"][1]; cfg.scale.z = j["scale"][2]; }
    if (j.contains("emitMesh"))          cfg.emitMesh          = j["emitMesh"];
    if (j.contains("emitParticle"))      cfg.emitParticle      = j["emitParticle"];
    if (j.contains("autoOrient"))        cfg.autoOrient        = j["autoOrient"];
}
