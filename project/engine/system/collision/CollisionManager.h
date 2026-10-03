#pragma once
#include "Object3d.h"
#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// <summary>空間分割グリッドの整数座標です。</summary>
struct Vector3i {
    int x, y, z;
};

/// <summary>順序を統一したオブジェクトペアをハッシュ化します。</summary>
struct PairHash {
    std::size_t operator()(const std::pair<Object3d*, Object3d*>& p) const {
        auto hashA = std::hash<Object3d*>{}(p.first);
        auto hashB = std::hash<Object3d*>{}(p.second);
        return hashA ^ (hashB << 1);
    }
};

/// <summary>レイキャストで最初に見つかった衝突情報です。</summary>
struct RaycastHit {
    bool isHit = false;            // 衝突したか。
    Object3d* hitObject = nullptr; // 衝突したオブジェクト。
    Vector3 hitPoint;              // 衝突座標。
    float distance = 0.0f;         // レイ始点からの距離。
    Vector3 normal;                // 衝突面の法線。
};

/// <summary>Physics Query の対象レイヤーや除外条件を指定します。</summary>
struct PhysicsQueryFilter {
    uint32_t mask = 0xFFFFFFFFu;
    Object3d* ignoredObject = nullptr;
    bool ignoreDescendants = true;
    bool includeTriggers = false;
    bool includeDisabled = false;
};

/// <summary>Overlap Query の対象と、押し戻し情報です。</summary>
struct PhysicsOverlapHit {
    Object3d* object = nullptr;
    CollisionInfo collision;
};

/// <summary>登録オブジェクトの接触判定と Physics Query を管理します。</summary>
class CollisionManager {
public:
    /// <summary>エンジンで共有する衝突管理インスタンスを取得します。</summary>
    static CollisionManager* GetInstance();

    /// <summary>空間グリッドを使って登録オブジェクトの接触を更新します。</summary>
    void Update();

    /// <summary>オブジェクトを衝突管理対象から外します。</summary>
    void RemoveObject(Object3d* object);

    /// <summary>オブジェクトを衝突管理対象へ追加します。</summary>
    void AddObject(Object3d* object);

    /// <summary>登録オブジェクトと接触状態をすべてクリアします。</summary>
    void ClearObjects();

    /// <summary>静的オブジェクトが変化したことを通知し、次回の再構築を予約します。</summary>
    void MarkStaticGridDirty() { needsStaticGridRebuild_ = true; }

    /// <summary>空間グリッドのセル幅を設定し、静的グリッドを再構築対象にします。</summary>
    /// <param name="size">正の有限値で指定するセル幅。無効値は無視します。</param>
    void SetGridSize(float size);

    /// <summary>指定レイヤーマスクの対象へレイを飛ばし、最初の衝突を返します。</summary>
    RaycastHit Raycast(const Vector3& start, const Vector3& direction,
        float maxDistance, uint32_t mask = 0xFFFFFFFF);

    /// <summary>Trigger や除外対象も指定して、レイの最初の衝突を返します。</summary>
    RaycastHit Raycast(const Vector3& start, const Vector3& direction,
        float maxDistance, const PhysicsQueryFilter& filter);

    /// <summary>球と重なる対象の押し戻し情報を取得します。</summary>
    std::vector<PhysicsOverlapHit> OverlapSphere(
        const Vector3& center, float radius,
        const PhysicsQueryFilter& filter = PhysicsQueryFilter{}) const;

    /// <summary>AABB と重なる対象の押し戻し情報を取得します。</summary>
    std::vector<PhysicsOverlapHit> OverlapAABB(
        const AABB& bounds,
        const PhysicsQueryFilter& filter = PhysicsQueryFilter{}) const;

    /// <summary>カプセルと重なる対象の押し戻し情報を取得します。</summary>
    std::vector<PhysicsOverlapHit> OverlapCapsule(
        const Vector3& pointA, const Vector3& pointB, float radius,
        const PhysicsQueryFilter& filter = PhysicsQueryFilter{}) const;

    /// <summary>球を方向へ移動させ、最初の衝突を返します。</summary>
    RaycastHit SphereCast(
        const Vector3& start, float radius, const Vector3& direction, float maxDistance,
        const PhysicsQueryFilter& filter = PhysicsQueryFilter{}) const;

    /// <summary>カプセルを方向へ移動させ、最初の衝突を返します。</summary>
    RaycastHit CapsuleCast(
        const Vector3& pointA, const Vector3& pointB, float radius,
        const Vector3& direction, float maxDistance,
        const PhysicsQueryFilter& filter = PhysicsQueryFilter{}) const;

    /// <summary>登録中のオブジェクト一覧を取得します。</summary>
    const std::list<Object3d*>& GetObjects() const { return objects_; }

private:
    CollisionManager() = default;
    ~CollisionManager() = default;
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    // セル幅と静的オブジェクト用の再構築フラグは同時に更新します。
    float gridSize_ = 5.0f;

    // ワールド座標とグリッド座標の変換に使用します。
    int64_t GetGridID(const Vector3& pos);

    // ワールド座標を整数のセル座標へ、またセル座標を格納用IDへ変換します。
    Vector3i GetGridIndices(const Vector3& pos);
    int64_t GetGridIDFromIndices(int x, int y, int z);

    // 2 つの Collider を調べ、当フレームの接触ペアへ登録します。
    void CheckCollisionPair(Object3d* objA, Object3d* objB);

    // 前フレームとの差から Enter、Stay、Exit を通知します。
    void DispatchContactEvents();

    // 各 Query で共通のレイヤー・親子・Trigger 条件を評価します。
    bool PassesQueryFilter(Object3d* object, const PhysicsQueryFilter& filter) const;

    // レイキャストのオーバーロードから共通処理へ合流します。
    RaycastHit RaycastInternal(
        const Vector3& start, const Vector3& direction, float maxDistance,
        const PhysicsQueryFilter& filter, bool excludePlayerHierarchy) const;

    // 静的オブジェクトをセルへ登録し直します。
    void BuildStaticGrid();

    std::unordered_map<int64_t, std::list<Object3d*>> staticGrid_;
    bool needsStaticGridRebuild_ = true;

private:
    // 動的な衝突ペアの候補と、前・現在フレームの接触状態を分けて保持します。
    std::list<Object3d*> objects_;
    std::unordered_map<int64_t, std::list<Object3d*>> grid_;
    std::unordered_set<std::pair<Object3d*, Object3d*>, PairHash> checkedPairs_;

    /// <summary>前後フレームで比較する接触情報とTrigger種別。</summary>
    struct ContactState {
        CollisionInfo firstInfo;
        bool isTrigger = false;
    };
    std::unordered_map<std::pair<Object3d*, Object3d*>, ContactState, PairHash> previousContacts_;
    std::unordered_map<std::pair<Object3d*, Object3d*>, ContactState, PairHash> currentContacts_;
};
