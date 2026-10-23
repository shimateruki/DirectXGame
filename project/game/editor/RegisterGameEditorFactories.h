#pragma once

#include "EditorObjectFactoryRegistry.h"
#include "RegisterGameAttackPreviews.h"
#include "RegisterGameCopyPreviews.h"
#include "RegisterGameEditorEvents.h"
#include "game/editor/CharacterStatusEditor.h"
#include "../actor/enemy/spawn/EnemyFactory.h"
#include "game/system/scene/GimmickFactory.h"
#include "game/system/scene/ItemFactory.h"

// 具体的な敵・ギミック・アイテムの生成方法はゲーム側で登録します。
inline void RegisterGameEditorFactories() {
    RegisterGameAttackPreviews();
    RegisterGameCopyPreviews();
    RegisterGameEditorEvents();
    CharacterStatusEditor::Register();
    using Registry = EditorObjectFactoryRegistry;
    Registry::Register(Registry::Kind::Enemy, [](const std::string& type, engine::Object3dCommon* common) {
        return EnemyFactory::GetInstance()->CreateEnemy(type, common);
    });
    Registry::Register(Registry::Kind::Gimmick, [](const std::string& type, engine::Object3dCommon* common) -> std::unique_ptr<engine::Object3d> {
        auto* factory = GimmickFactory::GetInstance();
        return factory->IsKnownType(type) ? factory->CreateGimmick(type, common) : nullptr;
    });
    std::vector<Registry::TypeInfo> gimmickTypes;
    for (const auto& type : GimmickFactory::GetInstance()->GetRegisteredTypes())
        gimmickTypes.push_back({type.id, type.displayName, type.matches});
    Registry::RegisterTypes(Registry::Kind::Gimmick, std::move(gimmickTypes));
    Registry::Register(Registry::Kind::Item, [](const std::string& type, engine::Object3dCommon* common) {
        return ItemFactory::GetInstance()->CreateItem(type, common);
    });
}
