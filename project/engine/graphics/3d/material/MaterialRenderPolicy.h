#pragma once

#include <cstdint>

// JSON・Replayに保存済みの材質番号から、描画する区間を一箇所で判定する。
// 新しい番号を追加するときは既存番号の意味を変更しない。
namespace MaterialRenderPolicy {
enum class Pass : std::uint8_t {
    Opaque,
    Transparent,
    LocalFog,
    Special,
};

constexpr Pass GetPass(std::int32_t materialType) noexcept {
    // 透明・局所霧・特殊材質を先に振り分け、未登録値は通常の不透明描画へ戻す。
    if (materialType == 1) {
        return Pass::Transparent;
    }
    if (materialType == 7) {
        return Pass::LocalFog;
    }
    if ((materialType >= 8 && materialType <= 22) || materialType == 26) {
        return Pass::Special;
    }
    return Pass::Opaque;
}

constexpr bool IsSpecial(std::int32_t materialType) noexcept {
    return GetPass(materialType) == Pass::Special;
}
} // namespace MaterialRenderPolicy
