#pragma once
#include <vector>
#include <memory>
#include "Object3d.h"

// 前方宣言
class CollisionManager;

/// <summary>
/// ゲームオブジェクトのリスト管理、追加、削除を一括で行うクラス
/// </summary>
class ObjectManager {
public:
	/// <summary>
	/// 更新処理 (Update呼び出し、保留追加、削除処理)
	/// </summary>
	void Update(float deltaTime);

	/// <summary>
	/// 描画処理 (不透明 -> 透明 の順で描画など)
	/// </summary>
	void Draw(ID3D12Resource* pointLight, ID3D12Resource* spotLight);

	/// <summary>
	/// オブジェクトを追加
	/// </summary>
	void AddObject(std::unique_ptr<Object3d> object);

	/// <summary>
	/// 削除予約
	/// </summary>
	void RequestRemove(Object3d* object);

	/// <summary>
	/// 生のリストを取得 (LevelLoaderや他クラスからの参照用)
	/// </summary>
	std::vector<std::unique_ptr<Object3d>>& GetObjects() { return objects_; }

	/// <summary>
	/// 影パス用の描画処理（全オブジェクトの影を落とす）
	/// </summary>
	void DrawShadow();

private:
	// 削除処理の実体
	void ProcessRemovals();

private:
	std::vector<std::unique_ptr<Object3d>> objects_;
	std::vector<std::unique_ptr<Object3d>> pendingObjects_; // 追加待ち
	std::vector<Object3d*> removalList_;                    // 削除待ち
};