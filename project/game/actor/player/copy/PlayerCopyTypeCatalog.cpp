#include "game/actor/player/copy/PlayerCopyTypeCatalog.h"

namespace {
const std::array<PlayerCopyTypeDescriptor, 5> kCopyTypes = {{
    { "Slime", "Characters/slime_pink", { 1.00f, 0.62f, 0.90f, 1.0f } },
    { "FireSlime", "Characters/slime_red", { 1.00f, 0.42f, 0.28f, 1.0f } },
    { "ThunderSlime", "Characters/slime_yellow", { 1.00f, 0.92f, 0.30f, 1.0f } },
    { "WindSlime", "Characters/slime_wind", { 0.48f, 1.00f, 0.82f, 1.0f } },
    { "Bomber", "Characters/slime_black", { 0.52f, 0.43f, 0.62f, 1.0f } },
}};
}

const std::array<PlayerCopyTypeDescriptor, 5>& PlayerCopyTypeCatalog::GetAll()
{
    return kCopyTypes;
}

const PlayerCopyTypeDescriptor* PlayerCopyTypeCatalog::Find(std::string_view enemyType)
{
    // 最終戦専用。通常の記憶台の候補数や並びは変更しません。
    static const PlayerCopyTypeDescriptor crown{ "Crown", "Characters/slime_crown", { 0.58f, 0.89f, 1.0f, 1.0f } };
    if (enemyType == "Crown") return &crown;
    for (const PlayerCopyTypeDescriptor& descriptor : kCopyTypes) {
        if (descriptor.enemyType == enemyType) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool PlayerCopyTypeCatalog::IsSupported(std::string_view enemyType)
{
    return Find(enemyType) != nullptr;
}

std::string_view PlayerCopyTypeCatalog::GetEssenceModel(std::string_view enemyType)
{
    if (enemyType == "FireSlime") return "Gimmicks/copy_essence_fire";
    if (enemyType == "ThunderSlime") return "Gimmicks/copy_essence_thunder";
    if (enemyType == "WindSlime") return "Gimmicks/copy_essence_wind";
    if (enemyType == "Bomber") return "Gimmicks/copy_essence_bomb";
    return "Gimmicks/copy_essence_pink";
}
