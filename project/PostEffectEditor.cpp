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

    // ==========================================================
        // トーンマッピングのモード切り替え
        // ==========================================================
    ImGui::Text("Tone Mapping Mode");

    int mode = params->enableToneMapping;
    if (ImGui::RadioButton("OFF (No ToneMap)", mode == 0)) { mode = 0; }
    if (ImGui::RadioButton("Real (ACES - White Out)", mode == 1)) { mode = 1; }
    if (ImGui::RadioButton("Anime (ACES - Vivid Color)", mode == 2)) { mode = 2; }
    params->enableToneMapping = mode;

    if (mode == 1) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Good for Realistic/Movie style.");
    } else if (mode == 2) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Good for Anime/Vivid Magic effects.");
    }
    ImGui::Separator();

    // ==========================================================
    // ブルームの設定
    // ==========================================================
    ImGui::Text("Bloom Settings");

    // 閾値の上限を少し上げておく (パーティクルだけ光らせる調整用)
    ImGui::DragFloat("Threshold (発光の閾値)", &params->threshold, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Intensity (光の強さ)", &params->bloomIntensity, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Spread (ぼかしの広がり)", &params->spread, 0.1f, 0.0f, 10.0f);

    ImGui::Separator();
    ImGui::Text("Cinematic Effects");
    ImGui::DragFloat("Vignette (周辺減光)", &params->vignetteIntensity, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Chromatic Aberration (色収差)", &params->chromaticAberration, 0.001f, 0.0f, 0.1f);
    ImGui::DragFloat("Film Grain (ノイズ)", &params->filmGrainIntensity, 0.001f, 0.0f, 0.5f);


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
    j["enableToneMapping"] = params->enableToneMapping;
    j["vignetteIntensity"] = params->vignetteIntensity;
    j["chromaticAberration"] = params->chromaticAberration;
    j["filmGrainIntensity"] = params->filmGrainIntensity;

    std::ofstream file(filename);
    if (file.is_open()) {
        file << j.dump(4); // インデント4で綺麗に出力
    }
}

void PostEffectEditor::LoadParams(const std::string& filename) {
    if (!targetEffect_) return;

    std::ifstream file(filename);
    if (!file.is_open()) return;

    try {
        json j;
        file >> j;

        auto* params = targetEffect_->GetParams();

        if (j.contains("threshold")) params->threshold = j["threshold"];
        if (j.contains("bloomIntensity")) params->bloomIntensity = j["bloomIntensity"];
        if (j.contains("spread")) params->spread = j["spread"];
        if (j.contains("enableToneMapping")) params->enableToneMapping = j["enableToneMapping"];
        if (j.contains("vignetteIntensity")) params->vignetteIntensity = j["vignetteIntensity"];
        if (j.contains("chromaticAberration")) params->chromaticAberration = j["chromaticAberration"];
        if (j.contains("filmGrainIntensity")) params->filmGrainIntensity = j["filmGrainIntensity"];

    }
    catch (...) {
        // ロード失敗時のエラーハンドリング（必要ならログ出力など）
    }
}