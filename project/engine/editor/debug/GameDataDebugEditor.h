#pragma once

#include "IEditable.h"

#include <string>

class SceneManager;

// 担当：セーブスロットとステージ進行をデバッグ用途で確認・編集する。
// 関連：SceneManager（シーン）、ゲーム側のセーブ・進行管理（編集先）
// 主な入口：Initialize / DrawImGui
class GameDataDebugEditor : public IEditable {
public:
    // セーブスロットとStage定義を先に同期し、編集対象を現在の実データへ向けます。
    void Initialize(SceneManager* sceneManager);

    // このツールの設定・操作UIをImGuiで描画する。
    void DrawImGui() override;

    std::string GetName() override { return "内部データ編集 (Game Data)"; }

private:
    /// 現在選択中のセーブスロット状態をUI内部の表示値へ同期する。
    void SyncSlotState();

    // Stage Select表示中だけ、変更済みの進行状態を画面へ反映します。
    void RefreshStageSelectScene();

    /// 編集対象にするセーブスロットを選ぶUIを描画する。
    void DrawSlotSelector();

    // 選択セーブスロットの進行概要を表示する。
    void DrawSlotSummary();

    // 選択セーブスロットの編集項目を描画する。
    void DrawCurrentSlotEditor();

    /// ステージ解放状態や王冠取得状態を一覧表で表示・編集する。
    void DrawStageTable();

    /// 指定した王冠数をステージデータへ反映し、進行状態テストをしやすくする。
    void ApplyCrownCountToStages();

private:
    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    int lastActiveSlot_ = -1;
    int crownTarget_ = 0;
    std::string lastStatus_ = "デバッグ用にセーブデータを直接編集できます。";
};
