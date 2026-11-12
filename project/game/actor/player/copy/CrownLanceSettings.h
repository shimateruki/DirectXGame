#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <array>
#include <fstream>
#include <filesystem>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

// プレイヤー専用の王冠槍。ボスの演出・威力設定とは独立して保存します。
struct CrownLanceSettings {
    float threeGateTime = 0.4f, fiveGateTime = 0.9f, fullChargeTime = 1.2f;
    float gateSpacing = 1.35f, gateHeight = 2.8f, gateScale = 0.72f;
    float openDuration = 0.10f, closeDuration = 0.20f, shotInterval = 0.035f;
    float fullCenterScale = 1.3f, shotSpeed = 36.0f, cooldown = 0.55f;
    // 発射時に槍ごとに保存し、次の攻撃を調整しても消散途中の値を変えません。
    struct Appearance {
        float dissolveTime = 0.24f;
        float edgeWidth = 0.095f;
        std::array<float, 3> edgeColor{1.0f, 0.76f, 0.30f};
        float trailLength = 3.8f;
        float trailWidth = 0.32f;
        float trailNoise = 0.48f;
    } appearance;
    float warpDistance = 5.0f, warpInvincibility = 0.26f, warpCooldown = 1.15f;
    float warpGateSize = 1.34f, warpMembraneOpacity = 0.045f;
    static constexpr const char* kConfigPath = "Resources/json/player/crown_lance.json";

    // EditorやJSONから入った調整値を、この設定で扱える有限値・範囲へ収める。
    void Sanitize() {
        auto clamp = [](float& v, float lo, float hi, float fallback) {
            v = std::clamp(std::isfinite(v) ? v : fallback, lo, hi);
        };
        clamp(warpGateSize, 0.9f, 1.9f, 1.34f);
        clamp(warpMembraneOpacity, 0.0f, 0.45f, 0.045f);
        clamp(warpDistance, 2.0f, 10.0f, 5.0f);
        clamp(warpInvincibility, 0.14f, 0.45f, 0.26f);
        clamp(warpCooldown, 0.45f, 3.0f, 1.15f);
        clamp(threeGateTime, 0.15f, 0.8f, 0.4f);
        clamp(fiveGateTime, threeGateTime + 0.1f, 1.5f, 0.9f);
        clamp(fullChargeTime, fiveGateTime + 0.1f, 2.5f, 1.2f);
        clamp(gateSpacing, 0.8f, 2.0f, 1.35f);
        clamp(gateHeight, 2.0f, 4.2f, 2.8f);
        clamp(gateScale, 0.45f, 1.15f, 0.72f);
        clamp(openDuration, 0.04f, 0.2f, 0.1f);
        clamp(closeDuration, 0.08f, 0.45f, 0.2f);
        clamp(shotInterval, 0.015f, 0.08f, 0.035f);
        clamp(fullCenterScale, 1.0f, 1.6f, 1.3f);
        clamp(shotSpeed, 18.0f, 60.0f, 36.0f);
        clamp(cooldown, 0.2f, 1.5f, 0.55f);
        clamp(appearance.dissolveTime, 0.12f, 0.45f, 0.24f);
        clamp(appearance.edgeWidth, 0.045f, 0.22f, 0.095f);
        clamp(appearance.trailLength, 0.6f, 7.0f, 3.8f);
        clamp(appearance.trailWidth, 0.08f, 0.7f, 0.32f);
        clamp(appearance.trailNoise, 0.0f, 1.0f, 0.48f);
        for (size_t i = 0; i < appearance.edgeColor.size(); ++i)
            clamp(appearance.edgeColor[i], 0.0f, 1.0f, Appearance{}.edgeColor[i]);
    }
    int GateCount(float seconds) const { return seconds >= fiveGateTime ? 5 : seconds >= threeGateTime ? 3 : 1; }
    // 設定JSONを読み込む。成功可否を返し、ゲーム中のActor生成や再生は開始しない。
    void Load() {
        try {
            std::ifstream in(kConfigPath);
            if (!in)
                return;
            nlohmann::json j;
            in >> j;
#define CROWN_READ(name) name = j.value(#name, name)
            CROWN_READ(threeGateTime);
            CROWN_READ(warpDistance);
            CROWN_READ(warpInvincibility);
            CROWN_READ(warpCooldown);
            CROWN_READ(warpGateSize);
            CROWN_READ(warpMembraneOpacity);
            CROWN_READ(fiveGateTime);
            CROWN_READ(fullChargeTime);
            CROWN_READ(gateSpacing);
            CROWN_READ(gateHeight);
            CROWN_READ(gateScale);
            CROWN_READ(openDuration);
            CROWN_READ(closeDuration);
            CROWN_READ(shotInterval);
            CROWN_READ(fullCenterScale);
            CROWN_READ(shotSpeed);
            CROWN_READ(cooldown);
#undef CROWN_READ
            appearance.dissolveTime = j.value("dissolveTime", appearance.dissolveTime);
            appearance.edgeWidth = j.value("edgeWidth", appearance.edgeWidth);
            appearance.edgeColor = j.value("edgeColor", appearance.edgeColor);
            appearance.trailLength = j.value("trailLength", appearance.trailLength);
            appearance.trailWidth = j.value("trailWidth", appearance.trailWidth);
            appearance.trailNoise = j.value("trailNoise", appearance.trailNoise);
        } catch (...) {
        } // 読み込み失敗時も有効な既定値に収めます。
        Sanitize();
    }
    // 現在の設定を専用JSONへ保存し、書き込みが成功したかを返す。
    bool Save() const {
        nlohmann::json j;
#define CROWN_WRITE(name) j[#name] = name
        CROWN_WRITE(threeGateTime);
        CROWN_WRITE(warpDistance);
        CROWN_WRITE(warpInvincibility);
        CROWN_WRITE(warpCooldown);
        CROWN_WRITE(warpGateSize);
        CROWN_WRITE(warpMembraneOpacity);
        CROWN_WRITE(fiveGateTime);
        CROWN_WRITE(fullChargeTime);
        CROWN_WRITE(gateSpacing);
        CROWN_WRITE(gateHeight);
        CROWN_WRITE(gateScale);
        CROWN_WRITE(openDuration);
        CROWN_WRITE(closeDuration);
        CROWN_WRITE(shotInterval);
        CROWN_WRITE(fullCenterScale);
        CROWN_WRITE(shotSpeed);
        CROWN_WRITE(cooldown);
#undef CROWN_WRITE
        j["dissolveTime"] = appearance.dissolveTime;
        j["edgeWidth"] = appearance.edgeWidth;
        j["edgeColor"] = appearance.edgeColor;
        j["trailLength"] = appearance.trailLength;
        j["trailWidth"] = appearance.trailWidth;
        j["trailNoise"] = appearance.trailNoise;
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), error);
        if (error)
            return false;
        std::ofstream out(kConfigPath);
        if (!out)
            return false;
        out << j.dump(2);
        return out.good();
    }
    static CrownLanceSettings& Get() {
        static CrownLanceSettings value = [] {
            CrownLanceSettings s;
            s.Load();
            return s;
        }();
        return value;
    }
