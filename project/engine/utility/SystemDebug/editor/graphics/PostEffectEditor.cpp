#include "PostEffectEditor.h"
#include "imgui.h"
#include "json.hpp" // JSONライブラリ
#include <fstream>
#include <filesystem>
#include "IconsFontAwesome5.h"
#include "Fade.h"
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
    ImGui::DragFloat(ICON_FA_MOON " Vignette (周辺減光強度)", &params->vignetteIntensity, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat(" Vignette Power (減光の丸み)", &params->vignettePower, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat(ICON_FA_COINS " Chromatic Aberration (色収差)", &params->chromaticAberration, 0.001f, 0.0f, 0.1f);
    ImGui::DragFloat(ICON_FA_BROADCAST_TOWER " Film Grain (ノイズ)", &params->filmGrainIntensity, 0.001f, 0.0f, 0.5f);

    ImGui::Spacing();
    ImGui::Text(ICON_FA_VIDEO " Action Camera Effects");
    ImGui::DragFloat(ICON_FA_EXPAND_ARROWS_ALT " Radial Blur (集中線の強さ)", &params->radialIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(" Radial Center X", &params->radialCenterX, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(" Radial Center Y", &params->radialCenterY, 0.01f, 0.0f, 1.0f);
    ImGui::DragInt(" Radial Blur Samples (サンプリング数)", &params->radialBlurSamples, 0.1f, 1, 64);

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
    ImGui::Spacing();
    ImGui::Text(ICON_FA_PALETTE " Color Effect");
    ImGui::DragFloat(" Grayscale (モノクロ)", &params->grayscaleIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(" Sepia (セピア調)", &params->sepiaIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::Spacing();
    ImGui::Text(ICON_FA_CLOUD " Blur Effects (資料)");
    ImGui::DragInt(" Box Filter Size (0:Off, 1:3x3...)", &params->boxFilterSize, 0.1f, 0, 10);
    ImGui::DragInt(" Gaussian Filter Size (0:Off, 1:3x3...)", &params->gaussianFilterSize, 0.1f, 0, 10);
    ImGui::DragFloat(" Gaussian Sigma", &params->gaussianSigma, 0.01f, 0.1f, 10.0f);
    ImGui::Spacing();
    ImGui::Text(ICON_FA_SQUARE " Outline Effects (資料)");
    ImGui::DragFloat(" Luminance Outline (輝度ベース)", &params->luminanceOutlineIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(" Depth Outline (深度ベース)", &params->depthOutlineIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::Spacing();
    ImGui::Text(ICON_FA_FIRE " Dissolve (資料)");
    ImGui::DragFloat(" Dissolve Threshold", &params->dissolveThreshold, 0.001f, 0.0f, 1.1f);
    ImGui::DragFloat(" Dissolve Edge Width", &params->dissolveEdgeWidth, 0.001f, 0.0f, 0.5f);
    ImGui::ColorEdit3(" Dissolve Edge Color", &params->dissolveEdgeColor.x);
    ImGui::Spacing();
    ImGui::Text(ICON_FA_DICE " Random (資料)");
    ImGui::DragFloat(" Random Intensity", &params->randomIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::Separator();

    // ==========================================================
    // スライムフェードの設定
    // ==========================================================
    ImGui::Text(ICON_FA_WATER " Slime Fade Settings");
    ImGui::DragFloat(" Slime Intensity", &params->slimeFadeIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(" Slime Density", &params->slimeDensity, 0.1f, 0.1f, 10.0f);
    ImGui::ColorEdit3(" Slime Color", &params->slimeColor.x);
    
    ImGui::Spacing();
    if (ImGui::Button("Start Fade In")) {
        Fade::GetInstance()->StartFadeIn(1.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Start Fade Out")) {
        Fade::GetInstance()->StartFadeOut(1.5f);
    }
    ImGui::Separator();

    // ==========================================================
    // アイリスフェードの設定
    // ==========================================================
    ImGui::Text(ICON_FA_DOT_CIRCLE " Iris Fade Settings");
    ImGui::DragFloat(" Iris Intensity", &params->irisFadeIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat2(" Iris Center", &params->irisCenterX, 0.01f, 0.0f, 1.0f);
    
    ImGui::Spacing();
    if (ImGui::Button("Start Iris In")) {
        Fade::GetInstance()->StartIrisIn(1.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Start Iris Out")) {
        Fade::GetInstance()->StartIrisOut(1.0f);
    }
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
    j["vignettePower"] = params->vignettePower;
    j["chromaticAberration"] = params->chromaticAberration;
    j["filmGrainIntensity"] = params->filmGrainIntensity;
    j["radialIntensity"] = params->radialIntensity;
    j["radialBlurSamples"] = params->radialBlurSamples;
    j["radialCenterX"] = params->radialCenterX;
    j["radialCenterY"] = params->radialCenterY;
    j["lutIntensity"] = params->lutIntensity;
    j["damageFlash"] = params->damageFlash;
    j["cinemaBarHeight"] = params->cinemaBarHeight;
    j["wobbleIntensity"] = params->wobbleIntensity;
    j["scanlineIntensity"] = params->scanlineIntensity;
    j["mosaicSize"] = params->mosaicSize;
    j["grayscaleIntensity"] = params->grayscaleIntensity;
    j["sepiaIntensity"] = params->sepiaIntensity;
    j["boxFilterSize"] = params->boxFilterSize;
    j["gaussianFilterSize"] = params->gaussianFilterSize;
    j["gaussianSigma"] = params->gaussianSigma;
    j["luminanceOutlineIntensity"] = params->luminanceOutlineIntensity;
    j["depthOutlineIntensity"] = params->depthOutlineIntensity;
    j["dissolveThreshold"] = params->dissolveThreshold;
    j["dissolveEdgeWidth"] = params->dissolveEdgeWidth;
    j["dissolveEdgeColor"] = { params->dissolveEdgeColor.x, params->dissolveEdgeColor.y, params->dissolveEdgeColor.z };
    j["randomIntensity"] = params->randomIntensity;
    j["slimeFadeIntensity"] = params->slimeFadeIntensity;
    j["slimeDensity"] = params->slimeDensity;
    j["slimeColor"] = { params->slimeColor.x, params->slimeColor.y, params->slimeColor.z };
    j["irisFadeIntensity"] = params->irisFadeIntensity;
    j["irisCenter"] = { params->irisCenterX, params->irisCenterY };
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
        if (j.contains("vignettePower")) params->vignettePower = j["vignettePower"];
        if (j.contains("chromaticAberration")) params->chromaticAberration = j["chromaticAberration"];
        if (j.contains("filmGrainIntensity")) params->filmGrainIntensity = j["filmGrainIntensity"];
        if (j.contains("radialIntensity")) params->radialIntensity = j["radialIntensity"];
        if (j.contains("radialBlurSamples")) params->radialBlurSamples = j["radialBlurSamples"];
        if (j.contains("radialCenterX")) params->radialCenterX = j["radialCenterX"];
        if (j.contains("radialCenterY")) params->radialCenterY = j["radialCenterY"];
        if (j.contains("lutIntensity")) params->lutIntensity = j["lutIntensity"];
        if (j.contains("damageFlash")) params->damageFlash = j["damageFlash"];
        if (j.contains("cinemaBarHeight")) params->cinemaBarHeight = j["cinemaBarHeight"];
        if (j.contains("wobbleIntensity")) params->wobbleIntensity = j["wobbleIntensity"];
        if (j.contains("scanlineIntensity")) params->scanlineIntensity = j["scanlineIntensity"];
        if (j.contains("mosaicSize")) params->mosaicSize = j["mosaicSize"];
        if (j.contains("grayscaleIntensity")) params->grayscaleIntensity = j["grayscaleIntensity"];
        if (j.contains("sepiaIntensity")) params->sepiaIntensity = j["sepiaIntensity"];
        if (j.contains("boxFilterSize")) params->boxFilterSize = j["boxFilterSize"];
        if (j.contains("gaussianFilterSize")) params->gaussianFilterSize = j["gaussianFilterSize"];
        if (j.contains("gaussianSigma")) params->gaussianSigma = j["gaussianSigma"];
        if (j.contains("luminanceOutlineIntensity")) params->luminanceOutlineIntensity = j["luminanceOutlineIntensity"];
        if (j.contains("depthOutlineIntensity")) params->depthOutlineIntensity = j["depthOutlineIntensity"];
        if (j.contains("dissolveThreshold")) params->dissolveThreshold = j["dissolveThreshold"];
        if (j.contains("dissolveEdgeWidth")) params->dissolveEdgeWidth = j["dissolveEdgeWidth"];
        if (j.contains("dissolveEdgeColor")) {
            params->dissolveEdgeColor.x = j["dissolveEdgeColor"][0];
            params->dissolveEdgeColor.y = j["dissolveEdgeColor"][1];
            params->dissolveEdgeColor.z = j["dissolveEdgeColor"][2];
        }
        if (j.contains("randomIntensity")) params->randomIntensity = j["randomIntensity"];
        if (j.contains("slimeFadeIntensity")) params->slimeFadeIntensity = j["slimeFadeIntensity"];
        if (j.contains("slimeDensity")) params->slimeDensity = j["slimeDensity"];
        if (j.contains("slimeColor")) {
            params->slimeColor.x = j["slimeColor"][0];
            params->slimeColor.y = j["slimeColor"][1];
            params->slimeColor.z = j["slimeColor"][2];
        }
        if (j.contains("irisFadeIntensity")) params->irisFadeIntensity = j["irisFadeIntensity"];
        if (j.contains("irisCenter")) {
            params->irisCenterX = j["irisCenter"][0];
            params->irisCenterY = j["irisCenter"][1];
        }

    }
    catch (...) {
        // ロード失敗時のエラーハンドリング
    }
}