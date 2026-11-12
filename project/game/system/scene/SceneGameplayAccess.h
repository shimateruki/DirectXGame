#pragma once

#include "BaseScene.h"
#include <cassert>

class Player;
class GameRule;

// ゲーム固有のPlayerとルールへの参照を、汎用BaseSceneから分離します。
// 関連：BaseSceneからゲーム固有型への直接依存を外し、Player・GameRuleの借用参照を公開する。
// 主な入口：GetPlayer / SetPlayer / GetGameRule
class ISceneGameplayAccess {
  public:
    virtual ~ISceneGameplayAccess() = default;

    virtual Player* GetPlayer() const = 0;

    virtual void SetPlayer(Player* player) = 0;

    virtual GameRule* GetGameRule() { return nullptr; }
};

inline Player* GetScenePlayer(const BaseScene* scene) {
    const auto* gameplay = dynamic_cast<const ISceneGameplayAccess*>(scene);
    return gameplay ? gameplay->GetPlayer() : nullptr;
}

inline void SetScenePlayer(BaseScene* scene, Player* player) {
    auto* gameplay = dynamic_cast<ISceneGameplayAccess*>(scene);
    assert(gameplay && "Playerを読み込むSceneにはISceneGameplayAccessが必要です。");
    if (gameplay)
        gameplay->SetPlayer(player);
}

inline GameRule* GetSceneGameRule(BaseScene* scene) {
    auto* gameplay = dynamic_cast<ISceneGameplayAccess*>(scene);
    return gameplay ? gameplay->GetGameRule() : nullptr;
}
