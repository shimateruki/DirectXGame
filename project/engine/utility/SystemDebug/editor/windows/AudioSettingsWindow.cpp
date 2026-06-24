#include "AudioSettingsWindow.h"

#include "AudioPlayer.h"
#include "GameAudioSettings.h"
#include "GameSettingsManager.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>

namespace {
const char* GetCategoryLabel(const std::string& category) {
    return category == "BGM" ? "BGM" : "SE";
}
}

void AudioSettingsWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    GameAudioSettings::GetInstance()->Initialize();
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
