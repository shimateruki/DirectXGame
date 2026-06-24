#pragma once
#include "Object3d.h"
#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Vector3i {
    int x, y, z;
};

// Object3d ポインタの組み合わせをハッシュ化するための関数オブジェクト
struct PairHash {
    std::size_t operator()(const std::pair<Object3d*, Object3d*>& p) const {
        auto hashA = std::hash<Object3d*>{}(p.first);
        auto hashB = std::hash<Object3d*>{}(p.second);
        return hashA ^ (hashB << 1);
    }
};

/// <summary>
/// レイキャスト結果を格納する構造体。
/// </summary>
struct RaycastHit {
    bool isHit = false;            // 衝突したか。
    Object3d* hitObject = nullptr; // 衝突したオブジェクト。
    Vector3 hitPoint;              // 衝突座標。
    float distance = 0.0f;         // レイ始点からの距離。
    Vector3 normal;                // 衝突面の法線。
};

/// <summary>
/// オブジェクト同士の衝突判定とレイキャストを管理するクラス。
/// </summary>
class CollisionManager {
public:
    static CollisionManager* GetInstance();

    /// <summary>
    /// 登録済みオブジェクト同士の衝突を判定する。
    /// </summary>
    void Update();

    void RemoveObject(Object3d* object);
    void AddObject(Object3d* object);
    void ClearObjects();
    void MarkStaticGridDirty() { needsStaticGridRebuild_ = true; }

    /// <summary>
    /// 空間分割グリッドの1辺のサイズを設定する。
    /// </summary>
    void SetGridSize(float size) { gridSize_ = size; }

    /// <summary>
    /// レイを飛ばし、指定マスクに合うオブジェクトとの最初の衝突を返す。
    /// </summary>
    RaycastHit Raycast(const Vector3& start, const Vector3& direction,
        float maxDistance, uint32_t mask = 0xFFFFFFFF);

    /// <summary>
    /// 登録済みオブジェクト一覧を取得する。
    /// </summary>
    const std::list<Object3d*>& GetObjects() const { return objects_; }

private:
    CollisionManager() = default;
    ~CollisionManager() = default;
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    // グリッドの1辺のサイズ。シーンの広さやオブジェクト密度に合わせて調整する。
    float gridSize_ = 5.0f;

    int64_t GetGridID(const Vector3& pos);
    Vector3i GetGridIndices(const Vector3& pos);
    int64_t GetGridIDFromIndices(int x, int y, int z);
    void CheckCollisionPair(Object3d* objA, Object3d* objB);

    /// <summary>
    /// 静的オブジェクトを staticGrid_ に登録する。
    /// </summary>
    void BuildStaticGrid();

    std::unordered_map<int64_t, std::list<Object3d*>> staticGrid_;
    bool needsStaticGridRebuild_ = true;

private:
    std::list<Object3d*> objects_;
    std::unordered_map<int64_t, std::list<Object3d*>> grid_;
    std::unordered_set<std::pair<Object3d*, Object3d*>, PairHash> checkedPairs_;
};
