#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

// 風コピーの風跳び・小型竜巻の調整値。能力は発動時に複製し、Editor変更で実行中の技を跳ねさせない。
// 主な入口はGet、DrawEditor。敵の風攻撃と左クリック竜巻の設定は変更しない。
struct WindCopySettings {
    float liftJumpScale = 1.15f; // 通常ジャンプ初速に対する鉛直初速の倍率。
    float glideDuration = 1.2f; // 頂点から緩降下を続ける秒数。
    float liftCooldown = 0.65f; // 風跳び終了から再使用までの秒数。
    float glideFallSpeed = 2.0f; // 緩降下中の落下速度上限。
    float invincibleTime = 0.12f; // 風跳び開始の無敵時間（秒）。
    float vortexSpeed = 12.0f; // 小型竜巻の速度。
    float vortexLifetime = 2.3f; // 飛行時間（秒）。旧設定との互換のためJSONキーを維持する。
    float vortexHoldDuration = 0.35f; // 射程端に留まる時間（秒）。0ならそのまま消散する。
    float vortexHoldPullScale = 1.5f; // 滞留時の引き寄せ速度倍率。
    float vortexRadius = 1.8f; // 小型竜巻の判定半径。
    float vortexDamage = 3.0f; // 小型竜巻の一回の威力。
    float vortexPull = 3.0f; // 小型竜巻の引き寄せ速度。
    float vortexHitInterval = 0.3f; // 小型竜巻の命中間隔（秒）。
    float vortexCooldown = 2.2f; // 小型竜巻の再使用待ち（秒）。
    float vortexVisualHeight = 3.4f; // 風帯の高さの基準（ワールド単位）。
    float vortexVisualWidth = 1.0f; // 判定半径に対する表示半径の倍率。
    float vortexSpinRate = 14.0f; // 外側の回転速度（ラジアン毎秒）。
    static constexpr const char* kConfigPath = "Resources/json/player/wind_copy.json";
    // 非有限値と極端な値を補正し、衝突照会や発生間隔が発散しないようにする。
    void Sanitize() {
        auto clamp = [](float& v, float lo, float hi, float fallback) {
            v = std::clamp(std::isfinite(v) ? v : fallback, lo, hi);
        };
        clamp(liftJumpScale, 0.8f, 1.45f, 1.15f);
        clamp(glideDuration, 0.2f, 3.0f, 1.2f);
        clamp(liftCooldown, 0.2f, 5.0f, 0.65f);
        clamp(glideFallSpeed, 0.5f, 10.0f, 2.0f);
        clamp(invincibleTime, 0.0f, 0.2f, 0.12f);
        clamp(vortexSpeed, 1.0f, 15.0f, 12.0f);
        clamp(vortexLifetime, 0.3f, 3.0f, 2.3f);
        clamp(vortexHoldDuration, 0.0f, 0.75f, 0.35f);
        clamp(vortexHoldPullScale, 1.0f, 2.0f, 1.5f);
        clamp(vortexRadius, 0.6f, 3.0f, 1.8f);
        clamp(vortexDamage, 0.0f, 30.0f, 3.0f);
        clamp(vortexPull, 0.0f, 8.0f, 3.0f);
        clamp(vortexHitInterval, 0.15f, 1.0f, 0.3f);
        clamp(vortexVisualHeight, 1.0f, 5.0f, 3.4f);
        clamp(vortexVisualWidth, 0.55f, 1.15f, 1.0f);
        clamp(vortexSpinRate, 4.0f, 28.0f, 14.0f);
        clamp(vortexCooldown, 0.5f, 8.0f, 2.2f);
    }
    // 読込失敗時は現在値を保持する。未保存の旧環境では既定値を使う。
    void Load() {
        try {
            std::ifstream in(kConfigPath);
            if (!in) return;
            nlohmann::json j; in >> j;
            WindCopySettings next;
            next.liftJumpScale = j.value("liftJumpScale", next.liftJumpScale);
            next.glideDuration = j.value("glideDuration", next.glideDuration);
            next.liftCooldown = j.value("liftCooldown", next.liftCooldown);
            if (!j.contains("liftCooldown")) next.liftCooldown = j.value("dashCooldown", next.liftCooldown);
            next.glideFallSpeed = j.value("glideFallSpeed", next.glideFallSpeed);
            next.invincibleTime = j.value("invincibleTime", next.invincibleTime);
            next.vortexSpeed = j.value("vortexSpeed", next.vortexSpeed);
            next.vortexLifetime = j.value("vortexLifetime", next.vortexLifetime);
            next.vortexHoldDuration = j.value("vortexHoldDuration", next.vortexHoldDuration);
            next.vortexHoldPullScale = j.value("vortexHoldPullScale", next.vortexHoldPullScale);
            next.vortexRadius = j.value("vortexRadius", next.vortexRadius);
            next.vortexDamage = j.value("vortexDamage", next.vortexDamage);
            next.vortexPull = j.value("vortexPull", next.vortexPull);
            next.vortexHitInterval = j.value("vortexHitInterval", next.vortexHitInterval);
            next.vortexCooldown = j.value("vortexCooldown", next.vortexCooldown);
            next.vortexVisualHeight = j.value("vortexVisualHeight", next.vortexVisualHeight);
            next.vortexVisualWidth = j.value("vortexVisualWidth", next.vortexVisualWidth);
            next.vortexSpinRate = j.value("vortexSpinRate", next.vortexSpinRate);
            next.Sanitize(); *this = next;
        } catch (...) {}
    }
    bool Save() const {
        auto safe = *this; safe.Sanitize();
        nlohmann::json j;
        j["liftJumpScale"] = safe.liftJumpScale;
        j["glideDuration"] = safe.glideDuration;
        j["liftCooldown"] = safe.liftCooldown;
        j["glideFallSpeed"] = safe.glideFallSpeed;
        j["invincibleTime"] = safe.invincibleTime;
        j["vortexSpeed"] = safe.vortexSpeed;
        j["vortexLifetime"] = safe.vortexLifetime;
        j["vortexHoldDuration"] = safe.vortexHoldDuration;
        j["vortexHoldPullScale"] = safe.vortexHoldPullScale;
        j["vortexRadius"] = safe.vortexRadius;
        j["vortexDamage"] = safe.vortexDamage;
        j["vortexPull"] = safe.vortexPull;
        j["vortexHitInterval"] = safe.vortexHitInterval;
        j["vortexCooldown"] = safe.vortexCooldown;
        j["vortexVisualHeight"] = safe.vortexVisualHeight;
        j["vortexVisualWidth"] = safe.vortexVisualWidth;
        j["vortexSpinRate"] = safe.vortexSpinRate;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), ec);
        if (ec) return false;
        std::ofstream out(kConfigPath); if (!out) return false;
        out << j.dump(2); return out.good();
    }
    static WindCopySettings& Get() {
        static WindCopySettings settings = [] { WindCopySettings v; v.Load(); return v; }();
        return settings;
    }
