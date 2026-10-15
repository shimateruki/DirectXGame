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
    /// 所有権を受け取り、衝突管理へは直ちに登録する。描画・更新リストへは次の反映時に追加する。
    /// </summary>
    void AddObject(std::unique_ptr<engine::Object3d> object);

    /// <summary>EditorやReplayが直後に参照できるよう、所有権を受け取って即時登録する。</summary>
    /// <returns>登録したオブジェクトへの非所有ポインター。</returns>
    engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object);

    /// <summary>衝突登録と削除予約を外し、管理中のオブジェクトの所有権を取り出す。</summary>
    /// <returns>対象が見つからなければ空のunique_ptr。</returns>
    std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object);

    /// <summary>
    /// 管理中のオブジェクトを即座に非表示にし、走査終了後に衝突登録を解除する。
    /// リプレイ保持中を除き、所有権もその時点で破棄する。
    /// </summary>
    void RequestRemove(engine::Object3d* object);
    /// <summary>AIを更新せず、保留中の追加と削除だけを反映する。</summary>
    void ApplyPendingChanges();

    /// <summary>
    /// シーンやローダーから直接参照するためのオブジェクトリスト。
    /// </summary>
    const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const { return objects_; }

    /// <summary>
    /// シャドウパス用の描画を行う。
    /// </summary>
    void DrawShadow();

  private:
    /// <summary>削除予約済みオブジェクトを衝突管理と所有リストから取り除く。</summary>
    void ProcessRemovals();

  private:
    std::vector<std::unique_ptr<engine::Object3d>> objects_;
    std::vector<std::unique_ptr<engine::Object3d>> pendingObjects_; // 更新中に追加されたオブジェクトの待機場所。
    std::vector<engine::Object3d*> removalList_;                    // 削除予約されたオブジェクト。
};
