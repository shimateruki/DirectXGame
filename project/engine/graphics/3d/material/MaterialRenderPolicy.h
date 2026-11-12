#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// 保存済みの整数は互換用の固定値。新しい通常材質は文字列IDを使用する。
namespace MaterialRenderPolicy {
enum class Pass : std::uint8_t { Opaque, Transparent, LocalFog, Special };
enum class SpecialKind : std::uint8_t {
    None, Water, Magma, Ice, Fire, Laser, SlimeGel, Shockwave,
    LiquidContact, DamageCrack, Updraft, StunBind, CrownUnlock,
    PoisonSpore, Cloud, GatePortal, WindOrb,
};
// 追加で必要な描画入力のビット集合。画面色・深度・エフェクト定数を個別に表す。
enum Input : std::uint8_t { NoExtraInput = 0, SceneColor = 1, SceneDepth = 2, EffectParameters = 4 };
// 保存済み材質番号とID・描画パス・必要入力の対応。
// 関連：BaseScene / MeshRenderer / Object3DCommon（描画分類）
struct LegacyMaterial {
    std::int32_t type;
    std::string_view id;
    const char* displayName;
    Pass pass;
    SpecialKind special;
    std::uint8_t inputs;
};
// ID と番号はシーンJSON・Replayとの互換性のため変更しない。
inline constexpr std::array<LegacyMaterial, 32> kLegacyMaterials = {{
    {0, "standard", "通常 (Standard)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {1, "glass", "ガラス (Glass)", Pass::Transparent, SpecialKind::None, NoExtraInput},
    {2, "crystal", "氷・宝石 (Ice/Crystal)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {3, "hologram", "ホログラム (Hologram)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {4, "dissolve", "消滅 (Dissolve)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {5, "legacy_magma", "旧マグマ (Emissive)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {6, "cel_shaded", "トゥーン調 (Cel Shaded)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {7, "local_fog", "ローカルフォグ (Local Fog)", Pass::LocalFog, SpecialKind::None, NoExtraInput},
    {8, "water", "水 (Water)", Pass::Special, SpecialKind::Water, 7},
    {9, "magma", "新マグマ (Magma)", Pass::Special, SpecialKind::Magma, 7},
    {10, "thick_ice", "分厚い氷 (Ice)", Pass::Special, SpecialKind::Ice, 7},
    {11, "fire", "炎 (Fire)", Pass::Special, SpecialKind::Fire, 7},
    {12, "laser", "レーザー (Laser)", Pass::Special, SpecialKind::Laser, 7},
    {13, "slime_gel", "スライムジェル (Slime Gel)", Pass::Special, SpecialKind::SlimeGel, 7},
    {14, "shockwave", "地面衝撃波 (Shockwave)", Pass::Special, SpecialKind::Shockwave, 7},
    {15, "liquid_contact", "水/マグマ接触 (Liquid Contact)", Pass::Special, SpecialKind::LiquidContact, 7},
    {16, "damage_crack", "ダメージ亀裂 (Damage Crack)", Pass::Special, SpecialKind::DamageCrack, 7},
    {17, "updraft", "上昇気流 (Updraft)", Pass::Special, SpecialKind::Updraft, 7},
    {18, "stun_bind", "スタン拘束 (Stun Bind)", Pass::Special, SpecialKind::StunBind, 7},
    {19, "crown_unlock", "王冠解放 (Crown Unlock)", Pass::Special, SpecialKind::CrownUnlock, 7},
    {20, "poison_spore", "毒胞子 (Poison Spore)", Pass::Special, SpecialKind::PoisonSpore, 7},
    {21, "cloud", "雲 (Cloud)", Pass::Special, SpecialKind::Cloud, 7},
    {22, "gate_portal", "ゲートポータル (Gate Portal)", Pass::Special, SpecialKind::GatePortal, 7},
    {23, "stylized_terrain", "アニメ調地形 (Stylized Terrain)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {24, "legacy_dash_panel", "ダッシュパネル (Dash Panel)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {25, "slime_soft", "スライム補正 (Slime Soft)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {26, "wind_orb", "風弾 (Wind Orb)", Pass::Special, SpecialKind::WindOrb, 7},
    {27, "prism_crystal", "プリズム結晶 (Prism Crystal)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {28, "decal", "デカール (Decal)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {29, "meadow_turf", "芝生面 (Meadow Turf)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {30, "animated_grass", "揺れる草 (Animated Grass)", Pass::Opaque, SpecialKind::None, NoExtraInput},
    {31, "animated_foliage", "揺れる樹木 (Animated Foliage)", Pass::Opaque, SpecialKind::None, NoExtraInput},
}};
// 番号／IDが未登録なら nullptr。カタログは固定配列なので返す参照は再配置されない。
constexpr const LegacyMaterial* Find(std::int32_t type) noexcept {
    for (const auto& material : kLegacyMaterials)
        if (material.type == type) return &material;
    return nullptr;
}
// 保存IDまたは旧番号から定義を探す。未登録ならnullptr。
constexpr const LegacyMaterial* Find(std::string_view id) noexcept {
    for (const auto& material : kLegacyMaterials)
        if (material.id == id) return &material;
    return nullptr;
}
// 未知の旧番号は通常の不透明パスへ分類する。特殊種別が不明な場合は None。
constexpr Pass GetPass(std::int32_t type) noexcept {
    const auto* material = Find(type);
    return material ? material->pass : Pass::Opaque;
}
constexpr bool IsSpecial(std::int32_t type) noexcept { return GetPass(type) == Pass::Special; }
constexpr SpecialKind GetSpecialKind(std::int32_t type) noexcept {
    const auto* material = Find(type);
    return material ? material->special : SpecialKind::None;
}
// 番号の連続性・ID重複・特殊パスとの整合をコンパイル時に検査する。
constexpr bool ValidCatalog() noexcept {
    for (std::size_t i = 0; i < kLegacyMaterials.size(); ++i) {
        if (kLegacyMaterials[i].type != static_cast<std::int32_t>(i) || kLegacyMaterials[i].id.empty() ||
            (kLegacyMaterials[i].pass == Pass::Special) != (kLegacyMaterials[i].special != SpecialKind::None)) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (kLegacyMaterials[j].id == kLegacyMaterials[i].id) return false;
    }
    return true;
}
static_assert(ValidCatalog(), "Legacy material IDs must remain unique and stable");
} // namespace MaterialRenderPolicy
