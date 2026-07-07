#pragma once

#include "IEditable.h"

#include <string>

class SceneManager;

/// セーブスロットやステージ進行状態をデバッグ用途で直接確認・編集するウィンドウ。
class GameDataDebugEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "内部データ編集 (Game Data)"; }

private:
    /// 現在選択中のセーブスロット状態をUI内部の表示値へ同期する。
    void SyncSlotState();
    void RefreshStageSelectScene();
    /// 編集対象にするセーブスロットを選ぶUIを描画する。
    void DrawSlotSelector();
    void DrawSlotSummary();
    void DrawCurrentSlotEditor();
    /// ステージ解放状態や王冠取得状態を一覧表で表示・編集する。
    void DrawStageTable();
    /// 指定した王冠数をステージデータへ反映し、進行状態テストをしやすくする。
    void ApplyCrownCountToStages();

private:
    SceneManager* sceneManager_ = nullptr;
    int lastActiveSlot_ = -1;
    int crownTarget_ = 0;
    std::string lastStatus_ = "デバッグ用にセーブデータを直接編集できます。";
};
