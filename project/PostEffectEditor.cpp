#include "PostEffectEditor.h"
#include "imgui.h"
#include "json.hpp" // JSONライブラリ
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

void PostEffectEditor::Initialize(PostEffect* postEffect) {
    targetEffect_ = postEffect;
    // 起動時に自動で前回の設定をロードする
    LoadParams();
}

void PostEffectEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!targetEffect_) return;

    ImGui::Begin("Post Effect Editor");

    auto* params = targetEffect_->GetParams();
    ImGui::Text("Bloom Settings");

    // パラメータの編集
    ImGui::DragFloat("Threshold (発光の閾値)", &params->threshold, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Intensity (光の強さ)", &params->bloomIntensity, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Spread (ぼかしの広がり)", &params->spread, 0.1f, 0.0f, 10.0f);

    ImGui::Separator();

    // セーブ・ロードボタン
    if (ImGui::Button("Save JSON")) {
        SaveParams();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load JSON")) {
        LoadParams();
    }

    ImGui::End();
#endif
}

void PostEffectEditor::SaveParams(const std::string& filename) {
    if (!targetEffect_) return;

    // フォルダが存在しない場合は作成する
    std::filesystem::path dir = std::filesystem::path(filename).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    auto* params = targetEffect_->GetParams();
    json j;
    j["threshold"] = params->threshold;
    j["bloomIntensity"] = params->bloomIntensity;
    j["spread"] = params->spread;

    std::ofstream file(filename);
    if (file.is_open()) {
        file << j.dump(4); // インデント4で綺麗に出力
    }
}

void PostEffectEditor::LoadParams(const std::string& filename) {
    if (!targetEffect_) return;

    std::ifstream file(filename);
    if (file.is_open()) {
        json j;
        file >> j;

        auto* params = targetEffect_->GetParams();
        // キーが存在するか確認してから代入
        if (j.contains("threshold")) params->threshold = j["threshold"];
        if (j.contains("bloomIntensity")) params->bloomIntensity = j["bloomIntensity"];
        if (j.contains("spread")) params->spread = j["spread"];
    }
}