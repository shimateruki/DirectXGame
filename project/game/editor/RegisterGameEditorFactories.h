#pragma once

// ゲーム固有の登録入口：RegisterGameEditorFactories。登録先の共通Editorに、個別の型名や分岐を持ち込まない。

#include "EditorObjectFactoryRegistry.h"
#include "RegisterGameAttackPreviews.h"
#include "RegisterGameCopyPreviews.h"
#include "RegisterGameEditorEvents.h"
#include "RegisterGamePortraitExports.h"
#include "RegisterGameAnimatorEditors.h"
#include "RegisterGameInspectorExtensions.h"
#include "game/editor/RegisterGameActorDebug.h"
#include "game/editor/CharacterStatusEditor.h"
#include "game/system/rules/GameplayStatusManager.h"
#include "../actor/enemy/spawn/EnemyFactory.h"
#include "game/system/scene/GimmickFactory.h"
#include "game/system/scene/ItemFactory.h"

namespace GameEditorFactoryDetail {
// 表示名と、用途ごとの公開範囲・順番はゲームが決める。未指定の新種は末尾へ自動追加する。
inline EditorObjectFactoryRegistry::TypeInfo MakeEnemyTypeInfo(const std::string& type) {
    using Registry = EditorObjectFactoryRegistry;
    const auto* preview = EnemyAttackPreviewRegistry::Find(type);
    Registry::TypeInfo info{type, preview ? preview->displayName : type,
        [type](const engine::Object3D& object) { return object.GetEnemyType() == type; }};
    // 敵のタイプID・表示名と、生成一覧・Spawner一覧での掲載順を対応させる。
    struct Presentation {
        const char* id;
        const char* label;
        int creationOrder;
        int spawnerOrder; // 負値は、その用途では選択候補へ出さない。
    };
    static constexpr Presentation kPresentations[] = {
        {"Slime", "ピンクスライム", 3, 0},
        {"Bomb", "ボム", 4, 1},
        {"Bomber", "ボムスライム", 5, 2},
        {"GiantSlime", "巨大スライム", 6, 3},
        {"PrismSlime", "クリスタルスライム", 7, 4},
        {"MagmaSlime", "マグマスライム", 8, 5},
        {"FireSlime", "炎スライム", 0, 6},
        {"ThunderSlime", "雷スライム", 1, 7},
        {"WindSlime", "風スライム", 2, 8},
        {"RingBurner", "リングバーナー", 9, -1},
        {"FalseKingSlime", "偽王スライム", -1, -1},
    };
    for (const auto& presentation : kPresentations) {
        if (type != presentation.id) continue;
        info.displayName = presentation.label;
        info.creation = {presentation.creationOrder >= 0, presentation.creationOrder};
        info.spawner = {presentation.spawnerOrder >= 0, presentation.spawnerOrder};
        break;
    }
    return info;
}
} // namespace GameEditorFactoryDetail

// 具体的な敵・ギミック・アイテムの生成方法はゲーム側で登録します。
inline void RegisterGameEditorFactories() {
    RegisterGameAttackPreviews();
    RegisterGameCopyPreviews();
    RegisterGameEditorEvents();
    CharacterStatusEditor::Register();
#ifdef USE_IMGUI
    RegisterGamePortraitExports();
    RegisterGameAnimatorEditors();
    RegisterGameInspectorExtensions();
    RegisterGameActorDebug();
#endif
    using Registry = EditorObjectFactoryRegistry;
    Registry::Register(Registry::Kind::Enemy, [](const std::string& type, engine::Object3DCommon* common) {
        return EnemyFactory::GetInstance()->CreateEnemy(type, common);
    });
    Registry::Register(Registry::Kind::Gimmick, [](const std::string& type, engine::Object3DCommon* common) -> std::unique_ptr<engine::Object3D> {
        auto* factory = GimmickFactory::GetInstance();
        return factory->IsKnownType(type) ? factory->CreateGimmick(type, common) : nullptr;
    });
    auto* statuses = GameplayStatusManager::GetInstance();
    statuses->Initialize();
    std::vector<Registry::TypeInfo> enemyTypes;
    for (const auto& type : statuses->GetEnemyTypes())
        enemyTypes.push_back(GameEditorFactoryDetail::MakeEnemyTypeInfo(type));
    Registry::RegisterTypes(Registry::Kind::Enemy, std::move(enemyTypes));
    std::vector<Registry::TypeInfo> gimmickTypes;
    for (const auto& type : GimmickFactory::GetInstance()->GetRegisteredTypes())
        gimmickTypes.push_back({type.id, type.displayName, type.matches});
    Registry::RegisterTypes(Registry::Kind::Gimmick, std::move(gimmickTypes));
    Registry::Register(Registry::Kind::Item, [](const std::string& type, engine::Object3DCommon* common) {
        return ItemFactory::GetInstance()->CreateItem(type, common);
    });
}
