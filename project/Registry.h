#pragma once
#pragma once
#include "Entity.h"
#include "TransformComponent.h"
#include "RenderComponent.h"
#include "PhysicsComponent.h"
#include <map>
#include <memory>
#include <vector>
#include <CollisionComponent.h>

// エンティティとコンポーネントを一元管理する「倉庫」
class Registry {
public:
    // --- エンティティの管理 ---
    Entity CreateEntity() {
        static Entity nextID = 1; // 1からIDを割り振る
        return nextID++;
    }
    void DestroyEntity(Entity entity); // (関連コンポーネントも全削除)

    // --- コンポーネントの追加・取得 ---

    // (例: TransformComponent)
    TransformComponent& AddTransform(Entity entity) {
        transforms_[entity] = TransformComponent{};
        return transforms_[entity];
    }
    TransformComponent* GetTransform(Entity entity) {
        if (transforms_.count(entity)) {
            return &transforms_[entity];
        }
        return nullptr;
    }

    // (例: RenderComponent)
    RenderComponent& AddRender(Entity entity) {
        renders_[entity] = RenderComponent{};
        return renders_[entity];
    }
    RenderComponent* GetRender(Entity entity) {};

    // ... (他のコンポーネントも同様) ...


    // --- システム(ロジック)が使う「ビュー」 ---
    // (例: Transform と Physics の両方を持つ Entity のリストを取得)
    std::vector<Entity> View_TransformAndPhysics();

private:
    // 各コンポーネントの倉庫 (ID -> 実体)
    std::map<Entity, TransformComponent> transforms_;
    std::map<Entity, RenderComponent> renders_;
    std::map<Entity, PhysicsComponent> physics_;
    std::map<Entity, CollisionComponent> collisions_;
};