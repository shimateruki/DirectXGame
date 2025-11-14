#pragma once
#include "Object3d.h"
#include <vector>
#include <list>
#include <map>      
#include <cstdint>  
#include <set>

struct Vector3i {
    int x, y, z;
};

/// <summary>
/// 衝突判定を管理するクラス
/// </summary>
class CollisionManager {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static CollisionManager* GetInstance();

    /// <summary>
    /// 更新処理。毎フレーム、全オブジェクトの衝突をチェックする
    /// (グリッド分割を使用して効率化)
    /// </summary>
    void Update();

    void RemoveObject(Object3d* object);

    /// <summary>
    /// 衝突判定リストにオブジェクトを追加
    /// </summary>
    void AddObject(Object3d* object);

    /// <summary>
    /// 衝突判定リストから全てのオブジェクトをクリア
    /// </summary>
    void ClearObjects();

    /// <summary>
    /// （デバッグ用）グリッドの1辺のサイズを設定
    /// </summary>
    void SetGridSize(float size) { gridSize_ = size; }

private:
    CollisionManager() = default;
    ~CollisionManager() = default;
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    /// ★ グリッドの1辺のサイズ (シーンの広さに合わせて調整)
    float gridSize_ = 10.0f;

    /// ★ 3D座標からグリッドID（ハッシュ値）を計算する
    int64_t GetGridID(const Vector3& pos);

    /// ★ グリッドIDから (x, y, z) のインデックスを取得する
    Vector3i GetGridIndices(int64_t gridID);

    /// ★ (x, y, z) のインデックスからグリッドIDを計算する
    int64_t GetGridIDFromIndices(int x, int y, int z);

    /// ★ 衝突判定のペアを実行するヘルパー関数
    void CheckCollisionPair(Object3d* objA, Object3d* objB);

private:
    // 衝突判定を取りたいオブジェクトのリスト
    std::list<Object3d*> objects_;

    // ★ グリッド本体。キーはグリッドID、値はそのセル内のオブジェクトリスト
    std::map<int64_t, std::list<Object3d*>> grid_;

    // ★ 判定済みのペアを記録するためのセット
    std::set<std::pair<Object3d*, Object3d*>> checkedPairs_;
};