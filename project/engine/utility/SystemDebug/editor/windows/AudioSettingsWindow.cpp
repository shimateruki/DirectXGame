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
    // 音源ファイルとイベント設定を分けて扱い、イベント側の調整値を編集します。
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(
        ICON_FA_VOLUME_UP " Audio Event",
        ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::TextWrapped(
        "複数clipのランダム再生、音量・Pitch揺らぎ、同時発音数、距離減衰を1つのJSONで管理します。");

    // ディスク上の Event 定義を毎回列挙し、別ツールで追加された JSON も一覧へ反映します。
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

    // 入力名に拡張子がなければ JSON を補い、読込・保存・試聴で同じ Path を使います。
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

    // Clip の重複を避けます。削除時は次の要素が同じ位置へ詰まるので index を進めません。
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

    // Event 全体の再生揺らぎと発音制限を編集します。距離減衰は 3D 指定時だけ表示します。
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
    // 現在の設定を表示し、変更操作は Audio の設定管理へ渡します。
#ifdef USE_IMGUI
    GameSettingsManager* gameSettings = GameSettingsManager::GetInstance();
    GameAudioSettings* audioSettings = GameAudioSettings::GetInstance();
    audioSettings->Initialize();

    ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_MUSIC " 音声設定");
    ImGui::TextWrapped("全体音量と、SE/BGMごとの個別音量を調整して保存できます。");
    ImGui::Separator();

    // マスター音量は操作時に反映します。保存操作で Game 設定と音源一覧を両方永続化します。
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

    // 新規登録欄はファイルを検索して選べる一方、入力した ID の重複と Path の実在も検証します。
    ImGui::SeparatorText("音声を登録");
    ImGui::InputText("ID##NewAudio", newAudioId_, sizeof(newAudioId_));
    ImGui::InputText("表示名##NewAudio", newAudioName_, sizeof(newAudioName_));
    ImGui::InputText("音源パス##NewAudio", newAudioPath_, sizeof(newAudioPath_));
    ImGui::Checkbox("BGM##NewAudio", &newAudioBgm_);
    if (ImGui::BeginCombo("音源ファイルから選択", newAudioPath_[0] ? newAudioPath_ : "(未選択)")) {
        const std::filesystem::path audioDirectory("Resources/audio");
        std::error_code error;
        if (std::filesystem::exists(audioDirectory, error) && !error) {
            for (std::filesystem::recursive_directory_iterator it(
                     audioDirectory, std::filesystem::directory_options::skip_permission_denied, error), end;
                 it != end && !error; it.increment(error)) {
                if (!it->is_regular_file(error) || error) {
                    continue;
                }
                const std::string extension = it->path().extension().string();
                if (extension != ".wav" && extension != ".mp3" && extension != ".ogg" && extension != ".flac") {
                    continue;
                }
                const std::string path = it->path().generic_string();
                if (ImGui::Selectable(path.c_str(), path == newAudioPath_)) {
                    strncpy_s(newAudioPath_, sizeof(newAudioPath_), path.c_str(), _TRUNCATE);
                }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button(ICON_FA_PLUS " 音声を追加")) {
        const std::filesystem::path path(newAudioPath_);
        if (newAudioId_[0] == '\0' || newAudioPath_[0] == '\0') {
            statusText_ = "IDと音源ファイルを指定してください。";
        } else if (!std::filesystem::is_regular_file(path)) {
            statusText_ = "音源ファイルが見つかりません。";
        } else {
            GameAudioSettings::SoundEntry entry;
            entry.id = newAudioId_;
            entry.displayName = newAudioName_[0] ? newAudioName_ : newAudioId_;
            entry.category = newAudioBgm_ ? "BGM" : "SE";
            entry.path = path.generic_string();
            if (audioSettings->AddEntry(entry)) {
                audioSettings->Save();
                newAudioId_[0] = newAudioName_[0] = newAudioPath_[0] = '\0';
                statusText_ = "音声を登録して保存しました。";
            } else {
                statusText_ = "そのIDは既に登録されています。";
            }
        }
    }

    ImGui::InputTextWithHint("検索", "id / 表示名 / パス", searchBuffer_, sizeof(searchBuffer_));

    // 一覧は検索で絞り、各行の音量・Loop・試聴・停止をその場で操作します。
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
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 165.0f);
        ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableHeadersRow();

        // 行の反復中には削除せず ID を退避し、Table 終了後に保存します。
        auto& entries = audioSettings->GetEntries();
        std::string removeId;
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
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_TRASH)) {
                removeId = entry.id;
            }

            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(entry.path.c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
        if (!removeId.empty() && audioSettings->RemoveEntry(removeId)) {
            audioSettings->Save();
            statusText_ = "音声を削除して保存しました。";
        }
    }
#endif
}

bool AudioSettingsWindow::MatchesSearch(const std::string& text) const {
    // 検索欄が空なら全件を表示し、入力時だけASCIIの大小文字を揃えて比較する。
    if (searchBuffer_[0] == '\0') {
        return true;
    }

    std::string haystack = text;
    std::string needle = searchBuffer_;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}
