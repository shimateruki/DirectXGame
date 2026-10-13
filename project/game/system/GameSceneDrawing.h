#pragma once

#include "BaseScene.h"
#include "BaseEnemy.h"

// ゲーム固有の敵の攻撃予告を、本体とは別の描画パスで集めます。
inline void DrawEnemyTelegraphs(const BaseScene& scene, ID3D12Resource* pointLight, ID3D12Resource* spotLight) {
    for (const auto& object : scene.GetObjects()) {
        if (auto* enemy = dynamic_cast<BaseEnemy*>(object.get())) {
            enemy->DrawAttackTelegraph(pointLight, spotLight);
        }
    }
}
