#pragma once

#include "Object3d.h"

#include <memory>
#include <vector>

/// Sceneが所有するObject3dの追加、更新、描画、削除予約をまとめて扱います。
/// 更新中のvector変更を避けるため、追加と削除はフレーム末尾で確定します。
class ObjectManager {
public:
    void Update(float deltaTime);
    void Draw(ID3D12Resource* pointLight, ID3D12Resource* spotLight);
    void AddObject(std::unique_ptr<Object3d> object);
    void RequestRemove(Object3d* object);
    std::vector<std::unique_ptr<Object3d>>& GetObjects() { return objects_; }
    void DrawShadow();

private:
    void ProcessRemovals();

    std::vector<std::unique_ptr<Object3d>> objects_;        // 現在のSceneが所有するObject。
    std::vector<std::unique_ptr<Object3d>> pendingObjects_; // 次の更新境界でobjects_へ移す追加待ちObject。
    std::vector<Object3d*> removalList_;                    // 所有権を持たない削除予約ポインタ。
};
