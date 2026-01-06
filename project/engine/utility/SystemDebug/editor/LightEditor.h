#pragma once
#include <string>
#include "LightManager.h"

class LightEditor {
public:
    // 初期化
    void Initialize();

    // ImGui描画 (毎フレーム呼ぶ)
    void DrawImGui();



private:
    LightManager* lightManager_ = nullptr;
    char currentFileName_[128] = "light_layout.json";
};