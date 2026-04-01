#include "VFXSequencerEditor.h"
#include <imgui.h>
#include <filesystem>
#include "IconsFontAwesome5.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include <MeshEffectManager.h>
namespace fs = std::filesystem;

void VFXSequencerEditor::Initialize() {
    previewSequencer_.Initialize(nullptr);
    RefreshFileList(); 
}

void VFXSequencerEditor::Update(float deltaTime) {
    // 実際の時間（リアルタイム）を使うか、1/60秒を強制的に入れる
    float timeStep = deltaTime;
    if (timeStep <= 0.0001f) timeStep = 1.0f / 60.0f;

    // シーケンサー自体の時間を進める
    previewSequencer_.Update(timeStep);


    bool isGamePlaying = false;
    if (SceneManager::GetInstance()) {
        isGamePlaying = SceneManager::GetInstance()->IsPlaying();
    }

    // ゲーム本編が止まっている（ポーズ中／エディタ編集中）ならエディタが更新を代行！
    if (!isGamePlaying) {
        MeshEffectManager::GetInstance()->Update(timeStep);
    }
}
// : フォルダ内の .json を自動検索
void VFXSequencerEditor::RefreshFileList() {
    particlePresetList_.clear();
    sequenceFileList_.clear();
    meshEffectList_.clear();
    // 1. パーティクル素材 (.json) を探す
    std::string particleDir = "Resources/json/gpu_particles/";
    if (fs::exists(particleDir)) {
        for (const auto& entry : fs::directory_iterator(particleDir)) {
            if (entry.path().extension() == ".json") {
                particlePresetList_.push_back(entry.path().stem().string());
            }
        }
    }
    
    std::string meshDir = "Resources/json/effect/";
    if (fs::exists(meshDir)) {
        for (const auto& entry : fs::directory_iterator(meshDir)) {
            if (entry.path().extension() == ".json") {
                meshEffectList_.push_back(entry.path().stem().string());
            }
        }
    }
    // 2. 作成済みの必殺技シーケンス (.json) を探す
    std::string sequenceDir = "Resources/json/vfx_sequence/";
    if (fs::exists(sequenceDir)) {
        for (const auto& entry : fs::directory_iterator(sequenceDir)) {
            if (entry.path().extension() == ".json") {
                sequenceFileList_.push_back(entry.path().stem().string());
            }
        }
    }
}

void VFXSequencerEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_FILM " --- VFXシーケンサー（複合演出）エディタ ---");

    if (ImGui::Button(ICON_FA_SYNC " リストを最新に更新", ImVec2(170, 20))) {
        RefreshFileList();
    }
    ImGui::Separator();

    // =========================================================
    // 1. プレビュー操作 (★ 違和感のあったターゲット選択UIを削除！)
    // =========================================================
    if (ImGui::Button(ICON_FA_PLAY " 再生 (Play Test)", ImVec2(120, 30))) {
        previewSequencer_.Play();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " 停止 (Stop)", ImVec2(120, 30))) {
        previewSequencer_.Stop();
    }

    if (previewSequencer_.IsPlaying()) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), ICON_FA_PLAY_CIRCLE " ▶ 再生中...");
    }
    else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), ICON_FA_STOP_CIRCLE " ■ 停止中");
    }

    ImGui::Separator();

    // =========================================================
    // 2. タイムライン（イベントのリスト）編集
    // =========================================================
    ImGui::Text(ICON_FA_STREAM " 【 タイムライン 】");

    auto& events = previewSequencer_.GetEvents();

    for (int i = 0; i < events.size(); i++) {
        ImGui::PushID(i);
        ImGui::BeginGroup();

        // 種類をバッジで表示
        if (events[i].type == VFXEventType::GPUParticle) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FA_FIRE " [ GPUパーティクル ]");
        }
        else {
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), ICON_FA_CUBE " [ メッシュエフェクト ]");
        }
        ImGui::SameLine();
        ImGui::TextDisabled(" | ID:%d", i);

        // 種類に合わせて読み込むリストを切り替える
        const auto& currentList = (events[i].type == VFXEventType::GPUParticle) ? particlePresetList_ : meshEffectList_;
        const char* comboLabel = (events[i].type == VFXEventType::GPUParticle) ? " プリセット名" : " エフェクト名";

        if (ImGui::BeginCombo(comboLabel, events[i].presetName.c_str())) {
            for (const auto& presetName : currentList) {
                bool isSelected = (events[i].presetName == presetName);
                if (ImGui::Selectable(presetName.c_str(), isSelected)) {
                    events[i].presetName = presetName;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::DragFloat(ICON_FA_STOPWATCH " 発火時間 (秒後)", &events[i].triggerTime, 0.05f, 0.0f, 60.0f);

        // オフセット (メッシュエフェクトの場合は非表示にする事でさらにスッキリ！)
        if (events[i].type == VFXEventType::GPUParticle) {
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット位置", &events[i].offset.x, 0.1f);
        }

        if (ImGui::Button(ICON_FA_TRASH_ALT " このイベントを削除", ImVec2(170, 0))) {
            events.erase(events.begin() + i);
            ImGui::PopID();
            ImGui::EndGroup();
            i--;
            continue;
        }
        ImGui::EndGroup();

        ImGui::PopID();
        ImGui::Separator();
    }

    // =========================================================
    // イベント追加ボタン
    // =========================================================
    float btnWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " パーティクル追加", ImVec2(btnWidth, 35))) {
        std::string def = particlePresetList_.empty() ? "" : particlePresetList_[0];
        events.push_back({ VFXEventType::GPUParticle, def, 0.0f, {0,0,0}, false });
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " メッシュ追加", ImVec2(btnWidth, 35))) {
        std::string def = meshEffectList_.empty() ? "" : meshEffectList_[0];
        events.push_back({ VFXEventType::MeshEffect, def, 0.0f, {0,0,0}, false });
    }
    ImGui::Separator();

    // =========================================================
    // 3. セーブ＆ロード
    // =========================================================
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " 保存と読み込み (Save & Load)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " 既存のファイル", sequenceNameInput_)) {
            for (const auto& seqName : sequenceFileList_) {
                bool isSelected = (std::string(sequenceNameInput_) == seqName);
                if (ImGui::Selectable(seqName.c_str(), isSelected)) {
                    strcpy_s(sequenceNameInput_, sizeof(sequenceNameInput_), seqName.c_str());
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::InputText(ICON_FA_FILE_SIGNATURE " 新規保存名 / 上書き名", sequenceNameInput_, sizeof(sequenceNameInput_));

        if (ImGui::Button(ICON_FA_DOWNLOAD " 保存 (Save)", ImVec2(120, 0))) {
            previewSequencer_.Save(sequenceNameInput_);
            RefreshFileList();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " 読み込み (Load)", ImVec2(120, 0))) {
            previewSequencer_.Load(sequenceNameInput_);
        }
    }
#endif
}