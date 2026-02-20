#pragma once
#include "PostEffect.h"
#include <string>

class PostEffectEditor {
public:
    // 初期化時に PostEffect のポインタを受け取る
    void Initialize(PostEffect* postEffect);

    // ImGui描画用
    void DrawImGui();

    // セーブ＆ロード関数 (デフォルトのファイル名付き)
    void SaveParams(const std::string& filename = "Resources/json/post_effect.json");
    void LoadParams(const std::string& filename = "Resources/json/post_effect.json");

private:
    PostEffect* targetEffect_ = nullptr;
};