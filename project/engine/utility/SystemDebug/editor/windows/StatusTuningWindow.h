#pragma once

#include "IEditable.h"

#include <string>

class DebugEditor;
class SceneManager;

/// ゲーム固有のステータス拡張を登録するための案内ウィンドウ。
class StatusTuningWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor, SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "ステータス管理"; }

private:
    DebugEditor* editor_ = nullptr;
    SceneManager* sceneManager_ = nullptr;
};
