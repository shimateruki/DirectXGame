#pragma once

#include "IEditable.h"

#include <string>

class SceneManager;

/// ゲーム固有の進行データ拡張を登録するための案内ウィンドウ。
class GameDataDebugEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "内部データ編集 (Game Data)"; }

private:
    SceneManager* sceneManager_ = nullptr;
};
