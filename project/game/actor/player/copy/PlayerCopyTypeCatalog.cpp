#include "game/actor/player/copy/PlayerCopyTypeCatalog.h"

namespace {
const std::array<PlayerCopyTypeDescriptor, 5> kCopyTypes = {{
    {"Slime", "Characters/slime_pink", {1.00f, 0.62f, 0.90f, 1.0f}},
    {"FireSlime", "Characters/slime_red", {1.00f, 0.42f, 0.28f, 1.0f}},
    {"ThunderSlime", "Characters/slime_yellow", {1.00f, 0.92f, 0.30f, 1.0f}},
    {"WindSlime", "Characters/slime_wind", {0.48f, 1.00f, 0.82f, 1.0f}},
    {"Bomber", "Characters/slime_black", {0.52f, 0.43f, 0.62f, 1.0f}},
}};
}

const std::array<PlayerCopyTypeDescriptor, 5>& PlayerCopyTypeCatalog::GetAll() {
    // 編集画面とゲーム側で同じ定義を共有し、呼び出しごとのコピーを避ける。
    return kCopyTypes;
}

const PlayerCopyTypeDescriptor* PlayerCopyTypeCatalog::Find(std::string_view enemyType) {
    // 最終戦専用。通常の記憶台の候補数や並びは変更しません。
    static const PlayerCopyTypeDescriptor crown{"Crown", "Characters/slime_crown", {1.0f, 0.77f, 0.25f, 1.0f}};
    if (enemyType == "Crown")
        return &crown;
    for (const PlayerCopyTypeDescriptor& descriptor : kCopyTypes) {
        if (descriptor.enemyType == enemyType) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool PlayerCopyTypeCatalog::IsSupported(std::string_view enemyType) {
    // 登録表に存在する敵型だけをコピー対象として扱う。
    return Find(enemyType) != nullptr;
}

std::string_view PlayerCopyTypeCatalog::GetEssenceModel(std::string_view enemyType) {
    // 敵の種類をコピーエッセンスの外観へ対応付ける。
    if (enemyType == "FireSlime")
        return "Gimmicks/copy_essence_fire";
    if (enemyType == "ThunderSlime")
        return "Gimmicks/copy_essence_thunder";
    if (enemyType == "WindSlime")
        return "Gimmicks/copy_essence_wind";
    if (enemyType == "Bomber")
        return "Gimmicks/copy_essence_bomb";
    return "Gimmicks/copy_essence_pink";
}
