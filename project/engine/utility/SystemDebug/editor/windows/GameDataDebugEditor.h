#pragma once

#include "IEditable.h"

#include <string>

class SceneManager;

class GameDataDebugEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "内部データ編集 (Game Data)"; }

private:
    void SyncSlotState();
    void RefreshStageSelectScene();
    void DrawSlotSelector();
    void DrawSlotSummary();
    void DrawCurrentSlotEditor();
    void DrawStageTable();
    void ApplyCrownCountToStages();

private:
    SceneManager* sceneManager_ = nullptr;
    int lastActiveSlot_ = -1;
    int crownTarget_ = 0;
    std::string lastStatus_ = "デバッグ用にセーブデータを直接編集できます。";
};
