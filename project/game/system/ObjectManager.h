#pragma once
#include <memory>
#include <vector>
#include "Object3d.h"

// 前方宣言
class CollisionManager;

/// <summary>
/// ゲーム内 Object3d の追加、更新、描画、削除予約をまとめて扱う管理クラス。
/// </summary>
class ObjectManager {
public:
    /// <summary>
    /// 既存オブジェクトの更新、追加待ちの反映、削除予約の処理を行う。
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 登録済みオブジェクトを描画する。
    /// </summary>
    void Draw(ID3D12Resource* pointLight, ID3D12Resource* spotLight);

    /// <summary>
    /// 次の更新タイミングでシーンへ追加するオブジェクトを予約する。
    /// </summary>
    void AddObject(std::unique_ptr<Object3d> object);

    /// <summary>
    /// オブジェクトの削除を予約する。
    /// </summary>
    void RequestRemove(Object3d* object);

    /// <summary>
    /// シーンやローダーから直接参照するためのオブジェクトリスト。
    /// </summary>
    std::vector<std::unique_ptr<Object3d>>& GetObjects() { return objects_; }

    /// <summary>
    /// シャドウパス用の描画を行う。
    /// </summary>
    void DrawShadow();

private:
    // 削除予約リストに入っているオブジェクトを実際に取り除く
    void ProcessRemovals();

private:
    std::vector<std::unique_ptr<Object3d>> objects_;
    std::vector<std::unique_ptr<Object3d>> pendingObjects_; // 更新中に追加されたオブジェクトの待機場所。
    std::vector<Object3d*> removalList_;                    // 削除予約されたオブジェクト。
};
