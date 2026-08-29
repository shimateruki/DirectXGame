#include "AudioSettingsWindow.h"

#include "AudioPlayer.h"
#include "GameAudioSettings.h"
#include "GameSettingsManager.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>

#include <filesystem>
#include <vector>
namespace {
const char* GetCategoryLabel(const std::string& category) {
    return category == "BGM" ? "BGM" : "SE";
}
}

void AudioSettingsWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    GameAudioSettings::GetInstance()->Initialize();
}

void AudioSettingsWindow::DrawAudioEventEditor() {
#ifdef USE_IMGUI
    if (!audioEventInitialized_) {
        // 新規プロジェクトでは旧ゲームのSEを引き継がず、空のEventから作成します。
        audioEventDefinition_.clips.clear();
        audioEventInitialized_ = true;
    }

    if (!ImGui::CollapsingHeader(
        ICON_FA_VOLUME_UP " Audio Event",
        ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::TextWrapped(
        "複数clipのランダム再生、音量・Pitch揺らぎ、同時発音数、距離減衰を1つのJSONで管理します。");

    const std::filesystem::path directory("Resources/json/audio_events");
    std::vector<std::string> eventFiles;
    std::error_code error;
    if (std::filesystem::exists(directory, error) && !error) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                eventFiles.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(eventFiles.begin(), eventFiles.end());

    if (ImGui::BeginCombo("Event一覧", audioEventFileName_)) {
        for (const std::string& fileName : eventFiles) {
            const bool selected = fileName == audioEventFileName_;
            if (ImGui::Selectable(fileName.c_str(), selected)) {
                strncpy_s(
                    audioEventFileName_,
                    sizeof(audioEventFileName_),
                    fileName.c_str(),
                    _TRUNCATE);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::InputText(
        "Eventファイル名",
        audioEventFileName_,
        sizeof(audioEventFileName_));

    std::filesystem::path eventPath =
        directory / std::filesystem::path(audioEventFileName_);
    if (eventPath.extension().empty()) {
        eventPath.replace_extension(".json");
    }
    const std::string eventPathString = eventPath.generic_string();

    if (ImGui::Button(ICON_FA_UPLOAD " 読込##AudioEvent")) {
        std::string message;
        const bool loaded = AudioEventSystem::GetInstance()->LoadEvent(
            eventPathString,
            audioEventDefinition_,
            &message);
        statusText_ = loaded ? "Audio Eventを読み込みました。" : message;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SAVE " 保存##AudioEvent")) {
        std::string message;
        const bool saved = AudioEventSystem::GetInstance()->SaveEvent(
            eventPathString,
            audioEventDefinition_,
            &message);
        statusText_ = saved ? "Audio Eventを保存しました。" : message;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLAY " 試聴##AudioEvent")) {
        const auto handle =
            AudioEventSystem::GetInstance()->Play(eventPathString);
        statusText_ = handle != AudioPlayer::kInvalidPlaybackHandle
            ? "Audio Eventを再生しました。"
            : "Audio Eventを再生できませんでした。保存内容を確認してください。";
    }

    ImGui::InputText(
        "追加するclip",
        audioEventClipPath_,
        sizeof(audioEventClipPath_));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS " 追加##AudioEventClip") &&
        audioEventClipPath_[0] != '\0') {
        const std::string clipPath = audioEventClipPath_;
        if (std::find(
            audioEventDefinition_.clips.begin(),
            audioEventDefinition_.clips.end(),
            clipPath) == audioEventDefinition_.clips.end()) {
            audioEventDefinition_.clips.push_back(clipPath);
        }
    }

    for (std::size_t index = 0;
        index < audioEventDefinition_.clips.size();) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextUnformatted(audioEventDefinition_.clips[index].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_TRASH)) {
            audioEventDefinition_.clips.erase(
                audioEventDefinition_.clips.begin() + index);
            ImGui::PopID();
            continue;
        }
        ImGui::PopID();
        ++index;
    }

    ImGui::DragFloatRange2(
        "音量範囲",
        &audioEventDefinition_.volumeMin,
        &audioEventDefinition_.volumeMax,
        0.01f,
        0.0f,
        1.0f,
        "Min %.2f",
        "Max %.2f");
    ImGui::DragFloatRange2(
        "Pitch範囲",
        &audioEventDefinition_.pitchMin,
        &audioEventDefinition_.pitchMax,
        0.01f,
        0.5f,
        2.0f,
        "Min %.2f",
        "Max %.2f");
    ImGui::DragInt(
        "最大同時発音数",
        &audioEventDefinition_.maxInstances,
        1.0f,
        1,
        64);
    ImGui::Checkbox("3D距離減衰", &audioEventDefinition_.spatial);
    if (audioEventDefinition_.spatial) {
        ImGui::DragFloat(
            "減衰開始距離",
            &audioEventDefinition_.minDistance,
            0.1f,
            0.0f,
            1000.0f);
        ImGui::DragFloat(
            "無音になる距離",
            &audioEventDefinition_.maxDistance,
            0.1f,
            audioEventDefinition_.minDistance + 0.01f,
            2000.0f);
    }
    ImGui::Separator();
