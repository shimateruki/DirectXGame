#define NOMINMAX
#include "GameDataDebugEditor.h"

#include "DebugConsole.h"
#include "GameDataManager.h"
#include "GameSelectScene.h"
#include "IconsFontAwesome5.h"
#include "SceneManager.h"
#include "StageManager.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {
std::string FormatTime(int totalSeconds) {
    totalSeconds = std::max(totalSeconds, 0);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds / 60) % 60;
    const int seconds = totalSeconds % 60;

    char buffer[32]{};
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    }
    return buffer;
}

bool IsStageUnlockedForSave(const StageData& stage, int stageIndex, GameDataManager* save) {
    if (!save) {
        return false;
    }
    if (stage.defaultUnlocked || stageIndex == 0) {
        return true;
    }
    if (stage.unlockStageIndex < 0) {
        return false;
    }
    return save->IsStageCleared(stage.unlockStageIndex);
}
} // namespace

void GameDataDebugEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    StageManager::GetInstance()->Initialize();
    SyncSlotState();
}

void GameDataDebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    SyncSlotState();

    ImGui::Text(ICON_FA_DATABASE " ゲーム内部データ編集");
    ImGui::TextWrapped("デバッグ用に、選択中のセーブスロットとステージ進行状態を直接編集します。変更は保存JSONへ即時反映されます。");
    ImGui::Separator();

    DrawSlotSelector();
    ImGui::Separator();
    DrawSlotSummary();
    ImGui::Separator();
    DrawCurrentSlotEditor();
    ImGui::Separator();
    DrawStageTable();

    ImGui::Separator();
    ImGui::TextWrapped("%s", lastStatus_.c_str());
#endif
}

void GameDataDebugEditor::SyncSlotState() {
    GameDataManager* save = GameDataManager::GetInstance();
    const int activeSlot = save->GetActiveSlot();
    if (lastActiveSlot_ != activeSlot) {
        lastActiveSlot_ = activeSlot;
        crownTarget_ = save->GetClearedStageCount();
    }
}

void GameDataDebugEditor::RefreshStageSelectScene() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }

    if (auto* selectScene = dynamic_cast<GameSelectScene*>(sceneManager_->GetCurrentScene())) {
        selectScene->RefreshDebugStageStates();
    }
}

