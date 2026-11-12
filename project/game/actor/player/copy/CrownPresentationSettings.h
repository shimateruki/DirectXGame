#pragma once
#include "CrownMeleeSettings.h"

// 既存の技設定を保持し、覇王コピー全体の見栄えだけを独立して調整します。
struct CrownPresentationSettings {
    float lanceScale = 1.35f;
    float lanceGateScale = 1.45f;
    float warpGateScale = 1.25f;
    float meleeGateScale = 1.5f;
    float meleeThickness = 1.18f;
    float particleScale = 1.2f;
    float trailScale = 1.35f;
    float ringScale = 1.0f;
    static constexpr const char* kConfigPath = "Resources/json/player/crown_presentation.json";
    // EditorやJSONから入った調整値を、この設定で扱える有限値・範囲へ収める。
    void Sanitize() {
        lanceScale = std::clamp(std::isfinite(lanceScale) ? lanceScale : 1.35f, 0.8f, 1.8f);
        lanceGateScale = std::clamp(std::isfinite(lanceGateScale) ? lanceGateScale : 1.45f, 0.8f, 2.0f);
        warpGateScale = std::clamp(std::isfinite(warpGateScale) ? warpGateScale : 1.25f, 0.8f, 1.65f);
        meleeGateScale = std::clamp(std::isfinite(meleeGateScale) ? meleeGateScale : 1.5f, 0.8f, 2.2f);
        meleeThickness = std::clamp(std::isfinite(meleeThickness) ? meleeThickness : 1.18f, 0.8f, 1.6f);
        particleScale = std::clamp(std::isfinite(particleScale) ? particleScale : 1.2f, 0.5f, 1.8f);
        trailScale = std::clamp(std::isfinite(trailScale) ? trailScale : 1.35f, 0.6f, 1.8f);
        ringScale = std::clamp(std::isfinite(ringScale) ? ringScale : 1.0f, 0.5f, 1.5f);
    }
    // 設定JSONを読み込む。成功可否を返し、ゲーム中のActor生成や再生は開始しない。
    void Load() {
        try {
            std::ifstream in(kConfigPath); if (!in) return;
            nlohmann::json j; in >> j;
            CrownPresentationSettings next;
            next.lanceScale = j.value("lanceScale", next.lanceScale);
            next.lanceGateScale = j.value("lanceGateScale", next.lanceGateScale);
            next.warpGateScale = j.value("warpGateScale", next.warpGateScale);
            next.meleeGateScale = j.value("meleeGateScale", next.meleeGateScale);
            next.meleeThickness = j.value("meleeThickness", next.meleeThickness);
            next.particleScale = j.value("particleScale", next.particleScale);
            next.trailScale = j.value("trailScale", next.trailScale);
            next.ringScale = j.value("ringScale", next.ringScale);
            next.Sanitize(); *this = next;
        } catch (...) { /* 設定の部分適用を避けます。 */ }
    }
    // 現在の設定を専用JSONへ保存し、書き込みが成功したかを返す。
    bool Save() const {
        auto safe = *this; safe.Sanitize(); nlohmann::json j;
        j["lanceScale"] = safe.lanceScale;
        j["lanceGateScale"] = safe.lanceGateScale;
        j["warpGateScale"] = safe.warpGateScale;
        j["meleeGateScale"] = safe.meleeGateScale;
        j["meleeThickness"] = safe.meleeThickness;
        j["particleScale"] = safe.particleScale;
        j["trailScale"] = safe.trailScale;
        j["ringScale"] = safe.ringScale;
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), error);
        if (error) return false;
        std::ofstream out(kConfigPath); if (!out) return false; out << j.dump(2); return out.good();
    }
    static CrownPresentationSettings& Get() {
        static CrownPresentationSettings value = [] { CrownPresentationSettings s; s.Load(); return s; }(); return value;
    }
#ifdef USE_IMGUI
    void DrawEditor() {
        if (!ImGui::CollapsingHeader("覇王・全技の迫力設定", ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::DragFloat("通常・溜め槍の大きさ", &lanceScale, 0.01f, 0.8f, 1.8f);
        ImGui::DragFloat("槍ゲートの倍率", &lanceGateScale, 0.01f, 0.8f, 2.0f);
        ImGui::DragFloat("回避ゲートの倍率", &warpGateScale, 0.01f, 0.8f, 1.65f);
        ImGui::DragFloat("近接ゲートの倍率", &meleeGateScale, 0.01f, 0.8f, 2.2f);
        ImGui::DragFloat("近接槍の太さ", &meleeThickness, 0.01f, 0.8f, 1.6f);
        ImGui::DragFloat("命中粒子の大きさ", &particleScale, 0.01f, 0.5f, 1.8f);
        ImGui::DragFloat("全技の残光の幅", &trailScale, 0.01f, 0.6f, 1.8f);
        ImGui::DragFloat("衝撃波の大きさ", &ringScale, 0.01f, 0.5f, 1.5f);
        Sanitize(); static int result = 0;
        if (ImGui::Button("覇王の迫力設定を保存")) result = Save() ? 1 : -1;
        if (result) ImGui::TextUnformatted(result > 0 ? "保存しました" : "保存に失敗しました");
        ImGui::TextWrapped("次の発動から反映。粒子の細かな形と寿命は player_crown_melee_ / player_crown_portal_ のプリセットで調整できます。");
    }
#endif
};
