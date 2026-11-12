#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <array>

// 視界確保の共通設定。材質の保存色や衝突設定は変更しない。
// 関連：Camera / CameraManager（視界確保）、MeshRenderer（描画反映）
struct CameraVisibilitySettings {
    bool enabled = true; // 視界確保を有効にするか。
    std::array<float, 4> silhouetteColor{0.15f, 0.85f, 1.0f, 0.38f};
    float radius = 1.8f;          // プレイヤー位置での透過円半径（ワールド単位）。
    float minimumOpacity = 0.08f; // 遮蔽物に残す最小の不透明度。
    float feather = 0.4f;        // 半径に対する境界のぼかし幅。
    float fadeIn = 0.18f; // 透過へ移る応答時間（秒）。
    float fadeOut = 0.35f; // 不透明へ戻る応答時間（秒）。
    float collisionRadius = 0.35f; // カメラと壁の判定に使う半径。
    float recoveryTime = 0.65f;   // 壁から離れたときの距離復帰時定数。
    float manualReturnDelay = 2.0f; // 手動操作後に誘導へ戻るまでの待機秒数。

    // 非有限値を既定値へ戻し、各設定を有効範囲へ制限する。
    void Validate() {
        auto clamp = [](float v, float fallback, float lo, float hi) {
            return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
        };
        for (auto& component : silhouetteColor) component = clamp(component, 0.0f, 0.0f, 1.0f);
        radius = clamp(radius, 1.8f, 0.3f, 8.0f);
        minimumOpacity = clamp(minimumOpacity, 0.08f, 0.0f, 0.8f);
        feather = clamp(feather, 0.4f, 0.05f, 0.9f);
        fadeIn = clamp(fadeIn, 0.18f, 0.02f, 2.0f);
        fadeOut = clamp(fadeOut, 0.35f, 0.02f, 3.0f);
        collisionRadius = clamp(collisionRadius, 0.35f, 0.1f, 1.0f);
        recoveryTime = clamp(recoveryTime, 0.65f, 0.05f, 3.0f);
        manualReturnDelay = clamp(manualReturnDelay, 2.0f, 0.0f, 10.0f);
    }
    // 視界確保の編集設定をJSONへ変換する。
    nlohmann::json ToJson() const {
        return {{"enabled", enabled}, {"silhouetteColor", silhouetteColor}, {"radius", radius}, {"minimumOpacity", minimumOpacity},
            {"feather", feather}, {"fadeIn", fadeIn}, {"fadeOut", fadeOut},
            {"collisionRadius", collisionRadius}, {"recoveryTime", recoveryTime},
            {"manualReturnDelay", manualReturnDelay}};
    }
    // 既定値から有効なJSON項目を読み、範囲を検証する。
    void FromJson(const nlohmann::json& j) {
        *this = {};
        if (!j.is_object()) return;
        if (j.contains("enabled") && j["enabled"].is_boolean()) enabled = j["enabled"].get<bool>();
        if (j.contains("silhouetteColor") && j["silhouetteColor"].is_array() && j["silhouetteColor"].size() == 4) {
            bool numeric = true;
            for (const auto& component : j["silhouetteColor"]) numeric = numeric && component.is_number();
            if (numeric) silhouetteColor = j["silhouetteColor"].get<std::array<float, 4>>();
        }
        auto read = [&](const char* key, float& v) {
            if (j.contains(key) && j[key].is_number()) v = j[key].get<float>();
        };
        read("radius", radius); read("minimumOpacity", minimumOpacity); read("feather", feather);
        read("fadeIn", fadeIn); read("fadeOut", fadeOut); read("collisionRadius", collisionRadius);
        read("recoveryTime", recoveryTime); read("manualReturnDelay", manualReturnDelay);
        Validate();
    }
};