#endif
}
void AudioSettingsWindow::DrawImGui() {
#ifdef USE_IMGUI
    GameSettingsManager* gameSettings = GameSettingsManager::GetInstance();
    GameAudioSettings* audioSettings = GameAudioSettings::GetInstance();
    audioSettings->Initialize();

    ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_MUSIC " 音声設定");
    ImGui::TextWrapped("全体音量と、SE/BGMごとの個別音量を調整して保存できます。");
    ImGui::Separator();

    float seMaster = gameSettings->GetSEVolume();
    float bgmMaster = gameSettings->GetBGMVolume();

    if (ImGui::SliderFloat("SE 全体音量", &seMaster, 0.0f, 1.0f, "%.2f")) {
        gameSettings->SetSEVolume(seMaster);
    }
    if (ImGui::SliderFloat("BGM 全体音量", &bgmMaster, 0.0f, 1.0f, "%.2f")) {
        gameSettings->SetBGMVolume(bgmMaster);
    }

    if (ImGui::Button(ICON_FA_SAVE " 保存")) {
        gameSettings->Save();
        audioSettings->Save();
        statusText_ = "音声設定を保存しました。";
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " 再読み込み")) {
        gameSettings->Load();
        audioSettings->Load();
        statusText_ = "音声設定を再読み込みしました。";
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " BGM停止")) {
        AudioPlayer::GetInstance()->StopBGM();
    }

    ImGui::TextColored(ImVec4(0.65f, 1.0f, 0.65f, 1.0f), "%s", statusText_.c_str());
    ImGui::Separator();

    DrawAudioEventEditor();

    ImGui::InputTextWithHint("検索", "id / 表示名 / パス", searchBuffer_, sizeof(searchBuffer_));

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("AudioSettingsTable", 8, tableFlags, ImVec2(0.0f, 430.0f))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("表示名", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("種類", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("個別音量", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableHeadersRow();

        auto& entries = audioSettings->GetEntries();
        for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
            auto& entry = entries[index];
            const std::string searchText = entry.id + " " + entry.displayName + " " + entry.category + " " + entry.path;
            if (!MatchesSearch(searchText)) {
                continue;
            }

            ImGui::PushID(index);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(entry.id.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.displayName.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(GetCategoryLabel(entry.category));

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SliderFloat("##volume", &entry.volume, 0.0f, 1.0f, "%.2f")) {
                entry.volume = std::clamp(entry.volume, 0.0f, 1.0f);
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f", entry.volume);

            ImGui::TableSetColumnIndex(5);
            ImGui::Checkbox("##loop", &entry.loop);

            ImGui::TableSetColumnIndex(6);
            if (ImGui::SmallButton(ICON_FA_PLAY " 試聴")) {
                if (entry.category == "BGM") {
                    audioSettings->PlayBGM(entry.id, 1.0f, entry.loop);
                } else {
                    audioSettings->PlaySE(entry.id, 1.0f, entry.loop);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_STOP)) {
                audioSettings->Stop(entry.id);
            }

            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(entry.path.c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
#endif
}

bool AudioSettingsWindow::MatchesSearch(const std::string& text) const {
    if (searchBuffer_[0] == '\0') {
        return true;
    }

    std::string haystack = text;
    std::string needle = searchBuffer_;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}