#ifdef USE_IMGUI
    void DrawEditor() {
        if (!ImGui::CollapsingHeader("風コピー・風跳びと小型竜巻", ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::TextWrapped("右クリックで地上から真上へ跳び、頂点から緩降下。左クリックで竜巻へ接続。Eで前へ進む小型竜巻。");
        ImGui::DragFloat("風跳びの初速倍率", &liftJumpScale, 0.01f, 0.8f, 1.45f);
        ImGui::DragFloat("頂点からの緩降下時間（秒）", &glideDuration, 0.01f, 0.2f, 3.0f);
        ImGui::DragFloat("風跳びの再使用待ち（秒）", &liftCooldown, 0.01f, 0.2f, 5.0f);
        ImGui::DragFloat("緩降下中の落下速度上限", &glideFallSpeed, 0.01f, 0.5f, 10.0f);
        ImGui::DragFloat("風跳び開始の無敵時間（秒）", &invincibleTime, 0.01f, 0.0f, 0.2f);
        ImGui::DragFloat("小型竜巻の速度", &vortexSpeed, 0.01f, 1.0f, 15.0f);
        ImGui::DragFloat("小型竜巻の飛行時間（秒）", &vortexLifetime, 0.01f, 0.3f, 3.0f);
        ImGui::DragFloat("射程端の滞留時間（秒）", &vortexHoldDuration, 0.01f, 0.0f, 0.75f);
        ImGui::DragFloat("滞留時の引き寄せ倍率", &vortexHoldPullScale, 0.02f, 1.0f, 2.0f);
        ImGui::DragFloat("小型竜巻の判定半径", &vortexRadius, 0.01f, 0.6f, 3.0f);
        ImGui::DragFloat("小型竜巻の一回の威力", &vortexDamage, 0.01f, 0.0f, 30.0f);
        ImGui::DragFloat("小型竜巻の引き寄せ速度", &vortexPull, 0.01f, 0.0f, 8.0f);
        ImGui::DragFloat("小型竜巻の命中間隔（秒）", &vortexHitInterval, 0.01f, 0.15f, 1.0f);
        ImGui::DragFloat("小型竜巻の再使用待ち（秒）", &vortexCooldown, 0.01f, 0.5f, 8.0f);
        ImGui::DragFloat("小型竜巻の表示高さ", &vortexVisualHeight, 0.02f, 1.0f, 5.0f);
        ImGui::DragFloat("小型竜巻の表示幅倍率", &vortexVisualWidth, 0.02f, 0.55f, 1.15f);
        ImGui::DragFloat("小型竜巻の回転速度（rad/秒）", &vortexSpinRate, 0.02f, 4.0f, 28.0f);
        Sanitize();
        ImGui::Text("竜巻中心の最大移動距離 %.1f", vortexSpeed * vortexLifetime);
        static int result = 0;
        if (ImGui::Button("風コピー設定を保存")) result = Save() ? 1 : -1;
        if (result) ImGui::TextUnformatted(result > 0 ? "保存しました" : "保存に失敗しました");
    }
#endif
};
