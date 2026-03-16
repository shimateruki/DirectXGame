#include "VFXSequencerEditor.h"
#include <imgui.h>
#include <filesystem>
#include "IconsFontAwesome5.h"
namespace fs = std::filesystem;

void VFXSequencerEditor::Initialize() {
    previewSequencer_.Initialize(nullptr);
    RefreshFileList(); 
}

void VFXSequencerEditor::Update(float deltaTime) {
    previewSequencer_.Update(deltaTime);
}

// : フォルダ内の .json を自動検索
void VFXSequencerEditor::RefreshFileList() {
    particlePresetList_.clear();
    sequenceFileList_.clear();

    // 1. パーティクル素材 (.json) を探す
    std::string particleDir = "Resources/json/gpu_particles/";
    if (fs::exists(particleDir)) {
        for (const auto& entry : fs::directory_iterator(particleDir)) {
            if (entry.path().extension() == ".json") {
                particlePresetList_.push_back(entry.path().stem().string());
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

        // =========================================================
        // ★大改造: 手打ちではなく、ドロップダウンリストから選択！
        // =========================================================
        if (ImGui::BeginCombo(ICON_FA_FIRE " 呼び出すパーティクル", events[i].presetName.c_str())) {
            for (const auto& presetName : particlePresetList_) {
                bool isSelected = (events[i].presetName == presetName);
                if (ImGui::Selectable(presetName.c_str(), isSelected)) {
                    events[i].presetName = presetName; // 選択されたら代入！
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // 発火タイミングとオフセット
        ImGui::DragFloat(ICON_FA_STOPWATCH " 発火時間 (秒後)", &events[i].triggerTime, 0.05f, 0.0f, 60.0f);
        ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット位置", &events[i].offset.x, 0.1f);

        // 削除ボタン
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

    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " ＋ イベントを追加 (Add Event)", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
        // リストに中身があれば最初の名前を、なければ空文字を入れる
        std::string defaultName = particlePresetList_.empty() ? "" : particlePresetList_[0];
        events.push_back({ defaultName, 0.0f, {0.0f, 0.0f, 0.0f}, false });
    }

    ImGui::Separator();

    // =========================================================
    // 3. セーブ＆ロード
    // =========================================================
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " 保存と読み込み (Save & Load)", ImGuiTreeNodeFlags_DefaultOpen)) {

        // ★ シーケンスのロードもリストから選べるように！
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
            RefreshFileList(); // 保存したらリストを再取得
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " 読み込み (Load)", ImVec2(120, 0))) {
            previewSequencer_.Load(sequenceNameInput_);
        }
    }
}