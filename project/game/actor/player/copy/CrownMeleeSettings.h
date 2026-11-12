#pragma once
#include "json.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

// 近接の調整値は発動時にコピーし、編集中も攻撃途中の時刻を飛ばさないようにします。
struct CrownMeleeSettings {
    std::array<float, 3> duration{0.43f, 0.46f, 0.68f};
    std::array<float, 3> reach{4.4f, 4.8f, 6.0f};
    std::array<float, 3> damage{14.0f, 17.0f, 28.0f};
    std::array<float, 3> hitStop{0.025f, 0.035f, 0.065f};
    float radius = 0.36f, stepDistance = 0.45f, queueStart = 0.08f;
    float trailWidth = 0.86f, trailLife = 0.22f;
    static constexpr const char* kConfigPath = "Resources/json/player/crown_melee.json";

    // EditorやJSONから入った調整値を、この設定で扱える有限値・範囲へ収める。
    void Sanitize() {
        auto clamp = [](float& v, float lo, float hi, float fallback) {
            v = std::clamp(std::isfinite(v) ? v : fallback, lo, hi);
        };
        const CrownMeleeSettings defaults;
        for (int i = 0; i < 3; ++i) {
            clamp(duration[i], 0.30f, 1.2f, defaults.duration[i]);
            clamp(reach[i], 2.0f, 6.0f, defaults.reach[i]);
            clamp(damage[i], 1.0f, 100.0f, defaults.damage[i]);
            clamp(hitStop[i], 0.0f, 0.09f, defaults.hitStop[i]);
        }
        clamp(radius, 0.12f, 0.65f, 0.36f);
        clamp(stepDistance, 0.0f, 0.9f, 0.45f);
        clamp(queueStart, 0.02f, 0.25f, 0.08f);
        clamp(trailWidth, 0.15f, 1.2f, 0.64f);
        clamp(trailLife, 0.06f, 0.25f, 0.14f);
    }
    // 設定JSONを読み込む。成功可否を返し、ゲーム中のActor生成や再生は開始しない。
    void Load() {
        try {
            std::ifstream in(kConfigPath);
            if (!in) return;
            nlohmann::json j; in >> j;
            CrownMeleeSettings next;
#define READ_MELEE(field) next.field = j.value(#field, next.field)
            READ_MELEE(duration); READ_MELEE(reach); READ_MELEE(damage); READ_MELEE(hitStop);
            READ_MELEE(radius); READ_MELEE(stepDistance); READ_MELEE(queueStart);
            READ_MELEE(trailWidth); READ_MELEE(trailLife);
#undef READ_MELEE
            next.Sanitize();
            *this = next;
        } catch (...) {
            // 壊れた設定を部分適用せず、有効な値を保ちます。
        }
    }
    // 現在の設定を専用JSONへ保存し、書き込みが成功したかを返す。
    bool Save() const {
        auto safe = *this; safe.Sanitize();
        nlohmann::json j;
#define WRITE_MELEE(field) j[#field] = safe.field
        WRITE_MELEE(duration); WRITE_MELEE(reach); WRITE_MELEE(damage); WRITE_MELEE(hitStop);
        WRITE_MELEE(radius); WRITE_MELEE(stepDistance); WRITE_MELEE(queueStart);
        WRITE_MELEE(trailWidth); WRITE_MELEE(trailLife);
#undef WRITE_MELEE
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), error);
        if (error) return false;
        std::ofstream out(kConfigPath);
        if (!out) return false;
        out << j.dump(2);
        return out.good();
    }
    static CrownMeleeSettings& Get() {
        static CrownMeleeSettings settings = [] { CrownMeleeSettings value; value.Load(); return value; }();
        return settings;
    }
#ifdef USE_IMGUI
    void DrawEditor() {
        if (!ImGui::CollapsingHeader("覇王・近接3段コンボの設定", ImGuiTreeNodeFlags_DefaultOpen)) return;
        const char* names[]{"1段目・横薙ぎ", "2段目・斬り上げ", "3段目・叩きつけ"};
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            ImGui::TextUnformatted(names[i]);
            ImGui::DragFloat("動作時間", &duration[i], 0.01f, 0.30f, 1.2f, "%.2f 秒");
            ImGui::DragFloat("槍の長さ", &reach[i], 0.05f, 2.0f, 6.0f);
            ImGui::DragFloat("威力", &damage[i], 1.0f, 1.0f, 100.0f);
            ImGui::DragFloat("命中停止時間", &hitStop[i], 0.005f, 0.0f, 0.09f, "%.3f 秒");
            ImGui::PopID();
        }
        ImGui::DragFloat("判定の太さ", &radius, 0.01f, 0.12f, 0.65f);
        ImGui::DragFloat("踏み込み距離", &stepDistance, 0.01f, 0.0f, 0.9f);
        ImGui::DragFloat("追加入力の受付開始", &queueStart, 0.01f, 0.02f, 0.25f, "%.2f 秒");
        ImGui::DragFloat("軌跡の幅", &trailWidth, 0.01f, 0.15f, 1.2f);
        ImGui::DragFloat("軌跡の残る時間", &trailLife, 0.01f, 0.06f, 0.25f, "%.2f 秒");
        Sanitize();
        static int result = 0;
        if (ImGui::Button("近接コンボの設定を保存")) result = Save() ? 1 : -1;
        if (result) ImGui::TextUnformatted(result > 0 ? "保存しました" : "保存に失敗しました");
    }
#endif
};
