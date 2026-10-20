#pragma once

#include "EditorObjectFactoryRegistry.h"
#include "../actor/enemy/spawn/EnemyFactory.h"
#include "game/system/scene/GimmickFactory.h"
#include "game/system/scene/ItemFactory.h"

// 具体的な敵・ギミック・アイテムの生成方法はゲーム側で登録します。
inline void RegisterGameEditorFactories() {
    using Registry = EditorObjectFactoryRegistry;
    Registry::Register(Registry::Kind::Enemy, [](const std::string& type, engine::Object3dCommon* common) {
        return EnemyFactory::GetInstance()->CreateEnemy(type, common);
    });
    Registry::Register(Registry::Kind::Gimmick, [](const std::string& type, engine::Object3dCommon* common) {
        return GimmickFactory::GetInstance()->CreateGimmick(type, common);
    });
    Registry::Register(Registry::Kind::Item, [](const std::string& type, engine::Object3dCommon* common) {
        return ItemFactory::GetInstance()->CreateItem(type, common);
    });
}
