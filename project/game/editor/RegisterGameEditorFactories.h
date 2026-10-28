#pragma once

#include "EditorObjectFactoryRegistry.h"
#include "RegisterGameAttackPreviews.h"
#include "RegisterGameCopyPreviews.h"
#include "RegisterGameEditorEvents.h"
#include "game/editor/RegisterGameActorDebug.h"
#include "game/editor/CharacterStatusEditor.h"
#include "game/system/rules/GameplayStatusManager.h"
#include "../actor/enemy/spawn/EnemyFactory.h"
#include "game/system/scene/GimmickFactory.h"
#include "game/system/scene/ItemFactory.h"

// 具体的な敵・ギミック・アイテムの生成方法はゲーム側で登録します。
inline void RegisterGameEditorFactories() {
    RegisterGameAttackPreviews();
    RegisterGameCopyPreviews();
    RegisterGameEditorEvents();
    CharacterStatusEditor::Register();
#ifdef USE_IMGUI
    RegisterGameActorDebug();
#endif
    using Registry = EditorObjectFactoryRegistry;
    Registry::Register(Registry::Kind::Enemy, [](const std::string& type, engine::Object3dCommon* common) {
        return EnemyFactory::GetInstance()->CreateEnemy(type, common);
    });
    Registry::Register(Registry::Kind::Gimmick, [](const std::string& type, engine::Object3dCommon* common) -> std::unique_ptr<engine::Object3d> {
        auto* factory = GimmickFactory::GetInstance();
        return factory->IsKnownType(type) ? factory->CreateGimmick(type, common) : nullptr;
    });
    auto* statuses = GameplayStatusManager::GetInstance();
    statuses->Initialize();
    std::vector<Registry::TypeInfo> enemyTypes;
    for (const auto& type : statuses->GetEnemyTypes()) {
        const auto* preview = EnemyAttackPreviewRegistry::Find(type);
        const std::string label = preview ? preview->displayName : type;
        enemyTypes.push_back({type, label, [type](const engine::Object3d& object) { return object.GetEnemyType() == type; }});
    }
    Registry::RegisterTypes(Registry::Kind::Enemy, std::move(enemyTypes));
    std::vector<Registry::TypeInfo> gimmickTypes;
    for (const auto& type : GimmickFactory::GetInstance()->GetRegisteredTypes())
        gimmickTypes.push_back({type.id, type.displayName, type.matches});
    Registry::RegisterTypes(Registry::Kind::Gimmick, std::move(gimmickTypes));
    Registry::Register(Registry::Kind::Item, [](const std::string& type, engine::Object3dCommon* common) {
        return ItemFactory::GetInstance()->CreateItem(type, common);
    });
}