#ifdef USE_IMGUI
    void DrawEditor() {
        if (ImGui::CollapsingHeader("覇王・ゲート回避の設定", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("回避ゲートの大きさ", &warpGateSize, 0.01f, 0.9f, 1.9f);
            ImGui::SliderFloat("回避ゲート中央の濃さ", &warpMembraneOpacity, 0.0f, 0.45f, "%.3f");
            ImGui::DragFloat("ワープ距離", &warpDistance, 0.05f, 2.0f, 10.0f);
            ImGui::DragFloat("回避無敵時間", &warpInvincibility, 0.01f, 0.14f, 0.45f, "%.2f 秒");
            ImGui::DragFloat("回避の再使用時間", &warpCooldown, 0.01f, 0.45f, 3.0f, "%.2f 秒");
            Sanitize();
            static int result = 0;
            if (ImGui::Button("ゲート回避の設定を保存"))
                result = Save() ? 1 : -1;
            if (result) ImGui::TextUnformatted(result > 0 ? "保存しました" : "保存に失敗しました");
        }
        if (!ImGui::CollapsingHeader("覇王・ゲート槍の設定", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        ImGui::DragFloat("3ゲートまで", &threeGateTime, 0.01f, 0.15f, 0.8f, "%.2f 秒");
        ImGui::DragFloat("5ゲートまで", &fiveGateTime, 0.01f, threeGateTime + 0.1f, 1.5f, "%.2f 秒");
        ImGui::DragFloat("最大溜めまで", &fullChargeTime, 0.01f, fiveGateTime + 0.1f, 2.5f, "%.2f 秒");
        ImGui::DragFloat("ゲートの横間隔", &gateSpacing, 0.02f, 0.8f, 2.0f);
        ImGui::DragFloat("中央ゲートの高さ", &gateHeight, 0.02f, 2.0f, 4.2f);
        ImGui::DragFloat("ゲートの大きさ", &gateScale, 0.01f, 0.45f, 1.15f);
        ImGui::DragFloat("ゲートが開く時間", &openDuration, 0.005f, 0.04f, 0.2f, "%.3f 秒");
        ImGui::DragFloat("ゲートが閉じる時間", &closeDuration, 0.01f, 0.08f, 0.45f, "%.2f 秒");
        ImGui::DragFloat("槍の発射間隔", &shotInterval, 0.005f, 0.015f, 0.08f, "%.3f 秒");
        ImGui::DragFloat("最大溜めの中央槍倍率", &fullCenterScale, 0.01f, 1.0f, 1.6f);
        ImGui::DragFloat("槍の速度", &shotSpeed, 0.5f, 18.0f, 60.0f);
        ImGui::DragFloat("再使用時間", &cooldown, 0.01f, 0.2f, 1.5f, "%.2f 秒");
        ImGui::DragFloat("槍が崩れる時間", &appearance.dissolveTime, 0.005f, 0.12f, 0.45f, "%.3f 秒");
        ImGui::DragFloat("ディゾルブの縁幅", &appearance.edgeWidth, 0.002f, 0.045f, 0.22f);
        ImGui::ColorEdit3("縁の色（白金の下地）", appearance.edgeColor.data());
        ImGui::DragFloat("光の帯の長さ", &appearance.trailLength, 0.05f, 0.6f, 7.0f);
        ImGui::DragFloat("光の帯の幅", &appearance.trailWidth, 0.005f, 0.08f, 0.7f);
        ImGui::SliderFloat("光の帯の揺らぎ", &appearance.trailNoise, 0.0f, 1.0f);
        ImGui::TextWrapped("粒子の色・数・大きさ・寿命はGPU Particle Editorのplayer_crown_で始まるプリセットから調整できます。");
        Sanitize();
        static int saved = 0;
        if (ImGui::Button("ゲート槍の設定を保存"))
            saved = Save() ? 1 : -1;
        if (saved)
            ImGui::TextUnformatted(saved > 0 ? "保存しました" : "保存に失敗しました");
        ImGui::TextWrapped("設定は次の溜め開始時に反映。構えと発射反動はAnimation "
                           "Workbenchのplayer_ability_crown_charge / releaseで編集できます。");
    }
#endif
};
