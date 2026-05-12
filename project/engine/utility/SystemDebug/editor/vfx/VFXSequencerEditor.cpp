#include "VFXSequencerEditor.h"
#include <imgui.h>
#include <filesystem>
#include "IconsFontAwesome5.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include <MeshEffectManager.h>
#include "GPUParticleManager.h"
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
        GPUParticleManager::GetInstance()->Update(timeStep);
     
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
    std::string seDir = "Resources/audio/se/";
    if (fs::exists(seDir)) {
        for (const auto& entry : fs::directory_iterator(seDir)) {
            auto ext = entry.path().extension();
            if (ext == ".wav" || ext == ".mp3") {
                seFileList_.push_back(entry.path().filename().string());
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
    // 1. プレビュー操作
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

        // 種類に合わせて読み込むリストとラベルを切り替える準備
        const std::vector<std::string>* currentListPtr = nullptr;
        const char* comboLabel = "";

        if (events[i].type == VFXEventType::GPUParticle) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FA_FIRE " [ GPUパーティクル (瞬間) ]");
            currentListPtr = &particlePresetList_;
            comboLabel = " プリセット名";
        }
        else if (events[i].type == VFXEventType::MovingParticle) {
            // 軌跡用UI
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), ICON_FA_FIRE " [ GPUパーティクル (軌跡) ]");
            currentListPtr = &particlePresetList_;
            comboLabel = " プリセット名";
        }
        else if (events[i].type == VFXEventType::MeshEffect) {
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), ICON_FA_CUBE " [ メッシュエフェクト ]");
            currentListPtr = &meshEffectList_;
            comboLabel = " エフェクト名";
        }
        else if (events[i].type == VFXEventType::SoundEffect) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_MUSIC " [ サウンドエフェクト ]");
            currentListPtr = &seFileList_;
            comboLabel = " ファイル名";
        }

        ImGui::SameLine();
        ImGui::TextDisabled(" | ID:%d", i);

        // コンボボックスの描画
        if (currentListPtr && ImGui::BeginCombo(comboLabel, events[i].presetName.c_str())) {
            for (const auto& itemName : *currentListPtr) {
                bool isSelected = (events[i].presetName == itemName);
                if (ImGui::Selectable(itemName.c_str(), isSelected)) {
                    events[i].presetName = itemName;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::DragFloat(ICON_FA_STOPWATCH " 発火時間 (秒後)", &events[i].triggerTime, 0.05f, 0.0f, 60.0f);

        // =========================================================
        // パラメータ入力欄 (SEやMeshには位置オフセットを出さない)
        // =========================================================
        if (events[i].type == VFXEventType::GPUParticle) {
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット位置", &events[i].offset.x, 0.1f);
        }
        else if (events[i].type == VFXEventType::MeshEffect) {
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット位置", &events[i].offset.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_SYNC_ALT " 追加回転 (rad)", &events[i].rotation.x, 0.05f);
            ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " スケール倍率", &events[i].scale.x, 0.05f);
        }
        else if (events[i].type == VFXEventType::MovingParticle) {
            // 軌跡用パラメータUI
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 始点オフセット", &events[i].offset.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 中間点(カーブ)", &events[i].controlPoint.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 終点オフセット", &events[i].endOffset.x, 0.1f);
            ImGui::DragFloat(ICON_FA_STOPWATCH " 移動時間 (秒)", &events[i].duration, 0.05f, 0.1f, 10.0f);
            ImGui::DragInt("イージング種別(0:等速, 2:減速など)", &events[i].easingType, 1.0f, 0, 30);
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
    // イベント追加ボタン（幅を3等分して綺麗に並べる）
    // =========================================================
    float btnWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " パーティクル", ImVec2(btnWidth, 35))) {
        std::string def = particlePresetList_.empty() ? "" : particlePresetList_[0];
        events.push_back({ VFXEventType::GPUParticle, def, 0.0f, {0,0,0}, {0,0,0}, {1,1,1}, false });
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " メッシュ", ImVec2(btnWidth, 35))) {
        std::string def = meshEffectList_.empty() ? "" : meshEffectList_[0];
        events.push_back({ VFXEventType::MeshEffect, def, 0.0f, {0,0,0}, {0,0,0}, {1,1,1}, false });
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " SE (効果音)", ImVec2(btnWidth, 35))) {
        std::string def = seFileList_.empty() ? "" : seFileList_[0];
        events.push_back({ VFXEventType::SoundEffect, def, 0.0f, {0,0,0}, {0,0,0}, {1,1,1}, false });
    }

    //  軌跡用パーティクルの追加ボタン
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " 軌跡パーティクル追加", ImVec2(-1, 30))) {
        std::string def = particlePresetList_.empty() ? "" : particlePresetList_[0];
        VFXEvent newEvent;
        newEvent.type = VFXEventType::MovingParticle;
        newEvent.presetName = def;
        newEvent.triggerTime = 0.0f;
        newEvent.offset = { 0,0,0 };
        newEvent.controlPoint = { 0, 5, 0 }; // デフォルトで少し上に弧を描く
        newEvent.endOffset = { 0, 0, 10 };   // 奥へ飛ぶ
        newEvent.duration = 1.0f;
        newEvent.easingType = 0;
        events.push_back(newEvent);
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