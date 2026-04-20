#include "PostEffectEditor.h"
#include "imgui.h"
#include "json.hpp" // JSONライブラリ
#include <fstream>
#include <filesystem>
#include "IconsFontAwesome5.h"
using json = nlohmann::json;

void PostEffectEditor::Initialize(PostEffect* postEffect) {
    targetEffect_ = postEffect;
    // 起動時に自動で前回の設定をロードする
    LoadParams();
}

void PostEffectEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!targetEffect_) return;

    auto* params = targetEffect_->GetParams();

    // ==========================================================
    // トーンマッピングのモード切り替え
    // ==========================================================
    ImGui::Text(ICON_FA_ADJUST " Tone Mapping Mode");

    int mode = params->enableToneMapping;
    if (ImGui::RadioButton("OFF (No ToneMap)", mode == 0)) { mode = 0; }
    if (ImGui::RadioButton(ICON_FA_EYE " Real (ACES - White Out)", mode == 1)) { mode = 1; }
    if (ImGui::RadioButton(ICON_FA_PALETTE " Anime (ACES - Vivid Color)", mode == 2)) { mode = 2; }
    params->enableToneMapping = mode;

    if (mode == 1) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Good for Realistic/Movie style.");
    }
    else if (mode == 2) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "  Good for Anime/Vivid Magic effects.");
    }
    ImGui::Separator();

    // ==========================================================
    // ブルームの設定
    // ==========================================================
    ImGui::Text(ICON_FA_SUN " Bloom Settings");

    ImGui::DragFloat(" Threshold (発光の閾値)", &params->threshold, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat(" Intensity (光の強さ)", &params->bloomIntensity, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat(" Spread (ぼかしの広がり)", &params->spread, 0.1f, 0.0f, 10.0f);

    ImGui::Separator();

    // ==========================================================
    // シネマティック効果
    // ==========================================================
    ImGui::Text(ICON_FA_FILM " Cinematic Effects");
    ImGui::DragFloat(ICON_FA_MOON " Vignette (周辺減光)", &params->vignetteIntensity, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat(ICON_FA_COINS " Chromatic Aberration (色収差)", &params->chromaticAberration, 0.001f, 0.0f, 0.1f);
    ImGui::DragFloat(ICON_FA_BROADCAST_TOWER " Film Grain (ノイズ)", &params->filmGrainIntensity, 0.001f, 0.0f, 0.5f);

    ImGui::Spacing();
    ImGui::Text(ICON_FA_VIDEO " Action Camera Effects");
    ImGui::DragFloat(ICON_FA_EXPAND_ARROWS_ALT " Radial Blur (集中線の強さ)", &params->radialIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(" Radial Center X", &params->radialCenterX, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(" Radial Center Y", &params->radialCenterY, 0.01f, 0.0f, 1.0f);

    ImGui::Spacing();
    ImGui::Text(ICON_FA_GAMEPAD " Action Game Effects");
    ImGui::DragFloat(ICON_FA_EXCLAMATION_TRIANGLE " Damage Flash (被弾赤画面)", &params->damageFlash, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(ICON_FA_WINDOW_MINIMIZE " Cinema Bar (黒帯)", &params->cinemaBarHeight, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat(ICON_FA_WAVE_SQUARE " Wobble (波打ち)", &params->wobbleIntensity, 0.001f, 0.0f, 0.1f);

    ImGui::Separator();

    // ==========================================================
    // レトロ効果
    // ==========================================================
    ImGui::Text(ICON_FA_TV " Retro Effects");
    ImGui::DragFloat(ICON_FA_BARS " Scanline (ブラウン管)", &params->scanlineIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(ICON_FA_TH " Mosaic Size (ドット絵化)", &params->mosaicSize, 1.0f, 0.0f, 64.0f);
    ImGui::Spacing();
    ImGui::Text(ICON_FA_HEARTBEAT " Danger / Health Effects");
    ImGui::DragFloat(" Danger Vignette (瀕死赤枠)", &params->dangerVignette, 0.01f, 0.0f, 2.0f);
    ImGui::Separator();

    // ==========================================================
    // セーブ・ロードボタン
    // ==========================================================
    if (ImGui::Button(ICON_FA_SAVE " Save JSON")) {
        SaveParams();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UPLOAD " Load JSON")) {
        LoadParams();
    }
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
    j["radialIntensity"] = params->radialIntensity;
    j["radialCenterX"] = params->radialCenterX;
    j["radialCenterY"] = params->radialCenterY;
    j["lutIntensity"] = params->lutIntensity;
    j["damageFlash"] = params->damageFlash;
    j["cinemaBarHeight"] = params->cinemaBarHeight;
    j["wobbleIntensity"] = params->wobbleIntensity;
    j["scanlineIntensity"] = params->scanlineIntensity;
    j["mosaicSize"] = params->mosaicSize;
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
        if (j.contains("radialIntensity")) params->radialIntensity = j["radialIntensity"];
        if (j.contains("radialCenterX")) params->radialCenterX = j["radialCenterX"];
        if (j.contains("radialCenterY")) params->radialCenterY = j["radialCenterY"];
        if (j.contains("lutIntensity")) params->lutIntensity = j["lutIntensity"];
        if (j.contains("damageFlash")) params->damageFlash = j["damageFlash"];
        if (j.contains("cinemaBarHeight")) params->cinemaBarHeight = j["cinemaBarHeight"];
        if (j.contains("wobbleIntensity")) params->wobbleIntensity = j["wobbleIntensity"];
        if (j.contains("scanlineIntensity")) params->scanlineIntensity = j["scanlineIntensity"];
        if (j.contains("mosaicSize")) params->mosaicSize = j["mosaicSize"];

    }
    catch (...) {
        // ロード失敗時のエラーハンドリング
    }
}