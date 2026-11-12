#pragma once

// ゲームシーンの特殊表示を共通の描画窓口へ接続する。シーンの進行状態や描画物は所有しない。

#include "BaseScene.h"
#include "BaseEnemy.h"
#include "Camera.h"
#include "RenderStats.h"
#include "engine/graphics/3d/material/MaterialRenderPolicy.h"

#include <functional>

// カメラ近傍は表示を優先し、遠方だけ視錐台で間引きます。
inline bool IsSceneObjectVisibleByFrustum(engine::Object3D* object, Camera* camera) {
    // カメラ近傍の大きなモデルは視錐台端で消えやすいため、一定距離だけ判定を省く。
    if (!object || !object->GetIsVisible()) {
        return false;
    }
    if (!camera) {
        return true;
    }

    const Aabb worldAabb = object->GetModelWorldAabb();
    constexpr float kNearObjectCullBypassDistance = 24.0f;
    if (engine::Math::DistanceSquaredPointAabb(camera->GetEye(), worldAabb.min, worldAabb.max) <=
        kNearObjectCullBypassDistance * kNearObjectCullBypassDistance) {
        return true;
    }

    const bool visible = engine::Math::IntersectFrustumAabb(camera->GetFrustum(), worldAabb.min, worldAabb.max);
    if (!visible) {
        engine::RenderStats::GetInstance()->RecordCulledObject();
    }
    return visible;
}

// 通常Objectの描画区間だけを共通化します。シーン固有の可視条件は filter に残します。
inline void DrawSceneObjectPass(const std::vector<std::unique_ptr<engine::Object3D>>& objects,
    MaterialRenderPolicy::Pass pass, ID3D12Resource* pointLight, ID3D12Resource* spotLight,
    engine::Object3D* viewActor, bool isFirstPerson, bool hideDescendants,
    const std::function<bool(engine::Object3D*)>& filter = {}) {
    // 一人称視点では本人と必要に応じて子Objectを除き、材質別パスに振り分ける。
    for (const auto& object : objects) {
        // Editorで一時非表示の実体は、派生Drawが所有する付属パーツごと除外する。
        if (!object || object->IsEditorTemporarilyHidden() || (filter && !filter(object.get()))) {
            continue;
        }

        if (isFirstPerson && viewActor) {
            engine::Object3D* current = object.get();
            while (current) {
                if (current == viewActor) {
                    break;
                }
                current = hideDescendants ? current->GetParent() : nullptr;
            }
            if (current) {
                continue;
            }
        }

        if (MaterialRenderPolicy::GetPass(object->GetMaterialType()) == pass) {
            object->Draw(pointLight, spotLight);
        }
    }
}

// ゲーム固有の敵の攻撃予告を、本体とは別の描画パスで集めます。
inline void DrawEnemyTelegraphs(const BaseScene& scene, ID3D12Resource* pointLight, ID3D12Resource* spotLight) {
    for (const auto& object : scene.GetObjects()) {
        if (auto* enemy = dynamic_cast<BaseEnemy*>(object.get())) {
            enemy->DrawAttackTelegraph(pointLight, spotLight);
        }
    }
}
