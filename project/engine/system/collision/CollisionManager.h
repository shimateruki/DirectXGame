#pragma once
#include "Object3d.h"
#include <vector>
#include <list>
#include <unordered_map>
#include <cstdint>  
#include <unordered_set>

struct Vector3i {
    int x, y, z;
};

// 2つのポインタのペアをハッシュ化するためのカスタムハッシュ
struct PairHash {
    std::size_t operator()(const std::pair<Object3d*, Object3d*>& p) const {
        // 簡易的なハッシュミックス
        auto hashA = std::hash<Object3d*>{}(p.first);
        auto hashB = std::hash<Object3d*>{}(p.second);
        return hashA ^ (hashB << 1);
    }
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
    float gridSize_ = 5.0f;

    /// ★ 3D座標からグリッドID（ハッシュ値）を計算する
    int64_t GetGridID(const Vector3& pos);

    Vector3i GetGridIndices(const Vector3& pos);



    /// ★ (x, y, z) のインデックスからグリッドIDを計算する
    int64_t GetGridIDFromIndices(int x, int y, int z);

    /// ★ 衝突判定のペアを実行するヘルパー関数
    void CheckCollisionPair(Object3d* objA, Object3d* objB);


    /// <summary>
    /// 静的オブジェクトを staticGrid_ に登録する (内部関数)
    /// </summary>
    void BuildStaticGrid();

    // ★ 静的オブジェクト（地面、壁など）専用のグリッド
    std::unordered_map<int64_t, std::list<Object3d*>> staticGrid_;

    // ★ 静的グリッドを再構築する必要があるか
    bool needsStaticGridRebuild_ = true;

private:
    // 衝突判定を取りたいオブジェクトのリスト
    std::list<Object3d*> objects_;

    // ★ グリッド本体。キーはグリッドID、値はそのセル内のオブジェクトリスト
    std::unordered_map<int64_t, std::list<Object3d*>> grid_;

    // ★ 判定済みのペアを記録するためのセット
    std::unordered_set<std::pair<Object3d*, Object3d*>, PairHash> checkedPairs_;
};