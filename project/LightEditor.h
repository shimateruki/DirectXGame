#pragma once
#include <string>
#include "LightManager.h"

class LightEditor {
public:
    // 初期化
    void Initialize();

    // ImGui描画 (毎フレーム呼ぶ)
    void DrawImGui();

    // JSONへ保存
    void SaveLightLayout(const std::string& filename);
    // JSONから読み込み
    void LoadLightLayout(const std::string& filename);

private:
    LightManager* lightManager_ = nullptr;
    std::string currentSaveFile_ = "resources/light_layout.json"; // デフォルト保存先
};