void GameDataDebugEditor::DrawSlotSelector() {
#ifdef USE_IMGUI
    GameDataManager* save = GameDataManager::GetInstance();
    int activeSlot = save->GetActiveSlot();

    ImGui::Text("編集対象スロット");
    for (int slot = 0; slot < GameDataManager::kSaveSlotCount; ++slot) {
        ImGui::PushID(slot);
        const std::string label = "Slot " + std::to_string(slot + 1);
        if (ImGui::RadioButton(label.c_str(), activeSlot == slot)) {
            save->SetActiveSlot(slot);
            SyncSlotState();
            RefreshStageSelectScene();
            lastStatus_ = "編集対象を Slot " + std::to_string(slot + 1) + " に切り替えました。";
        }
        if (slot + 1 < GameDataManager::kSaveSlotCount) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    ImGui::TextDisabled("保存先: %s", save->GetSaveFilePath().c_str());

    if (ImGui::Button(ICON_FA_SAVE " 保存")) {
        save->Save();
        lastStatus_ = "現在のスロットを保存しました。";
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC " 再読み込み")) {
        save->Load();
        SyncSlotState();
        RefreshStageSelectScene();
        lastStatus_ = "現在のスロットを保存JSONから再読み込みしました。";
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH " 初期化")) {
        ImGui::OpenPopup("ResetActiveSaveSlot");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TIMES_CIRCLE " 削除")) {
        ImGui::OpenPopup("DeleteActiveSaveSlot");
    }

    if (ImGui::BeginPopupModal("ResetActiveSaveSlot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("現在のスロットを初期状態に戻します。");
        ImGui::TextDisabled("ファイルは残り、値だけリセットされます。");
        if (ImGui::Button("初期化する", ImVec2(140.0f, 0.0f))) {
            save->ResetAll();
            SyncSlotState();
            RefreshStageSelectScene();
            lastStatus_ = "現在のスロットを初期化しました。";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("DeleteActiveSaveSlot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("現在のスロットの保存ファイルを削除します。");
        ImGui::TextDisabled("削除後は初期状態として扱われます。");
        if (ImGui::Button("削除する", ImVec2(140.0f, 0.0f))) {
            const int slot = save->GetActiveSlot();
            const bool removed = save->DeleteSlot(slot);
            SyncSlotState();
            RefreshStageSelectScene();
            lastStatus_ = removed ? "保存ファイルを削除しました。" : "保存ファイルは見つかりませんでした。初期状態で続行します。";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

void GameDataDebugEditor::DrawSlotSummary() {
#ifdef USE_IMGUI
    GameDataManager* save = GameDataManager::GetInstance();

    if (ImGui::BeginTable("GameDataSlotSummary", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("残機", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("コイン", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("王冠", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("スター", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("プレイ時間", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("チュートリアル", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int slot = 0; slot < GameDataManager::kSaveSlotCount; ++slot) {
            const auto summary = save->GetSlotSummary(slot);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Slot %d", slot + 1);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(summary.exists ? "あり" : "なし");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", summary.lives);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", summary.coins);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", summary.clearedStageCount);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d", summary.collectedStarCoins);
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(FormatTime(summary.playTimeSeconds).c_str());
            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(summary.tutorialCleared ? "完了" : "未完了");
        }

        ImGui::EndTable();
    }
#endif
}

void GameDataDebugEditor::DrawCurrentSlotEditor() {
#ifdef USE_IMGUI
    GameDataManager* save = GameDataManager::GetInstance();

    ImGui::Text("現在スロットの基本値");

    int lives = save->GetLives();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragInt("残機", &lives, 1.0f, 0, 99)) {
        save->SetLives(lives);
        lastStatus_ = "残機を変更しました。";
    }

    int coins = save->GetCoins();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragInt("コイン", &coins, 1.0f, 0, 9999)) {
        save->SetCoins(coins);
        lastStatus_ = "コイン数を変更しました。";
    }

    int totalSeconds = save->GetPlayTimeSeconds();
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds / 60) % 60;
    int seconds = totalSeconds % 60;
    bool timeChanged = false;
    ImGui::SetNextItemWidth(90.0f);
    timeChanged |= ImGui::DragInt("時", &hours, 1.0f, 0, 99);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    timeChanged |= ImGui::DragInt("分", &minutes, 1.0f, 0, 59);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    timeChanged |= ImGui::DragInt("秒", &seconds, 1.0f, 0, 59);
    if (timeChanged) {
        hours = std::clamp(hours, 0, 99);
        minutes = std::clamp(minutes, 0, 59);
        seconds = std::clamp(seconds, 0, 59);
        save->SetPlayTimeSeconds(hours * 3600 + minutes * 60 + seconds);
        lastStatus_ = "プレイ時間を変更しました。";
    }

    bool tutorialCleared = save->IsStageCleared(-1);
    if (ImGui::Checkbox("チュートリアル完了", &tutorialCleared)) {
        save->SetStageCleared(-1, tutorialCleared);
        RefreshStageSelectScene();
        lastStatus_ = "チュートリアル進行状態を変更しました。";
    }

    const int stageCount = static_cast<int>(StageManager::GetInstance()->GetStages().size());
    ImGui::Text("王冠数: %d / %d", save->GetClearedStageCount(), stageCount);
    ImGui::SameLine();
    if (ImGui::Button("現在値へ同期")) {
        crownTarget_ = save->GetClearedStageCount();
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragInt("王冠数を指定", &crownTarget_, 1.0f, 0, std::max(stageCount, 0));
    crownTarget_ = std::clamp(crownTarget_, 0, std::max(stageCount, 0));
    ImGui::SameLine();
    if (ImGui::Button("王冠数を反映")) {
        ApplyCrownCountToStages();
    }
#endif
}

void GameDataDebugEditor::DrawStageTable() {
#ifdef USE_IMGUI
    GameDataManager* save = GameDataManager::GetInstance();
    const auto& stages = StageManager::GetInstance()->GetStages();
    if (stages.empty()) {
        ImGui::TextDisabled("ステージ定義がありません。Resources/json/stage_select/stages.json を確認してください。");
        return;
    }

    if (ImGui::Button("全ステージクリア")) {
        for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
            save->SetStageCleared(stageIndex, true);
        }
        SyncSlotState();
        RefreshStageSelectScene();
        lastStatus_ = "全ステージをクリア済みにしました。";
    }
    ImGui::SameLine();
    if (ImGui::Button("全ステージ未クリア")) {
        for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
            save->SetStageCleared(stageIndex, false);
        }
        SyncSlotState();
        RefreshStageSelectScene();
        lastStatus_ = "全ステージを未クリアに戻しました。";
    }
    ImGui::SameLine();
    if (ImGui::Button("全スター取得")) {
        for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
            for (int coinIndex = 0; coinIndex < 3; ++coinIndex) {
                save->SetStarCoinCollected(stageIndex, coinIndex, true);
            }
        }
        lastStatus_ = "全スターを取得済みにしました。";
    }
    ImGui::SameLine();
    if (ImGui::Button("全スター未取得")) {
        for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
            for (int coinIndex = 0; coinIndex < 3; ++coinIndex) {
                save->SetStarCoinCollected(stageIndex, coinIndex, false);
            }
        }
        lastStatus_ = "全スターを未取得に戻しました。";
    }

    if (ImGui::BeginTable("GameDataStageTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 360.0f))) {
        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("ステージ", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("解放条件", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn("解放中", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("クリア", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("解放演出済み", ImGuiTableColumnFlags_WidthFixed, 104.0f);
        ImGui::TableSetupColumn("スター", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
            const StageData& stage = stages[stageIndex];
            const bool unlocked = IsStageUnlockedForSave(stage, stageIndex, save);

            ImGui::PushID(stageIndex);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", stageIndex);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(stage.name.c_str());
            if (!stage.description.empty()) {
                ImGui::TextDisabled("%s", stage.description.c_str());
            }
            ImGui::TextDisabled("%s", stage.levelPath.c_str());

            ImGui::TableSetColumnIndex(2);
            if (stage.defaultUnlocked || stageIndex == 0) {
                ImGui::TextUnformatted("初期解放");
            } else {
                ImGui::Text("Stage %d", stage.unlockStageIndex);
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(unlocked ? "はい" : "いいえ");

            ImGui::TableSetColumnIndex(4);
            bool cleared = save->IsStageCleared(stageIndex);
            if (ImGui::Checkbox("##cleared", &cleared)) {
                save->SetStageCleared(stageIndex, cleared);
                SyncSlotState();
                RefreshStageSelectScene();
                lastStatus_ = stage.name + " のクリア状態を変更しました。";
            }

            ImGui::TableSetColumnIndex(5);
            bool seen = save->IsStageUnlockSeen(stageIndex);
            if (ImGui::Checkbox("##seen", &seen)) {
                save->SetStageUnlockSeen(stageIndex, seen);
                RefreshStageSelectScene();
                lastStatus_ = stage.name + " の解放演出済み状態を変更しました。";
            }

            ImGui::TableSetColumnIndex(6);
            for (int coinIndex = 0; coinIndex < 3; ++coinIndex) {
                ImGui::PushID(coinIndex);
                bool collected = save->IsStarCoinCollected(stageIndex, coinIndex);
                if (ImGui::Checkbox("##star", &collected)) {
                    save->SetStarCoinCollected(stageIndex, coinIndex, collected);
                    lastStatus_ = stage.name + " のスター取得状態を変更しました。";
                }
                if (coinIndex < 2) {
                    ImGui::SameLine();
                }
                ImGui::PopID();
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
#endif
}

void GameDataDebugEditor::ApplyCrownCountToStages() {
    GameDataManager* save = GameDataManager::GetInstance();
    const auto& stages = StageManager::GetInstance()->GetStages();
    const int target = std::clamp(crownTarget_, 0, static_cast<int>(stages.size()));

    for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
        save->SetStageCleared(stageIndex, stageIndex < target);
    }

    crownTarget_ = target;
    SyncSlotState();
    RefreshStageSelectScene();
    lastStatus_ = "王冠数を " + std::to_string(target) + " に合わせました。先頭ステージから順にクリア扱いにしています。";
    DebugConsole::GetInstance()->AddLog("GameData Debug: crown count applied.");
}
